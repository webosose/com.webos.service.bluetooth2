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


#include "bluetoothhfpprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "logging.h"
#include "ls2utils.h"
#include "bluetoothdevice.h"

#define RINGING_INTERVAL         3

BluetoothHfpProfileService::BluetoothHfpProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "HFP", "0000111f-0000-1000-8000-00805f9b34fb",
        "0000111e-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, openSCO)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, closeSCO)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, receiveAT)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, sendResult)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, indicateCall)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, sendAT)
		LS_CATEGORY_CLASS_METHOD(BluetoothHfpProfileService, receiveResult)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/hfp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/hfp", this);
}

BluetoothHfpProfileService::~BluetoothHfpProfileService()
{
	for (auto itMap = mIndicateCallWatches.begin(); itMap != mIndicateCallWatches.end(); itMap++)
	{
		LSUtils::ClientWatch *clientWatch = itMap->second.second.second;
		if (NULL == clientWatch)
			continue;

		delete clientWatch;
	}
	mIndicateCallWatches.clear();

	for (auto itMap = mOpenScoWatches.begin(); itMap != mOpenScoWatches.end(); itMap++)
	{
		LSUtils::ClientWatch *clientWatch = itMap->second;
		if (NULL == clientWatch)
			continue;

		delete clientWatch;
	}
	mOpenScoWatches.clear();

	for (auto itMap = mIndicateCallUserData.begin(); itMap != mIndicateCallUserData.end(); itMap++)
	{
		RingCallbackInfo *userData = itMap->second;
		if (NULL == userData)
			continue;

		delete userData;
	}
	mIndicateCallUserData.clear();
}

void BluetoothHfpProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothHfpProfile>()->registerObserver(this);
}

void BluetoothHfpProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothHfpProfile>(adapterAddress)->registerObserver(this);
}

