// Copyright (c) 2015-2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0


#include "bluetoothpbapprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "config.h"

#define BLUETOOTH_PROFILE_PBAP_MAX_REQUEST_ID 999

BluetoothPbapProfileService::BluetoothPbapProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "PBAP", "00001130-0000-1000-8000-00805f9b34fb"),
        mIncomingAccessRequestWatch(nullptr),
        mAccessRequestsAllowed(false),
        mRequestIndex(0),
        mNextRequestId(1)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, setPhoneBook)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, getSize)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, vCardListing)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, getPhoneBookProperties)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, getvCardFilters)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, pullvCard)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, searchPhoneBook)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, pullPhoneBook)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, awaitAccessRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, acceptAccessRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, rejectAccessRequest)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/pbap", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/pbap", this);

	mGetPropertiesSubscriptions.setServiceHandle(manager);
}

BluetoothPbapProfileService::~BluetoothPbapProfileService()
{
}

void BluetoothPbapProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothPbapProfile>()->registerObserver(this);
}

void BluetoothPbapProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothPbapProfile>(adapterAddress)->registerObserver(this);
}

bool BluetoothPbapProfileService::awaitAccessRequest(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string)) REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (mIncomingAccessRequestWatch)
	{
		LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getManager()->getPowered(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_TURNED_OFF);
		return true;
	}

	mIncomingAccessRequestWatch = new LSUtils::ClientWatch(getManager()->get(), &message, [this]()
	{
		notifyAccessRequestListenerDropped();
	});

	setAccessRequestsAllowed(true);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(mIncomingAccessRequestWatch->getMessage(), responseObj);

	return true;
}

void BluetoothPbapProfileService::propertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPropertiesList properties)
{
	BluetoothProfileService::propertiesChanged(adapterAddress, address, properties);
}

void BluetoothPbapProfileService::propertiesChanged(const std::string &address, BluetoothPropertiesList properties)
{
	BluetoothProfileService::propertiesChanged(address, properties);
}

void BluetoothPbapProfileService::transferStatusChanged(const std::string &adapterAddress, const std::string &address, const std::string &destinationPath, const std::string &objectPath, const std::string &state)
{
	BT_INFO("PBAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("destinationFile",destinationPath);
	responseObj.put("status",state);

	auto itr = mGetPhoneBookSubscriptions.find(objectPath);
	if(itr != mGetPhoneBookSubscriptions.end())
	{
		LS::SubscriptionPoint* subscriptionPoint = itr->second;
		if(("completed" == state)||("error" == state))
		{
			responseObj.put("subscribed",false);
			if ("error" == state)
				responseObj.put("returnValue",false);
			else
				responseObj.put("returnValue",true);
			LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
			mGetPhoneBookSubscriptions.erase(itr);
			delete subscriptionPoint;
			return;
		}

		responseObj.put("subscribed",true);
		responseObj.put("returnValue",true);
		LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
	}
}

void BluetoothPbapProfileService::setAccessRequestsAllowed(bool state)
{
	BT_DEBUG("Setting Access request to %d", state);

	if (!state && mIncomingAccessRequestWatch)
	{
		delete mIncomingAccessRequestWatch;
		mIncomingAccessRequestWatch = 0;
	}

	mAccessRequestsAllowed = state;
}

bool BluetoothPbapProfileService::notifyAccessRequestListenerDropped()
{
	setAccessRequestsAllowed(false);

	return false;
}

bool BluetoothPbapProfileService::prepareConfirmationRequest(LS::Message &request, pbnjson::JValue &requestObj, bool accept)
{
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothPbapProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(requestId, string), PROP(adapterAddress, string)) REQUIRED_1(requestId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("requestId"))
			LSUtils::respondWithError(request, BT_ERR_PBAP_REQUESTID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!mAccessRequestsAllowed)
	{
		LSUtils::respondWithError(request, BT_ERR_PBAP_ACCESS_NOT_ALLOWED);
		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string requestIdStr = requestObj["requestId"].asString();
	AccessRequest *accessRequest = findRequest(requestIdStr);

	if (!accessRequest)
	{
		LSUtils::respondWithError(request, BT_ERR_PBAP_REQUESTID_NOT_EXIST);
		return true;
	}

	BluetoothPbapAccessRequestId accessRequestId = findAccessRequestId(requestIdStr);

	if (BLUETOOTH_PBAP_ACCESS_REQUEST_ID_INVALID == accessRequestId)
	{
		LSUtils::respondWithError(request, BT_ERR_PBAP_ACCESS_REQUEST_NOT_EXIST);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto accessRequestCallback = [this, requestMessage, adapterAddress](BluetoothError error)
	{
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifyConfirmationRequest(request, adapterAddress, false);
		else
			notifyConfirmationRequest(request, adapterAddress, true);
	};

	getImpl<BluetoothPbapProfile>()->supplyAccessConfirmation(accessRequestId, accept, accessRequestCallback);

	deleteAccessRequestId(requestIdStr);
	deleteAccessRequest(requestIdStr);

	return true;
}

bool BluetoothPbapProfileService::prepareSetPhoneBook(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string),
												PROP(repository, string), PROP(object, string))
												REQUIRED_3(address, repository, object));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("repository"))
			LSUtils::respondWithError(request, BT_ERR_PBAP_REPOSITORY_PARAM_MISSING);

		else if (!requestObj.hasKey("object"))
			LSUtils::respondWithError(request, BT_ERR_PBAP_OBJECT_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

	return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}
	return true;
}

