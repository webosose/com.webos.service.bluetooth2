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
	BluetoothProfileService::propertiesChanged(getManager()->getAddress(), address, properties);
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
			LSUtils::respondWithError(request, BT_ERR_REPOSITORY_PARAM_MISSING);

		else if (!requestObj.hasKey("object"))
			LSUtils::respondWithError(request, BT_ERR_OBJECT_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

	return false;
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return false;
	}

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
	if (requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return false;
	}

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
	if(requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return false;
	}
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
	if(requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return false;
	}
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
	setErrorProperties();
	std::string address =  requestObj["address"].asString();

	if (request.isSubscription())
	{
		mGetPropertiesSubscriptions.subscribe(request);
		subscribed = true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto getPhoneBookPropertiesResultCallback = [this, address, adapterAddress, requestMessage, subscribed](BluetoothError error, const BluetoothPropertiesList &properties) {
		LS::Message request(requestMessage);
		bool success =false;
		if (error == BLUETOOTH_ERROR_NONE)
		{
			updateFromPbapProperties(properties);
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
	if( retrieveErrorCodeText(BLUETOOTH_ERROR_PBAP_CALL_SELECT_FOLDER_TYPE) == getFolder())
		return;
	pbnjson::JValue propertyObj = pbnjson::Object();
	propertyObj.put("repository",getFolder());
	propertyObj.put("databaseIdentifier", getDatabaseIdentifier());
	propertyObj.put("primaryVersionCounter", getPrimaryCounter());
	propertyObj.put("secondaryVersionCounter", getSecondaryCounter());
	propertyObj.put("fixedImageSize", getFixedImageSize());
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

void BluetoothPbapProfileService::notifySubscribersAboutPropertiesChange(const std::string &address)
{
	BT_INFO("PBAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", getManager()->getAddress());
	responseObj.put("address", address);
	appendCurrentProperties(responseObj);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	LSUtils::postToSubscriptionPoint(&mGetPropertiesSubscriptions, responseObj);
}

void BluetoothPbapProfileService::profilePropertiesChanged(BluetoothPropertiesList properties, const std::string &address)
{
	BT_DEBUG("Bluetooth PBAP properties have changed");
	if(updateFromPbapProperties(properties))
	{
		notifySubscribersAboutPropertiesChange(address);
	}
}

bool BluetoothPbapProfileService::updateFromPbapProperties(const BluetoothPropertiesList &properties)
{
	bool changed = false;
	BT_INFO("PBAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	for(auto prop : properties)
	{
		switch (prop.getType())
		{
		case BluetoothProperty::Type::FOLDER:
			mFolder = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("PBAP Current folder has changed to %s", mFolder.c_str());
			break;
		case BluetoothProperty::Type::PRIMARY_COUNTER:
			mPrimaryCounter = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("PBAP primary version counter has changed to %s", mPrimaryCounter.c_str());
			break;
		case BluetoothProperty::Type::SECONDERY_COUNTER:
			mSecondaryCounter = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("PBAP secondary version has changed to %s", mSecondaryCounter.c_str());
			break;
		case BluetoothProperty::Type::DATABASE_IDENTIFIER:
			mDatabaseIdentifier = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("PBAP persistent database identifier version has changed to %s", mDatabaseIdentifier.c_str());
			break;
		case BluetoothProperty::Type::FIXED_IMAGE_SIZE:
			mFixedImageSize = prop.getValue<bool>();
			changed = true;
			BT_DEBUG("PBAP support for fixed image size has changed to %d", mFixedImageSize);
			break;
		default:
			break;
		}
	}
	return changed;
}

void BluetoothPbapProfileService::setErrorProperties()
{
	mFolder = retrieveErrorCodeText(BLUETOOTH_ERROR_PBAP_CALL_SELECT_FOLDER_TYPE);
	mPrimaryCounter = "NULL";
	mSecondaryCounter = "NULL";
	mDatabaseIdentifier = "NULL";
	mFixedImageSize = false;
}