void BluetoothHfpProfileService::scoStateChanged(const std::string &address, bool state)
{
	BT_INFO("HFP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	auto device = getManager()->findDevice(address);
	if (!device)
		return;

	if (!isDeviceConnected(address))
		return;

	auto deviceIterator = std::find(mOpenedScoDevices.begin(), mOpenedScoDevices.end(), address);

	if (state && deviceIterator == mOpenedScoDevices.end())
		mOpenedScoDevices.push_back(address);
	else if (!state && deviceIterator != mOpenedScoDevices.end())
		mOpenedScoDevices.erase(deviceIterator);
	else
		return;

	if (!state)
		removeOpenScoWatchForDevice(address, !state);

	notifyStatusSubscribers(getManager()->getAddress(), address, true);
}

std::string BluetoothHfpProfileService::typeToString(BluetoothHfpAtCommand::Type type) const
{
	switch(type)
	{
	case BluetoothHfpAtCommand::BASIC:
		return "basic";
	case BluetoothHfpAtCommand::ACTION:
		return "action";
	case BluetoothHfpAtCommand::READ:
		return "read";
	case BluetoothHfpAtCommand::SET:
		return "set";
	case BluetoothHfpAtCommand::TEST:
		return "test";
	default:
		return "unknown";
	}
}

BluetoothHfpAtCommand::Type BluetoothHfpProfileService::stringToType(const std::string &type) const
{
	if (type.compare("basic") == 0)
		return BluetoothHfpAtCommand::Type::BASIC;
	else if (type.compare("action") == 0)
		return BluetoothHfpAtCommand::Type::ACTION;
	else if (type.compare("read") == 0)
		return BluetoothHfpAtCommand::Type::READ;
	else if (type.compare("set") == 0)
		return BluetoothHfpAtCommand::Type::SET;
	else if (type.compare("test") == 0)
		return BluetoothHfpAtCommand::Type::TEST;
	else
		return BluetoothHfpAtCommand::Type::UNKNOWN;
}

void BluetoothHfpProfileService::atCommandReceived(const std::string &address, const BluetoothHfpAtCommand &atCommand)
{
	BT_INFO("HFP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	auto device = getManager()->findDevice(address);
	if (!device)
		return;

	if (!isDeviceConnected(address))
		return;

	notifyReceiveATSubscribers(address, address, atCommand);
}

void BluetoothHfpProfileService::resultCodeReceived(const std::string &address, const std::string &resultCode)
{
	BT_DEBUG("resultCodeReceived:: Addr = %s, resultCode = %s", address.c_str(), resultCode.c_str());
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("address", address);
	responseObj.put("resultCode", resultCode);
	responseObj.put("adapterAddress", getManager()->getAddress());
	notifyToSubscribers(address, mReceiveResultSubscriptions, responseObj);
}

void BluetoothHfpProfileService::notifyToSubscribers(const std::string &address, HfpServiceSubscriptions &subscriptions, pbnjson::JValue &responseObj)
{
	HfpServiceSubscriptions::iterator subscriptionIter[2];
	if (address.compare("") == 0)
		subscriptionIter[0] = subscriptions.end();
	else
		subscriptionIter[0] = subscriptions.find(address);
	subscriptionIter[1] = subscriptions.find("");

	for (auto i = 0; i < 2; i++)
	{
		if (subscriptionIter[i] == subscriptions.end())
			continue;

		LS::SubscriptionPoint *subscriptionPoint = subscriptionIter[i]->second;
		if (nullptr == subscriptionPoint)
			continue;

		LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
	}
}

void BluetoothHfpProfileService::notifyReceiveATSubscribers(const std::string &key, const std::string &address, const BluetoothHfpAtCommand &atCommand)
{
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("address", address);
	responseObj.put("adapterAddress", getManager()->getAddress());

	if (BluetoothHfpAtCommand::Type::UNKNOWN != atCommand.getType())
		responseObj.put("type", typeToString(atCommand.getType()).c_str());

	if (!atCommand.getCommand().empty())
		responseObj.put("command", atCommand.getCommand().c_str());

	if (!atCommand.getArguments().empty())
		responseObj.put("arguments", atCommand.getArguments().c_str());

	notifyToSubscribers(key, mReceiveAtSubscriptions, responseObj);
}

void BluetoothHfpProfileService::addSubscription(const std::string &deviceAddress, LS::Message &request, HfpServiceSubscriptions &subscriptions)
{
	LS::SubscriptionPoint* subscriptionPoint = 0;

	auto subscriptionIter = subscriptions.find(deviceAddress);
	if (subscriptionIter == subscriptions.end())
	{
		subscriptionPoint = new LS::SubscriptionPoint;
		subscriptionPoint->setServiceHandle(getManager());
		subscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(deviceAddress, subscriptionPoint));
	}
	else
	{
		subscriptionPoint = subscriptionIter->second;
	}
	subscriptionPoint->subscribe(request);
}

pbnjson::JValue BluetoothHfpProfileService::buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue, std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	appendCommonProfileStatus(responseObj, connected, connecting, subscribed, returnValue, adapterAddress, deviceAddress);

	auto deviceIterator = std::find(mOpenedScoDevices.begin(), mOpenedScoDevices.end(), deviceAddress);

	responseObj.put("sco", (deviceIterator != mOpenedScoDevices.end()) ? true : false);

	return responseObj;
}

/**
Open a SCO connection to broadcast and listen to audio to/from the remote devices it is connected to.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | Address of the remote device
subscribe | No | Boolean | To subscribe to be notified that the SCO connection is closed
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the SCO connection was successfully opened; false otherwise.
subscribed | Yes | Boolean | Value is false if the caller does not subscribe this method.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the SCO connection was successfully opened; false otherwise.
subscribed | Yes | Boolean | Value is false if the caller does not subscribe this method.
disconnectByRemote | Yes | Boolean | If the remote device initiated the SCO disconnect, disconnectByRemote will contain true. If the local user initiated the SCO disconnect, disconnectByRemote will contain false.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.
 */
