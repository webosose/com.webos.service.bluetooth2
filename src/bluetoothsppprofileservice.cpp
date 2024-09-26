// Copyright (c) 2015-2024 LG Electronics, Inc.
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


#include "bluetoothsppprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "logging.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "utils.h"

using namespace std::placeholders;

BluetoothSppProfileService::BluetoothSppProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "SPP", "00001101-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothSppProfileService, createChannel)
		LS_CATEGORY_CLASS_METHOD(BluetoothSppProfileService, writeData)
		LS_CATEGORY_CLASS_METHOD(BluetoothSppProfileService, readData)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/spp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/spp", this);
}

BluetoothSppProfileService::~BluetoothSppProfileService()
{
}

void BluetoothSppProfileService::initialize()
{
	if (mImpl)
		return;

	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothSppProfile>()->registerObserver(this);
}

void BluetoothSppProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	createChannelManager(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothSppProfile>(adapterAddress)->registerObserver(this);
}

void BluetoothSppProfileService::reset(const std::string &adapterAddress)
{
	BluetoothProfileService::reset(adapterAddress);

	auto implIter = mChannelImpls.find(adapterAddress);
	if (implIter == mChannelImpls.end())
		return;

	mChannelImpls.erase(implIter);
}

void BluetoothSppProfileService::createChannelManager(const std::string &adapterAddress)
{
	auto iter = mChannelImpls.find(adapterAddress);

	if (iter != mChannelImpls.end())
		return;

	ChannelManager *channelmanager = new ChannelManager();

	mChannelImpls.insert(std::pair<std::string,ChannelManager*>(adapterAddress, channelmanager));
}

ChannelManager* BluetoothSppProfileService::findChannelImpl (const std::string &adapterAddress)
{
	std::string convertedAddress = convertToLower(adapterAddress);
	auto implIter = mChannelImpls.find(convertedAddress);
	if (implIter == mChannelImpls.end())
	{
		convertedAddress = convertToUpper(adapterAddress);
		implIter = mChannelImpls.find(convertedAddress);
		if (implIter == mChannelImpls.end())
			return nullptr;
	}

	return implIter->second;
}

void BluetoothSppProfileService::handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &address,
        const BluetoothSppChannelId channelId)
{

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	if (!channelManager->isChannelConnected(channelId))
		return;

	std::string userChannelId = channelManager->getUserChannelId(channelId);
	auto watchIter = mConnectWatches.find(userChannelId);
	if (watchIter == mConnectWatches.end())
		return;

	if (!mImpl)
		return;

	auto disconnectCallback = [this, channelId, adapterAddress, address, channelManager](BluetoothError error) {
		if (!channelManager->isChannelConnected(address))
			markDeviceAsNotConnected(adapterAddress, address);
	};

	getImpl<BluetoothSppProfile>(adapterAddress)->disconnectUuid(channelId, disconnectCallback);
}

void BluetoothSppProfileService::connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	std::string address = convertToLower(requestObj["address"].asString());
	std::string uuid = convertToLower(requestObj["uuid"].asString());

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	if (channelManager->isChannelConnecting(uuid))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_CONNECTING);
		return;
	}

	if (channelManager->getMessageOwner(request.get()).compare("") == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_APPID_PARAM_MISSING, true);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto isConnectedCallback = [this, requestMessage, adapterAddress, address, uuid, channelManager](const BluetoothError error, const bool state) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		if (state)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECTED);
			LSMessageUnref(request.get());
			return;
		}

		channelManager->markChannelAsConnecting(uuid);
		notifyStatusSubscribers(adapterAddress, address, uuid, channelManager->isChannelConnected(address));

		auto connectCallback = [this, requestMessage, adapterAddress, address, uuid, channelManager](const BluetoothError error, const BluetoothSppChannelId channelId) {
			LS::Message request(requestMessage);
			bool subscribed = false;

			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
				LSMessageUnref(request.get());

				channelManager->markChannelAsNotConnecting(uuid);
				notifyStatusSubscribers(adapterAddress, address, uuid, channelManager->isChannelConnected(address));

				return;
			}

			// NOTE: At this point we're successfully connected but we will notify
			// possible subscribers once we get the update from the SIL through the
			// observer that the device is connected.

			// If we have a subscription we need to register a watch with the device
			// and update the client once the connection with the remote device is
			// dropped.
			//Connect indication is already coming from SIL in channelStateChanged callback and  markChannelAsConnected already done.
			std::string userChannelId = channelManager->getUserChannelId(channelId);
			channelManager->setChannelAppName(userChannelId, channelManager->getMessageOwner(requestMessage));
			markDeviceAsConnected(adapterAddress, address);
			if (request.isSubscription())
			{
				auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
				                    std::bind(&BluetoothSppProfileService::handleConnectClientDisappeared, this, adapterAddress, address, channelId));

				mConnectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(userChannelId, watch));
				subscribed = true;
			}

			pbnjson::JValue responseObj = pbnjson::Object();

			if (subscribed)
				responseObj.put("subscribed", subscribed);

			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("address", address);
			responseObj.put("channelId", userChannelId);

			LSUtils::postToClient(request, responseObj);

			// We're done with sending out the first response to the client so
			// no use anymore for the message object
			LSMessageUnref(request.get());
		};

		getImpl<BluetoothSppProfile>(adapterAddress)->connectUuid(address, uuid, connectCallback);
	};

	// Before we start to connect with the device we have to make sure
	// we're not already connected with it.
	getImpl<BluetoothSppProfile>(adapterAddress)->getChannelState(address, uuid, isConnectedCallback);
}