bool BluetoothPbapProfileService::prepareGetSize(LS::Message &request, pbnjson::JValue &requestObj)
{

	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string))
                                              REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}
	return true;
}

bool BluetoothPbapProfileService::prepareGetvCardFilters(LS::Message &request, pbnjson::JValue &requestObj)
{

	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string))
                                              REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}
	return true;
}

bool BluetoothPbapProfileService::getSize(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareGetSize(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address =  requestObj["address"].asString();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto getSizeCallback = [this, adapterAddress, address, requestMessage](BluetoothError error,uint16_t size) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifyGetSizeRequest(request, error, adapterAddress, address, size,false);
		else
			notifyGetSizeRequest(request, error, adapterAddress, address, size,true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->getPhonebookSize(address,getSizeCallback);

	return true;
}

bool BluetoothPbapProfileService::getvCardFilters(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareGetvCardFilters(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address =  requestObj["address"].asString();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto getFilterList = [this, adapterAddress, address, requestMessage](BluetoothError error,std::list<std::string> filters) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifyGetvCardFiltersRequest(request, error, adapterAddress, address, filters, false);
		else
			notifyGetvCardFiltersRequest(request, error, adapterAddress, address, filters, true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->getvCardFilters(address,getFilterList);

	return true;
}

bool BluetoothPbapProfileService::setPhoneBook(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareSetPhoneBook(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string repository = requestObj["repository"].asString();
	std::string object = requestObj["object"].asString();

	setFolderRepository(repository);
	setFolderObject(object);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto setPhoneBookCallback = [this, adapterAddress,address, requestMessage](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifySetPhoneBookRequest(request, error, adapterAddress, address, false);
		else
			notifySetPhoneBookRequest(request, error, adapterAddress, address, true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->setPhoneBook(address, repository, object, setPhoneBookCallback);

	return true;
}


pbnjson::JValue BluetoothPbapProfileService::createJsonFilterList(std::list<std::string> filters)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto data = filters.begin(); data != filters.end(); data++)
	{
		platformObjArr.append(data->c_str());
	}
	return platformObjArr;
}

void BluetoothPbapProfileService::notifyGetvCardFiltersRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, std::list<std::string> filters, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", success);
	responseObj.put("filters", createJsonFilterList(filters));
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::notifyGetSizeRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, uint16_t size, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", success);
	responseObj.put("size", size);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::notifySetPhoneBookRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", success);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::notifyConfirmationRequest(LS::Message &request, const std::string &adapterAddress, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, BT_ERR_PBAP_STATE_ERR);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("returnValue", success);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

uint64_t BluetoothPbapProfileService::getAccessRequestId(const std::string &requestIdStr)
{
	uint64_t accessRequestId = 0;

	for (auto propIter : mAccessRequests)
	{
		AccessRequest *accessRequest = propIter.second;

		if (accessRequest->requestId == requestIdStr)
		{
			accessRequestId = (int64_t) propIter.first;
			break;
		}
	}

	return accessRequestId;
}

void BluetoothPbapProfileService::deleteAccessRequestId(const std::string &requestIdStr)
{
	uint64_t requestId = getAccessRequestId(requestIdStr);

	auto idIter = mAccessRequestIds.find(requestId);
	if (idIter == mAccessRequestIds.end())
		return;

	mAccessRequestIds.erase(idIter);
}

void BluetoothPbapProfileService::deleteAccessRequest(const std::string &requestId)
{
	for (auto propIter = mAccessRequests.begin(); propIter != mAccessRequests.end(); propIter++)
	{
		AccessRequest *accessRequest = propIter->second;
		if (accessRequest->requestId == requestId)
		{
			delete accessRequest;
			mAccessRequests.erase(propIter);
			break;
		}
	}
}

BluetoothPbapAccessRequestId BluetoothPbapProfileService::findAccessRequestId(const std::string &requestIdStr)
{
	BluetoothPbapAccessRequestId accessRequestId = BLUETOOTH_PBAP_ACCESS_REQUEST_ID_INVALID;
	uint64_t requestId = getAccessRequestId(requestIdStr);
	auto idIter = mAccessRequestIds.find(requestId);

	if (idIter != mAccessRequestIds.end())
	{
		accessRequestId = idIter->second;
	}

	return accessRequestId;
}

BluetoothPbapProfileService::AccessRequest *BluetoothPbapProfileService::findRequest(const std::string &requestIdStr)
{
	for (auto propIter : mAccessRequests)
	{
		AccessRequest *accessRequest = propIter.second;

		if (accessRequest->requestId == requestIdStr)
			return accessRequest;
	}

	return nullptr;
}

void BluetoothPbapProfileService::assignAccessRequestId(AccessRequest *accessRequest)
{
	std::string mNextRequestIdStr = std::to_string(mNextRequestId);

	auto padStr = [](std::string & str, const size_t num, const char paddingChar)
	{
		if (num > str.size())
			str.insert(0, num - str.size(), paddingChar);
	};

	padStr(mNextRequestIdStr, 3, '0');
	mNextRequestId++;

	accessRequest->requestId = mNextRequestIdStr;
}

void BluetoothPbapProfileService::createAccessRequest(BluetoothPbapAccessRequestId accessRequestId, const std::string &address, const std::string &deviceName)
{
	AccessRequest *accessRequest = new AccessRequest();

	accessRequest->address = address;
	accessRequest->name = deviceName;

	if (mNextRequestId > BLUETOOTH_PROFILE_PBAP_MAX_REQUEST_ID)
		mNextRequestId = 1;

	assignAccessRequestId(accessRequest);

	mAccessRequests.insert(std::pair<uint64_t, AccessRequest *>(mRequestIndex, accessRequest));
	mAccessRequestIds.insert(std::pair<uint64_t, BluetoothPbapAccessRequestId>(mRequestIndex, accessRequestId));
	notifyAccessRequestConfirmation(mRequestIndex);
	mRequestIndex++;
}

void BluetoothPbapProfileService::notifyAccessRequestConfirmation(uint64_t requestIndex)
{
	pbnjson::JValue object = pbnjson::Object();
	auto requestIter = mAccessRequests.find(requestIndex);

	if (requestIter != mAccessRequests.end())
	{
		AccessRequest *accessRequest = requestIter->second;

		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("requestId", accessRequest->requestId);
		responseObj.put("address", accessRequest->address);
		responseObj.put("name", accessRequest->name);

		object.put("request", responseObj);
		LSUtils::postToClient(mIncomingAccessRequestWatch->getMessage(), object);
	}
}

void BluetoothPbapProfileService::accessRequested(BluetoothPbapAccessRequestId  accessRequestId, const std::string &address, const std::string &deviceName)
{
	BT_DEBUG("Received PBAP access request from %s and device name %s", address.c_str(), deviceName.c_str());
	if (!mAccessRequestsAllowed)
	{
		BT_DEBUG("Not allowed to accept PBAP access request");
		return;
	}

	createAccessRequest(accessRequestId, address, deviceName);
}

bool BluetoothPbapProfileService::acceptAccessRequest(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	return prepareConfirmationRequest(request, requestObj, true);
}

bool BluetoothPbapProfileService::rejectAccessRequest(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	return prepareConfirmationRequest(request, requestObj, false);
}

bool BluetoothPbapProfileService::prepareVCardListing(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(address, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}
	return true;
}

bool BluetoothPbapProfileService::vCardListing(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareVCardListing(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address =  requestObj["address"].asString();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto vCardListingResultCallback = [this, address, adapterAddress, requestMessage](BluetoothError error, std::map<std::string, std::string> &list) {
		LS::Message request(requestMessage);
		if (BLUETOOTH_ERROR_NONE != error)
			notifyVCardListingRequest(request, error, adapterAddress, address, list, false);
		else
			notifyVCardListingRequest(request, error, adapterAddress, address, list, true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->vCardListing(address, vCardListingResultCallback);
	return true;
}

bool BluetoothPbapProfileService::prepareSearchPhoneBook(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string), PROP(address, string), PROP(order, string), OBJECT(filter, OBJSCHEMA_2(PROP(key, string), PROP(value, string)))) REQUIRED_2(address, filter));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("filter"))
		{
			LSUtils::respondWithError(request, BT_ERR_PBAP_FILTER_PARAM_MISSING);
		}
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}

	return true;
}

bool BluetoothPbapProfileService::searchPhoneBook(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareSearchPhoneBook(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address =  requestObj["address"].asString();

	std::string searchOrder = "indexed";
	if(requestObj.hasKey("order"))
		searchOrder =  requestObj["order"].asString();

	std::string filterKey, filterValue;
	if (requestObj.hasKey("filter"))
	{
		auto searchFiltersObj = requestObj["filter"];

		if (searchFiltersObj.hasKey("key"))
			filterKey = searchFiltersObj["key"].asString();

		if (searchFiltersObj.hasKey("value"))
			filterValue = searchFiltersObj["value"].asString();
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto SearchPhoneBookResultCallback = [this, address, adapterAddress, requestMessage](BluetoothError error, std::map<std::string, std::string> &list) {
		LS::Message request(requestMessage);
		if (BLUETOOTH_ERROR_NONE != error)
			notifySearchPhoneBookRequest(request, error, adapterAddress, address, list, false);
		else
			notifySearchPhoneBookRequest(request, error, adapterAddress, address, list, true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->searchPhoneBook(address, searchOrder, filterKey, filterValue, SearchPhoneBookResultCallback);

	return true;
}

void BluetoothPbapProfileService::notifyVCardListingRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, BluetoothPbapVCardList &list, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", success);
	responseObj.put("vcfHandles", createJsonVCardListing(list));
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::notifySearchPhoneBookRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, BluetoothPbapVCardList &list, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", success);
	responseObj.put("vcfHandles", createJsonVCardListing(list));
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

pbnjson::JValue BluetoothPbapProfileService::createJsonVCardListing(BluetoothPbapVCardList &list)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto data = list.begin(); data != list.end(); data++)
	{
		pbnjson::JValue cmdRply = pbnjson::Object();
		cmdRply.put("Handle", data->first);
		cmdRply.put("Name", data->second);
		platformObjArr << cmdRply;
	}
	return platformObjArr;
}

bool BluetoothPbapProfileService::prepareGetPhoneBookProperties(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();
	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}

	return true;
}

bool BluetoothPbapProfileService::getPhoneBookProperties(LSMessage &message)
{

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	bool subscribed = false;

	if (!prepareGetPhoneBookProperties(request, requestObj))
		return true;

	std::string adapterAddress;

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	initializePbapApplicationParameters();

	std::string address =  requestObj["address"].asString();

	if (request.isSubscription())
	{
		mGetPropertiesSubscriptions.subscribe(request);
		subscribed = true;
	}

	pbapApplicationParameters.setFolder("No folder is selected");

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto getPhoneBookPropertiesResultCallback = [this, address, adapterAddress, requestMessage, subscribed](BluetoothError error, BluetoothPbapApplicationParameters &applicationParams) {
		LS::Message request(requestMessage);
		bool success =false;
		if (error == BLUETOOTH_ERROR_NONE)
		{
			updateFromPbapProperties(applicationParams);
			success =true;
		}
		notifyGetPhoneBookPropertiesRequest (request, error, adapterAddress, address, subscribed, success);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->getPhoneBookProperties(address,getPhoneBookPropertiesResultCallback);

	return true;
}

void BluetoothPbapProfileService::appendCurrentProperties(pbnjson::JValue &object)
{
	BT_INFO("PBAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	if( retrieveErrorCodeText(BLUETOOTH_ERROR_PBAP_CALL_SELECT_FOLDER_TYPE) == pbapApplicationParameters.getFolder())
		return;

	pbnjson::JValue propertyObj = pbnjson::Object();

	propertyObj.put("repository",pbapApplicationParameters.getFolder());
	propertyObj.put("databaseIdentifier", pbapApplicationParameters.getDataBaseIdentifier());
	propertyObj.put("primaryVersionCounter", pbapApplicationParameters.getPrimaryCounter());
	propertyObj.put("secondaryVersionCounter", pbapApplicationParameters.getSecondaryCounter());
	propertyObj.put("fixedImageSize", pbapApplicationParameters.getFixedImageSize());

	object.put("properties", propertyObj);
}

void BluetoothPbapProfileService::notifyGetPhoneBookPropertiesRequest (LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, bool subscribed, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	appendCurrentProperties(responseObj);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::notifySubscribersAboutPropertiesChange(const std::string &adapterAddress, const std::string &address)
{
	BT_INFO("PBAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	appendCurrentProperties(responseObj);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	LSUtils::postToSubscriptionPoint(&mGetPropertiesSubscriptions, responseObj);
}

void BluetoothPbapProfileService::profilePropertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPbapApplicationParameters &properties)
{
	BT_DEBUG("Bluetooth PBAP properties have changed");
	updateFromPbapProperties(properties);
	notifySubscribersAboutPropertiesChange(adapterAddress, address);
}

void BluetoothPbapProfileService::updateFromPbapProperties(BluetoothPbapApplicationParameters &applicationParams)
{
	pbapApplicationParameters.setFolder(applicationParams.getFolder());
	pbapApplicationParameters.setPrimaryCounter(applicationParams.getPrimaryCounter());
	pbapApplicationParameters.setSecondaryCounter(applicationParams.getSecondaryCounter());
	pbapApplicationParameters.setDataBaseIdentifier(applicationParams.getDataBaseIdentifier());
	pbapApplicationParameters.setFixedImageSize(applicationParams.getFixedImageSize());
}

void BluetoothPbapProfileService::initializePbapApplicationParameters()
{
	pbapApplicationParameters.setFolder("NULL");
	pbapApplicationParameters.setPrimaryCounter("NULL");
	pbapApplicationParameters.setSecondaryCounter("NULL");
	pbapApplicationParameters.setDataBaseIdentifier("NULL");
	pbapApplicationParameters.setFixedImageSize(false);
}

bool BluetoothPbapProfileService::pullvCard(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!preparePullVcard(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address =  requestObj["address"].asString();
	std::string vCardHandle =  requestObj["vCardHandle"].asString();
	std::string vCardVersion = "2.1";

	if (requestObj.hasKey("vCardVersion"))
	{
		vCardVersion = requestObj["vCardVersion"].asString();
	}

	std::string check_destinationFile;

	if (requestObj.hasKey("vCardVersion"))
	{
		check_destinationFile = requestObj["destinationFile"].asString();
		if(check_destinationFile.empty())
		{
			check_destinationFile = vCardHandle;
		}
	}
	else
	{
		check_destinationFile = vCardHandle;
	}

	std::string destinationFile = buildStorageDirPath(check_destinationFile, address);

	if (!checkPathExists(destinationFile)) {
		std::string errorMessage = "Supplied destination path ";
		errorMessage += destinationFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_DESTPATH_INVALID);
		return true;
	}

	BluetoothPbapVCardFilterList vCardFilters;

	if (requestObj.hasKey("filterFields"))
	{
		auto vCardFiltersObjArray = requestObj["filterFields"];
		for (int n = 0; n < vCardFiltersObjArray.arraySize(); n++)
		{
			pbnjson::JValue element = vCardFiltersObjArray[n];
			vCardFilters.push_back(element.asString());
		}
	}
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto pullResultCallback = [this, address, adapterAddress, requestMessage, destinationFile](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifyPullVcardRequest(request, error, adapterAddress, address, destinationFile, false);
		else
			notifyPullVcardRequest(request, error, adapterAddress, address, destinationFile, true);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->pullvCard(address, destinationFile, vCardHandle, vCardVersion, vCardFilters, pullResultCallback);

	return true;

}

bool BluetoothPbapProfileService::pullPhoneBook(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!parseGetPhoneBookParam(request, requestObj))
		return true;

	std::string address =  requestObj["address"].asString();
	std::string check_destinationFile = requestObj["destinationFile"].asString();

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;;
	}

	if(!isDeviceConnected(adapterAddress, address))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	std::string vCardVersion = "2.1";
	if (requestObj.hasKey("vCardVersion"))
		vCardVersion = requestObj["vCardVersion"].asString();

	std::string destinationFile = buildStorageDirPath(check_destinationFile, address);

	if (!checkPathExists(destinationFile)) {
		std::string errorMessage = "Supplied destination path ";
		errorMessage += destinationFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_DESTPATH_INVALID);
		return true;
	}

	BluetoothPbapVCardFilterList vCardFilters;

	if (requestObj.hasKey("filterFields"))
	{
		auto vCardFiltersObjArray = requestObj["filterFields"];
		for (int n = 0; n < vCardFiltersObjArray.arraySize(); n++)
		{
			pbnjson::JValue element = vCardFiltersObjArray[n];
			vCardFilters.push_back(element.asString());
		}
	}

	uint16_t startIndex = 0;
	if (requestObj.hasKey("startOffset"))
		startIndex = (uint16_t)requestObj["startOffset"].asNumber<int32_t>();

	uint16_t maxListCount = 65535;
	if (requestObj.hasKey("maxListCount"))
		maxListCount = (uint16_t)requestObj["maxListCount"].asNumber<int32_t>();

	bool subscribed = false;
	if (request.isSubscription())
	{
		subscribed = true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto pullResultCallback = [this, address, adapterAddress, requestMessage,subscribed, destinationFile](BluetoothError error, std::string objectPath) {
		LS::Message request(requestMessage);
		if((subscribed)&&(BLUETOOTH_ERROR_NONE == error))
		{
			LS::SubscriptionPoint* subscriptionPoint = new LS::SubscriptionPoint;
			mGetPhoneBookSubscriptions.insert({objectPath,subscriptionPoint});
			subscriptionPoint->setServiceHandle(getManager());
			subscriptionPoint->subscribe(request);
		}

		sendGetPhoneBookResponse(request, error, adapterAddress, address, destinationFile, subscribed);
	};

	getImpl<BluetoothPbapProfile>(adapterAddress)->pullPhoneBook(address, destinationFile, vCardVersion, vCardFilters, startIndex ,maxListCount,pullResultCallback);
	return true;
}

bool BluetoothPbapProfileService::parseGetPhoneBookParam(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_8(PROP(address, string), PROP(adapterAddress, string),
												PROP(destinationFile, string), PROP(startOffset, integer),
												PROP(vCardVersion, string),ARRAY(filterFields,string),
												PROP(maxListCount, integer),PROP_WITH_VAL_1(subscribe, boolean, true))
												REQUIRED_3(address,destinationFile,subscribe));

	paramList localParam ={{"address", BT_ERR_ADDR_PARAM_MISSING},
							   {"destinationFile", BT_ERR_DESTFILE_PARAM_MISSING},
							   {"subscribe", BT_ERR_MTHD_NOT_SUBSCRIBED}};

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		updateParseError(request,requestObj,parseError,localParam);
		return false;
	}

	return true;
}

bool BluetoothPbapProfileService::updateMissingParamError(LS::Message &request , pbnjson::JValue &requestObj, paramList &mandatoryParamList)
{
	for (auto iterParam : mandatoryParamList)
	{
		if (!requestObj.hasKey(iterParam.first))
			{
				LSUtils::respondWithError(request, iterParam.second);
				return true;
			}
	}
	return false;
}

void BluetoothPbapProfileService::updateParseError(LS::Message &request , pbnjson::JValue &requestObj, int parseError, paramList &mandatoryParamList)
{
	if (JSON_PARSE_SCHEMA_ERROR != parseError)
		LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
	else if (updateMissingParamError(request,requestObj,mandatoryParamList))
		return;
	else
		LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
}

bool BluetoothPbapProfileService::preparePullVcard(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(address, string), PROP(adapterAddress, string),
												PROP(destinationFile, string), PROP(vCardHandle, string), PROP(vCardVersion, string), ARRAY(filterFields, string))
												REQUIRED_2(address, vCardHandle));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("vCardHandle"))
			LSUtils::respondWithError(request, BT_ERR_PBAP_VCARD_HANDLE_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

	return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return false;
		}
	}
	return true;
}