bool BluetoothHfpProfileService::openSCO(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string),
                                                      PROP_WITH_VAL_1(subscribe, boolean, true))
                                                      REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();
	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (!isDeviceConnected(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto openScoCallback = [this, requestMessage, adapterAddress, deviceAddress](BluetoothError error) {
		LS::Message request(requestMessage);
		bool subscribed = false;

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_HFP_OPEN_SCO_FAILED);
			LSMessageUnref(request.get());
			return;
		}

		if (request.isSubscription())
		{
			auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
			                    std::bind(&BluetoothHfpProfileService::handleOpenScoClientDisappeared, this, adapterAddress, deviceAddress));

			mOpenScoWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(deviceAddress, watch));
			subscribed = true;
		}

		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("subscribed", subscribed);
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);

		LSUtils::postToClient(request, responseObj);

		LSMessageUnref(request.get());

	};

	getImpl<BluetoothHfpProfile>()->openSCO(deviceAddress, openScoCallback);

	return true;
}

void BluetoothHfpProfileService::handleOpenScoClientDisappeared(const std::string &adapterAddress, const std::string &address)
{
	auto watchIter = mOpenScoWatches.find(address);
	if (watchIter == mOpenScoWatches.end())
		return;

	if (!getImpl<BluetoothHfpProfile>())
		return;

	auto closeScoCallback = [this, address, adapterAddress](BluetoothError error) {
		removeOpenScoWatchForDevice(address, true, false);
	};

	getImpl<BluetoothHfpProfile>()->closeSCO(address, closeScoCallback);
}

void BluetoothHfpProfileService::removeOpenScoWatchForDevice(const std::string &address, bool disconnected, bool remoteDisconnect)
{
	auto watchIter = mOpenScoWatches.find(address);
	if (watchIter == mOpenScoWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", !disconnected);
	if (disconnected)
	{
		if (remoteDisconnect)
			responseObj.put("disconnectByRemote", true);
		else
			responseObj.put("disconnectByRemote", false);
	}
	responseObj.put("adapterAddress", getManager()->getAddress());

	LSUtils::postToClient(watch->getMessage(), responseObj);

	mOpenScoWatches.erase(watchIter);
	delete watch;
}

/**
Close the SCO connection to a remote device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | Address of the remote device
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the SCO connection was successfully closed; false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Not applicable
*/
bool BluetoothHfpProfileService::closeSCO(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string))  REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();
	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (!isDeviceConnected(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto closeScoCallback = [this, requestMessage, adapterAddress, deviceAddress](BluetoothError error) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_HFP_CLOSE_SCO_FAILED);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		LSUtils::postToClient(request, responseObj);

		removeOpenScoWatchForDevice(deviceAddress, true, false);
	};

	getImpl<BluetoothHfpProfile>()->closeSCO(deviceAddress, closeScoCallback);

	return true;
}

/**
Receive AT command from the connected HF device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
subscribe | Yes | Boolean | To subscribe to read AT commands to be received. Must set subscribe to true.
address | No | String | Address of the remote device
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if an AT command was successfully received; false otherwise.
subscribed | Yes | Boolean | If the subscription request is successful, this will be true until the final response sent by the service before stopping.
adapterAddress | Yes | String | Address of the adapter executing this method
type | No | String | Should be one of basic, action, read, set, and test
command | No | String | AT command without 'AT' string
arguments | No | String | Arguments of the AT command
address | No | String | Address of the remote device
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

As for a successful call
 */