bool BluetoothSppProfileService::isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(uuid, string),
	        PROP(adapterAddress, string), PROP(subscribe, boolean)) REQUIRED_2(address, uuid));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("uuid"))
			LSUtils::respondWithError(request, BT_ERR_SPP_UUID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	return true;
}

bool BluetoothSppProfileService::isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(channelId, string), PROP(adapterAddress, string))  REQUIRED_1(channelId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("channelId"))
			LSUtils::respondWithError(request, BT_ERR_SPP_CHANNELID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	return true;
}

void BluetoothSppProfileService::disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	std::string channelId = requestObj["channelId"].asString();

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	BluetoothSppChannelId stackChannelId = channelManager->getStackChannelId(channelId);
	if (!channelManager->isChannelConnected(stackChannelId))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto disconnectCallback = [this, requestMessage, adapterAddress, channelId, stackChannelId](BluetoothError error) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_DISCONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());

		removeConnectWatchForDevice(channelId, true, false);
	};

	getImpl<BluetoothSppProfile>(adapterAddress)->disconnectUuid(stackChannelId, disconnectCallback);
}

void BluetoothSppProfileService::notifyStatusSubscribers(const std::string &adapterAddress, const std::string &address,
        const std::string &uuid, bool connected)
{
	auto subscriptionIter = mGetStatusSubscriptions.find(address);
	if (subscriptionIter == mGetStatusSubscriptions.end())
		return;

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	pbnjson::JValue responseObj = buildGetStatusResp(connected, channelManager->isChannelConnecting(uuid), true,
	                                                 true, adapterAddress, address);

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;
	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

/**
Register a service record in the device service record database with the specified UUID and name.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
name | Yes | String | An identifiable name of a SPP service in the server
uuid | Yes | String | UUID used by the server application
subscribe | Yes | Boolean | Must be set to true to be informed of changes to the channel (connection of client, removal of the channel)
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the channel was successfully created, false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
subscribed | Yes | Boolean | If the subscription request succeeds, subscribed will contain true while the channel is alive. If the channel is removed, subscribed will contain false.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the channel was successfully created; false otherwise.
subscribed | Yes | Boolean | If the subscription request succeeds, subscribed will contain true while the channel is alive. If the channel is removed, subscribed will contain false.
adapterAddress | Yes | String | Address of the adapter executing this method
channelId | Yes | String | Unique ID of a SPP channel, which has the following format: "nnn", 3 digit number increasing sequentially.
connecting | Yes | Boolean | Value becomes true after a connection request has created and becomes false after the stack has finishing processing the connection request.
connected | Yes | Boolean | Value is true if the connection is open; false otherwise.
address | No | String | Address of the device
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.
 */
bool BluetoothSppProfileService::createChannel(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(name, string), PROP(uuid, string),
	        PROP(adapterAddress, string), PROP_WITH_VAL_1(subscribe, boolean, true)) REQUIRED_3(name, uuid, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("name"))
			LSUtils::respondWithError(request, BT_ERR_SPP_NAME_PARAM_MISSING, true);
		else if (!requestObj.hasKey("uuid"))
			LSUtils::respondWithError(request, BT_ERR_SPP_UUID_PARAM_MISSING, true);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED, true);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL, true);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothSppProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL, true);
		return true;
	}

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return true;

	if (channelManager->getMessageOwner(request.get()).compare("") == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_APPID_PARAM_MISSING, true);
		return true;
	}

	std::string name = requestObj["name"].asString();
	std::string uuid = requestObj["uuid"].asString();

	BluetoothError error = getImpl<BluetoothSppProfile>(adapterAddress)->createChannel(name, uuid);
	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_CREATE_CHANNEL_FAILED, true);
		return true;
	}

	if (request.isSubscription())
	{
		auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
		        std::bind(&BluetoothSppProfileService::removeChannel, this, uuid, adapterAddress));
		channelManager->addCreateChannelSubscripton(uuid, watch, request.get());
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

