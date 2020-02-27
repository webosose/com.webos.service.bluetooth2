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
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, awaitAccessRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, acceptAccessRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothPbapProfileService, rejectAccessRequest)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/pbap", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/pbap", this);
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