bool BluetoothHfpProfileService::receiveAT(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string),
                                              PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string))
                                              REQUIRED_1(subscribe));

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

	std::string deviceAddress = "";
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		auto device = getManager()->findDevice(deviceAddress);
		if (!device)
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!device->hasConnectedRole(BluetoothDeviceRole::BLUETOOTH_DEVICE_ROLE_HFP_AG))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
			return true;
		}

		/*
		 * Testing with sample application provided from LGSI doesn't
		 * work with checking if device is connected or not before calling
		 * receiveAT. In a general consideration, it seems like receiveAT
		 * should not be dependent with connection status since received
		 * AT command could not be handled properly before HFP connection
		 * is established.
		 * We will do further check if this check routine is really required or
		 * not. For now, we will comment this for the functionality between wbs
		 * and HFP sample application.
		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
		*/
	}

	if (request.isSubscription())
		addSubscription(deviceAddress, request, mReceiveAtSubscriptions);

	return true;
}

/**
Send result code to a remote HF device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | Address of the remote device
resultCode | Yes | String | Unsolicited result code to send
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the result code was successfully transferred; false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Not applicable
 */
bool BluetoothHfpProfileService::sendResult(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string),
                                                 PROP(resultCode, string)) REQUIRED_2(address, resultCode));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("resultCode"))
			LSUtils::respondWithError(request, BT_ERR_HFP_RESULT_CODE_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string deviceAddress;

	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}

		auto device = getManager()->findDevice(deviceAddress);
		if (!device)
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!device->hasConnectedRole(BluetoothDeviceRole::BLUETOOTH_DEVICE_ROLE_HFP_AG))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
			return true;
		}
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string resultCode;

	if (requestObj.hasKey("resultCode"))
	{
		resultCode = requestObj["resultCode"].asString();
	}

	BluetoothError error = getImpl<BluetoothHfpProfile>()->sendResultCode(deviceAddress, resultCode);
	if (BLUETOOTH_ERROR_NONE != error)
	{
		if (BLUETOOTH_ERROR_NOT_ALLOWED == error)
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
		else
			LSUtils::respondWithError(request, BT_ERR_HFP_WRITE_RESULT_CODE_FAILED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

/**
Indicate an incoming call to the connected HF device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | Address of the remote device
subscribe | Yes | Boolean | Must set subscribe to true. The caller must subscribe to this method.
number | No | String | Phone number of incoming call
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the ringing was successfully accomplished; false otherwise.
subscribed | Yes | Boolean | If the subscription request is successful, this will be true until the final ringing sent by the service.
adapterAddress | Yes | String | Address of the adapter executing this method
address | Yes | String | Address of the remote device
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

As for a successful call
 */
bool BluetoothHfpProfileService::indicateCall(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP_WITH_VAL_1(subscribe, boolean, true),
                                              PROP(number, string), PROP(adapterAddress, string))
                                              REQUIRED_2(address, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string deviceAddress = "";
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}

		auto device = getManager()->findDevice(deviceAddress);
		if (!device)
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!device->hasConnectedRole(BluetoothDeviceRole::BLUETOOTH_DEVICE_ROLE_HFP_HF))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
			return true;
		}
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string phoneNumber = "";
	if (requestObj.hasKey("number"))
		phoneNumber = requestObj["number"].asString();

	auto indicateCallIter = mIndicateCallWatches.find(deviceAddress);
	if (mIndicateCallWatches.end() == indicateCallIter)
	{
		LSUtils::ClientWatch *indicateCallWatch = new LSUtils::ClientWatch(getManager()->get(), &message,
		        [this, deviceAddress]() {
			stopRinging(deviceAddress);
		});
		mIndicateCallWatches.insert(std::pair<std::string, std::pair<std::string, std::pair<guint, LSUtils::ClientWatch *>>>(deviceAddress,
                        std::make_pair(phoneNumber, std::make_pair(0, indicateCallWatch))));
	}
	else
	{
		LSUtils::respondWithError(request, BT_ERR_HFP_ALLOW_ONE_SUBSCRIBE_PER_DEVICE);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);

	this->startRinging(deviceAddress, phoneNumber);

	return true;
}