void BluetoothSppProfileService::removeChannel(const std::string &uuid, const std::string &adapterAddress)
{
	getImpl<BluetoothSppProfile>(adapterAddress)->removeChannel(uuid);

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	channelManager->deleteCreateChannelSubscription(uuid);
}

void BluetoothSppProfileService::notifyCreateChannelSubscribers(const std::string &adapterAddress, const std::string &address,
        const std::string &uuid, const std::string &channelId, bool connected)
{
	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	LSUtils::ClientWatch *watch = channelManager->getCreateChannelSubscription(uuid);
	if (NULL == watch)
		return;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("connected", connected);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("channelId", channelId);

	LSUtils::postToClient(watch->getMessage(), responseObj);
}

/**
Transfer data to the connected remote device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
channelId | Yes | String | Unique ID of a SPP channel
data | Yes | Number array | Data to send
adapterAddress | No | String | Address of the adapter executing this method. If not specified, the default adapter will be used.

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the data was successfully written, false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Not applicable
*/
bool BluetoothSppProfileService::writeData(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(channelId, string), PROP(data, string),
	        PROP(adapterAddress, string)) REQUIRED_2(channelId, data));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("channelId"))
			LSUtils::respondWithError(request, BT_ERR_SPP_CHANNELID_PARAM_MISSING);
		else if (!requestObj.hasKey("data"))
			LSUtils::respondWithError(request, BT_ERR_SPP_DATA_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothSppProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return true;

	if (channelManager->getMessageOwner(request.get()).compare("") == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_APPID_PARAM_MISSING, true);
		return true;
	}

	std::string channelId = requestObj["channelId"].asString();
	BluetoothSppChannelId stackChannelId = channelManager->getStackChannelId(channelId);
	if (BLUETOOTH_SPP_CHANNEL_ID_INVALID == stackChannelId)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_CHANNELID_NOT_AVAILABLE);
		return true;
	}

	std::string appName = channelManager->getMessageOwner(request.get());
	std::string channelAppName = channelManager->getChannelAppName(channelId);
	if (channelAppName != appName)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_PERMISSION_DENIED);
		return true;
	}

	std::string data = requestObj["data"].asString();
	gsize outLen = 0;
	guchar *gdata = g_base64_decode(data.c_str(), &outLen);
	if (NULL == gdata)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_WRITE_DATA_FAILED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto writeDataCallback = [this, requestMessage, adapterAddress](BluetoothError error) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_SPP_WRITE_DATA_FAILED);
			LSMessageUnref(request.get());
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};
	getImpl<BluetoothSppProfile>(adapterAddress)->writeData(stackChannelId, gdata, outLen, writeDataCallback);
	g_free(gdata);

	return true;
}

/**
Receive data from the connected remote device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
channelId | No | String | Unique ID of a SPP channel
subscribe | No | Boolean | If subscribe is not true, this method will be finished after receiving the return.
timeout | No | Number | The receiving timeout in seconds. This value is only available when subscribe is true. After timeout seconds, the subscription is canceled. A value of 0 means that the timeout is disabled.
adapterAddress | No | String | Address of the adapter executing this method. If not specified, the default adapter will be used.

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the data was successfully read, false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
subscribed | Yes | Boolean | Value is false if the caller does not subscribe this method.
channelId | Yes | String | Unique ID of a SPP channel
data | No | Number array | The received data from the remote device
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

As for a successful call
 */