void BluetoothPbapProfileService::notifyPullVcardRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, const std::string &destinationFile, bool success)
{
	if (!success)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	appendGenericPullResponse(responseObj,adapterAddress,address,destinationFile);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::sendGetPhoneBookResponse(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, const std::string &destinationFile,const bool &subscribed)
{
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	appendGenericPullResponse(responseObj,adapterAddress,address,destinationFile);
	responseObj.put("subscribed", subscribed);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothPbapProfileService::appendGenericPullResponse(pbnjson::JValue &responseObj, const std::string &adapterAddress, const std::string &address, const std::string &destinationFile)
{
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("returnValue", true);
	responseObj.put("destinationFile", destinationFile);
}

std::string BluetoothPbapProfileService::buildStorageDirPath(const std::string &path, const std::string &address)
{
	std::string result = WEBOS_MOUNTABLESTORAGEDIR;
	result += "/";
	result += "pbap";
	result += "/";
	result += replaceString(convertToLower(address),":","_");
	result += "/";
	result += getFolderRepository();
	result += "/";
	result += getFolderObject();
	result += "/";
	if (g_mkdir_with_parents(result.c_str(), 0755) != 0)
	{
		BT_DEBUG("failed to create folder: %s", result.c_str());
	}
	result += path;
	return result;
}