/**
Send AT command to a remote AG device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | Address of the remote device
adapterAddress | No | String | Address of the local device
type | Yes | String | Should be one of basic, action, read, set and test
command | Yes | String | AT command without 'AT' string
arguments | No | String | Arguments of the AT command

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the AT command was successfully transferred; false otherwise
adapterAddress | Yes | String | THe address of the adapter executing this method call.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

As for a successful call
 */
bool BluetoothHfpProfileService::sendAT(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress, string),
                                                 PROP(type, string), PROP(command, string), PROP(arguments, string))
                                                REQUIRED_3(address, type, command));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("command"))
			LSUtils::respondWithError(request, BT_ERR_HFP_ATCMD_MISSING);
		else if (!requestObj.hasKey("type"))
			LSUtils::respondWithError(request, BT_ERR_HFP_TYPE_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string deviceAddress = requestObj["address"].asString();
	std::string atCommand = requestObj["command"].asString();
	std::string type = requestObj["type"].asString();

	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}
	if (!isDeviceConnected(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	auto device = getManager()->findDevice(deviceAddress);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (!device->hasConnectedRole(BluetoothDeviceRole::BLUETOOTH_DEVICE_ROLE_HFP_HF))
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string arguments = "";
	if (requestObj.hasKey("arguments"))
		arguments = requestObj["arguments"].asString();

	BluetoothHfpAtCommand localAtCommand;
	localAtCommand.setType(stringToType(type));
	localAtCommand.setArguments(arguments);
	if (localAtCommand.getType() == BluetoothHfpAtCommand::Type::BASIC)
		localAtCommand.setCommand("AT" + atCommand);
	else
		localAtCommand.setCommand("AT+" + atCommand);

	BluetoothError error = getImpl<BluetoothHfpProfile>()->sendAtCommand(deviceAddress, localAtCommand);
	if (BLUETOOTH_ERROR_NONE != error)
	{
		if (BLUETOOTH_ERROR_NOT_ALLOWED == error)
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
		else
			LSUtils::respondWithError(request, BT_ERR_HFP_SEND_AT_FAIL);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	LSUtils::postToClient(request, responseObj);

	return true;
}

/**
Deliver a AT command to HFP service

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
subscribe | Yes | Boolean | To subscribe to be notified when the HF receives the resultcode.
address | No | String | Address of the remote device
adapterAddress | No | String | Address of the local device

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
address | Yes | String | The address of the remote device
adapterAddress | Yes | String | The address of the adapter executing this method call.
returnValue | Yes | Boolean | Value is true if the result code was successfully returned; false otherwise.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)
Name | Required | Type | Description
-----|--------|------|----------
resultCode | Yes | String | Result code received from AG
address | Yes | String | The address of the remote device
adapterAddress | Yes | String | The address of the adapter executing this method call.
returnValue | Yes | Boolean | Value is true if the result code was successfully returned; false otherwise.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.
 */
bool BluetoothHfpProfileService::receiveResult(LSMessage &message)
{
	BT_INFO("HFP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHfpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

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

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = "";
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		auto device = getManager()->findDevice(deviceAddress);
		if (!device)
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!device->hasConnectedRole(BluetoothDeviceRole::BLUETOOTH_DEVICE_ROLE_HFP_HF))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
			return true;
		}
	}

	if (request.isSubscription())
	{
		addSubscription(deviceAddress, request, mReceiveResultSubscriptions);
		BT_DEBUG("%s: Register subscription", __FUNCTION__);
	}

	return true;
}