bool BluetoothSppProfileService::readData(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(channelId, string), PROP(subscribe, boolean),
	        PROP(timeout, integer), PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON, true);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL, true);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothSppProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL, true);
		return true;
	}

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return true;

	if (channelManager->getMessageOwner(request.get()).compare("") == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_APPID_PARAM_MISSING, true);
		return true;
	}
	std::string channelId = EMPTY_STRING;
	std::string appName = channelManager->getMessageOwner(request.get());
	if (requestObj.hasKey("channelId"))
	{
		channelId = requestObj["channelId"].asString();
		BluetoothSppChannelId stackChannelId = channelManager->getStackChannelId(channelId);
		if (BLUETOOTH_SPP_CHANNEL_ID_INVALID == stackChannelId)
		{
			LSUtils::respondWithError(request, BT_ERR_SPP_CHANNELID_NOT_AVAILABLE, true);
			return true;
		}

		std::string channelAppName = channelManager->getChannelAppName(channelId);
		if (channelAppName != appName)
		{
			LSUtils::respondWithError(request, BT_ERR_SPP_PERMISSION_DENIED, true);
			return true;
		}
	}

	int timeout = 0;
	bool subscribed = false;
	if (requestObj.hasKey("timeout"))
		timeout = requestObj["timeout"].asNumber<int32_t>();
	if (requestObj.hasKey("subscribe"))
		subscribed = requestObj["subscribe"].asBool();
	if (timeout < 0)
	{
		LSUtils::respondWithError(request, BT_ERR_SPP_TIMEOUT_NOT_AVAILABLE, true);
		return true;
	}

	if (subscribed)
		addReadDataSubscription(request, channelManager, channelId, timeout);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", subscribed);
	responseObj.put("channelId", channelId);

	int size = 0;
	gchar *gdata = NULL;
	const ChannelManager::DataBuffer *dataBuffer = channelManager->getChannelBufferData(channelId, appName);
	if (dataBuffer)
	{
		gdata = g_base64_encode(dataBuffer->buffer, dataBuffer->size);
		if (gdata)
		{
			size = strlen(gdata);
			responseObj.put("channelId", channelId);
			responseObj.put("data", gdata);
		}

		delete dataBuffer;
	}

	if (subscribed)
	{
		if (size > 0)
		{
			responseObj.put("returnValue", true);
			LSUtils::postToClient(request, responseObj);
		}
	}
	else
	{
		if (size > 0)
			responseObj.put("returnValue", true);
		else
			responseObj.put("returnValue", false);
		LSUtils::postToClient(request, responseObj);
	}

	g_free(gdata);

	return true;
}

void BluetoothSppProfileService::addReadDataSubscription(LS::Message &request, ChannelManager *channelManager, const std::string channelId, const int timeout)
{
	LSUtils::ClientWatch *watch = new LSUtils::ClientWatch(getManager()->get(), request.get(), NULL);
	std::string appName = channelManager->getMessageOwner(request.get());
	void *readDataInfo = channelManager->addReadDataSubscription(channelId, timeout, watch, appName);

	auto func = std::bind(&ChannelManager::deleteReadDataSubscription, channelManager, readDataInfo);
	watch->setCallback(func);
}

void BluetoothSppProfileService::channelStateChanged(const std::string &adapterAddress, const std::string &address, const std::string &uuid,
        const BluetoothSppChannelId channelId, const bool state)
{
	// If the callers(i.e. com.lge.service.watchmanager and com.lge.service.mashupmanager) should use the
        // binary socket to send/receive the data, WBS handles the binary socket.

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	std::string userChannelId = EMPTY_STRING;
	if (state)
	{
		userChannelId = channelManager->markChannelAsConnected(channelId, address, uuid);
		if (isCallerUsingBinarySocket(channelManager, userChannelId))
			enableBinarySocket(adapterAddress, userChannelId);

		markDeviceAsConnected(adapterAddress, address);
	}
	else
	{
		userChannelId = channelManager->getUserChannelId(channelId);
		if (isCallerUsingBinarySocket(channelManager, userChannelId))
			disableBinarySocket(userChannelId);

		removeConnectWatchForDevice(userChannelId, true);
		channelManager->markChannelAsNotConnected(channelId, getManager()->getAddress());
		if (!channelManager->isChannelConnected(address))
			markDeviceAsNotConnected(adapterAddress, address);
	}

	notifyCreateChannelSubscribers(adapterAddress, address, uuid, userChannelId, state);
	notifyStatusSubscribers(adapterAddress, address, uuid, channelManager->isChannelConnected(address));
}

void BluetoothSppProfileService::dataReceived(const BluetoothSppChannelId channelId, const std::string &adapterAddress, const uint8_t *data, const uint32_t size)
{
	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	// If caller used the binary socket, WBS does not support Luna APIs to read the data.
	// After receiving the data from the stack, it will be sent to the binary socket directly.
	std::string userChannelId = channelManager->getUserChannelId(channelId);
	if (isCallerUsingBinarySocket(channelManager, userChannelId))
	{
		auto binarySocket = findBinarySocket(userChannelId);
		if (binarySocket)
			binarySocket->sendData(data, size);
	}
	else
		channelManager->addReceiveQueue(adapterAddress, channelId, data, size);
}

BluetoothBinarySocket* BluetoothSppProfileService::findBinarySocket(const std::string &channelId) const
{
	auto binarySocketIter = mBinarySockets.find(channelId);
	if (binarySocketIter == mBinarySockets.end())
		return 0;

	return binarySocketIter->second;
}

void BluetoothSppProfileService::enableBinarySocket(const std::string &adapterAddress, const std::string &channelId)
{
	bool created = false;
	BluetoothBinarySocket *binarySocket = new BluetoothBinarySocket();
	if (binarySocket)
		created = binarySocket->createBinarySocket(channelId);

	if (created)
	{
		binarySocket->registerReceiveDataWatch(std::bind(&BluetoothSppProfileService::handleBinarySocketRecieveRequest,
												this, channelId, adapterAddress, _1, _2));
		mBinarySockets.insert(std::pair<std::string, BluetoothBinarySocket*>(channelId, binarySocket));
	}
	else
        {
                binarySocket->removeBinarySocket();
                delete binarySocket;
        }
}

void BluetoothSppProfileService::disableBinarySocket(const std::string &channelId)
{
	auto binarySocketIter = mBinarySockets.find(channelId);
	if (binarySocketIter == mBinarySockets.end())
		return;

	BluetoothBinarySocket *binarySocket = binarySocketIter->second;
	binarySocket->removeBinarySocket();

	mBinarySockets.erase(binarySocketIter);
	delete binarySocket;
}

bool BluetoothSppProfileService::isCallerUsingBinarySocket(ChannelManager *channelManager, const std::string &channelId)
{
	// Supports the binary socket to com.lge.watchmanager and com.lge.service.mashupmanager
	std::string callerName = channelManager->getChannelAppName(channelId);
	if (callerName.compare("com.lge.watchmanager") == 0 ||
			callerName.compare("com.lge.service.mashupmanager") == 0)
		return true;

	return false;
}

void BluetoothSppProfileService::handleBinarySocketRecieveRequest(const std::string &channelId, const std::string &adapterAddress, guchar *readBuf, gsize readLen)
{
	auto binarySocket = findBinarySocket(channelId);
	if (binarySocket)
		sendDataToStack(channelId, adapterAddress, readBuf, readLen);
}

void BluetoothSppProfileService::sendDataToStack(const std::string &channelId, const std::string &adapterAddress, guchar *data, gsize outLen)
{
	auto binarySocket = findBinarySocket(channelId);
	if (!binarySocket)
		return;

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return;

	BluetoothSppChannelId stackChannelId = channelManager->getStackChannelId(channelId);
	if (BLUETOOTH_SPP_CHANNEL_ID_INVALID == stackChannelId)
	{
		BT_DEBUG("stackChannelId is invalid");
		return;
	}

	if (!channelManager->isChannelConnected(stackChannelId))
	{
		BT_DEBUG("stackChannelId is not connected");
		return;
	}

	auto writeDataCallback = [binarySocket](BluetoothError error) {
		if (error != BLUETOOTH_ERROR_NONE)
		{
			BT_DEBUG("Failed to write the binary socket data to stack");
			return;
		}
		binarySocket->setWriting(false);
	};

	binarySocket->setWriting(true);
	getImpl<BluetoothSppProfile>(adapterAddress)->writeData(stackChannelId, data, outLen, writeDataCallback);
}

pbnjson::JValue BluetoothSppProfileService::buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
        std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	ChannelManager *channelManager = findChannelImpl(adapterAddress);

	if (channelManager == nullptr)
		return responseObj;

	appendCommonProfileStatus(responseObj, connected, connecting, subscribed,
	                          returnValue, adapterAddress, deviceAddress);
	responseObj.put("connectedChannels", channelManager->getConnectedChannels(deviceAddress));

	return responseObj;
}