void BluetoothHfpProfileService::startRinging(const std::string &address, const std::string &phoneNumber)
{
	auto ringCallback = [] (gpointer userdata) -> gboolean {

		RingCallbackInfo *callbackInfo = static_cast<RingCallbackInfo *>(userdata);
		if (NULL == callbackInfo)
			return FALSE;

		std::string address = callbackInfo->address;
		std::string phoneNumber = callbackInfo->phoneNumber;
		BluetoothHfpProfileService *service = callbackInfo->service;

		auto indicateCallIter = service->mIndicateCallWatches.find(address);
		if (indicateCallIter != service->mIndicateCallWatches.end())
		{
			service->sendRingResultCode(address);
			if (phoneNumber != "")
				service->sendCLIPResultCode(address);
			return TRUE;
		}

		return FALSE;
	};

	sendRingResultCode(address);
	if (phoneNumber != "")
		sendCLIPResultCode(address);

	auto indicateCallIter = mIndicateCallWatches.find(address);
	if (indicateCallIter != mIndicateCallWatches.end())
	{
		RingCallbackInfo *callbackInfo = new RingCallbackInfo();
		callbackInfo->service = this;
		callbackInfo->address = address;
		callbackInfo->phoneNumber = phoneNumber;

		mIndicateCallUserData.insert(std::pair<std::string, RingCallbackInfo *>(address, callbackInfo));

		indicateCallIter->second.second.first = g_timeout_add_seconds(RINGING_INTERVAL, ringCallback, callbackInfo);
	}
 }

void BluetoothHfpProfileService::stopRinging(const std::string &address)
{
	auto indicateCallIter = mIndicateCallWatches.find(address);
	if (indicateCallIter != mIndicateCallWatches.end())
	{
		if (indicateCallIter->second.second.first > 0)
			g_source_remove(indicateCallIter->second.second.first);

		delete indicateCallIter->second.second.second;
		mIndicateCallWatches.erase(indicateCallIter);

		auto indicateCallUserData = mIndicateCallUserData.find(address);
		if (indicateCallUserData != mIndicateCallUserData.end())
		{
			delete indicateCallUserData->second;
			mIndicateCallUserData.erase(indicateCallUserData);
		}
	}
}

void BluetoothHfpProfileService::sendRingResultCode(const std::string &address)
{
	auto indicateCallIter = mIndicateCallWatches.find(address);
	if (indicateCallIter == mIndicateCallWatches.end())
		return;

	LSUtils::ClientWatch *indicateCallWatch = indicateCallIter->second.second.second;
	if (NULL == indicateCallWatch)
		return;

	BluetoothError error = getImpl<BluetoothHfpProfile>()->sendResultCode(address, "RING");
	if (BLUETOOTH_ERROR_NONE != error)
	{
		if (BLUETOOTH_ERROR_NOT_ALLOWED == error)
			LSUtils::respondWithError(indicateCallWatch->getMessage(), BLUETOOTH_ERROR_NOT_ALLOWED);
		else
			LSUtils::respondWithError(indicateCallWatch->getMessage(), BT_ERR_HFP_WRITE_RING_RESULT_CODE_FAILED);
		stopRinging(address);
	}
}

void BluetoothHfpProfileService::sendCLIPResultCode(const std::string &address)
{
	auto indicateCallIter = mIndicateCallWatches.find(address);
	if (indicateCallIter == mIndicateCallWatches.end())
		return;

	LSUtils::ClientWatch *indicateCallWatch = indicateCallIter->second.second.second;
	if (NULL == indicateCallWatch)
		return;
	std::string phoneNumber = indicateCallIter->second.first;

	BluetoothError error = getImpl<BluetoothHfpProfile>()->sendResultCode(address, "+CLIP:" + phoneNumber);
	if (BLUETOOTH_ERROR_NONE != error)
	{
		if (BLUETOOTH_ERROR_NOT_ALLOWED == error)
			LSUtils::respondWithError(indicateCallWatch->getMessage(), BLUETOOTH_ERROR_NOT_ALLOWED);
		else
			LSUtils::respondWithError(indicateCallWatch->getMessage(), BT_ERR_HFP_WRITE_RING_RESULT_CODE_FAILED);
		stopRinging(address);
	}
}
