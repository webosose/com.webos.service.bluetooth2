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


#include "bluetoothgattancsprofile.h"
#include "bluetoothprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothgattprofileservice.h"
#include "bluetoothdevice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "logging.h"
#include <iostream>
#include <sstream>
#include <iterator>
#include <ctime>

using namespace std::placeholders;

static std::unordered_map<int, std::string> ancsStatus =
{
		{0, "Added"},
		{1, "Modified"},
		{2, "Removed"}
		//3 - 255 - Reserved
};

static std::unordered_map<int, std::string> ancsFlags =
{
		{1, "Silent"},
		{2, "Important"},
		{4, "Pre-Existing"},
		{8, "Positive Action"},
		{16, "Negative Action"}
		//32 to 128 - Reserved
};

static std::unordered_map<int, std::string> ancsCategory =
{
		{0, "Category Other"},
		{1, "Incoming Call"},
		{2, "Missed Call"},
		{3, "Voicemail"},
		{4, "Social Message"},
		{5, "Schedule"},
		{6, "Email"},
		{7, "News"},
		{8, "Health and Fitness"},
		{9, "Business and Finance"},
		{10, "Location"},
		{11, "Entertainment"}
		//12 - 255 - Reserved
};

BluetoothGattAncsProfile::BluetoothGattAncsProfile (BluetoothManagerService *manager,
		BluetoothGattProfileService *btGattSrvHandle) :
		BluetoothGattProfileService(manager, "GATT","00001801-0000-1000-8000-00805f9b34fb"),
		mNotificationQueryInfo(0),
		mAncsUuid(BluetoothUuid(ANCS_UUID))
{
	LS_CREATE_CLASS_CATEGORY_BEGIN(BluetoothGattAncsProfile, base)
		LS_CATEGORY_CLASS_METHOD(BluetoothProfileService, connect)
		LS_CATEGORY_CLASS_METHOD(BluetoothProfileService, disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothProfileService, getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, advertise)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, awaitConnectionRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, awaitNotifications)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, queryNotificationAttributes)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, performNotificationAction)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattAncsProfile, queryAppAttributes)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/gatt/ancs", LS_CATEGORY_TABLE_NAME(base),
			NULL, NULL);
	manager->setCategoryData("/gatt/ancs", this);
	btGattSrvHandle->registerGattStatusObserver(this);
	BT_DEBUG("ANCS Gatt Service Created");
}

BluetoothGattAncsProfile::~BluetoothGattAncsProfile()
{

}

/**
Start ANCS BLE advertisement

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
includeTxPower| No | Boolean | true or false
TxPower| No | Number | Measured  power at 1m distance from device
includeName|No|Boolean | Include device name with advertisement

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if advertisement was configured and started
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

*/
bool BluetoothGattAncsProfile::advertise(LSMessage &message)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	pbnjson::JValue requestObj;
	LS::Message request(&message);
	int parseError = 0;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema =
			STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
									   OBJECT(includeTxPower, OBJSCHEMA_1(PROP(TxPower,integer))),
									   PROP(includeName, boolean)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		return true;
	}

	bool includeTxPower = false;
	bool includeName = true;
	bool setScanResponse = false;
	uint8_t txPower = 0x00;

	if (requestObj.hasKey("includeTxPower"))
	{
		includeTxPower = true;
		pbnjson::JValue txPowerObj = requestObj["includeTxPower"];
		if (txPowerObj.hasKey("TxPower"))
			txPower = (uint8_t) txPowerObj["TxPower"].asNumber<int32_t>();

	}

	if (requestObj.hasKey("includeName"))
		includeName = requestObj["includeName"].asBool();

	if(requestObj.hasKey("setScanRsp"))
		setScanResponse = requestObj["setScanResponse"].asBool();

	bool connectable = true;
	std::string adapterAddress = getManager()->getAddress();
	BluetoothLowEnergyServiceList serviceList;
	BluetoothLowEnergyData manufacturerData;
	ProprietaryDataList dataList;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}

	if (getManager()->getAdvertisingState()) {
		LSUtils::respondWithError(request, BT_ERR_BLE_ADV_ALREADY_ADVERTISING);
        return true;
	}

	BluetoothAdapter* defaultAdapter = getManager()->getDefaultAdapter();
	if (!defaultAdapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return true;
	}
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto ancsConfigCallback =
			[this,requestMessage,adapterAddress, &defaultAdapter](BluetoothError error)
			{
				pbnjson::JValue responseObj = pbnjson::Object();
				if(BLUETOOTH_ERROR_NONE != error)
				{
					responseObj.put("adapterAddress", adapterAddress);
					responseObj.put("returnValue", false);
					appendErrorResponse(responseObj, error);
					LSUtils::postToClient(requestMessage, responseObj);
					LSMessageUnref(requestMessage);
					return true;
				}
				auto ancsAdvertiseCallback =
				[this,requestMessage,adapterAddress](BluetoothError error)
				{
					pbnjson::JValue responseObj = pbnjson::Object();

					if (BLUETOOTH_ERROR_NONE == error)
					{
						responseObj.put("adapterAddress", adapterAddress);
						responseObj.put("returnValue", true);
						getManager()->setAdvertisingState(true);
					}
					else
					{
						responseObj.put("adapterAddress", adapterAddress);
						appendErrorResponse(responseObj, error);
					}

					LSUtils::postToClient(requestMessage, responseObj);
					LSMessageUnref(requestMessage);

				};

				BT_INFO("ANCS", 0, "Start ANCS Avertisement\n");
				defaultAdapter->startAdvertising(ancsAdvertiseCallback);
				return true;
			};

	BT_DEBUG("configureAdvertisment includeTxPower=%d txPower=%d includeName=%d\n",includeTxPower,txPower,includeName);
	defaultAdapter->configureAdvertisement(connectable, includeTxPower,	includeName, setScanResponse, manufacturerData, serviceList, dataList, ancsConfigCallback,txPower, mAncsUuid);

	return true;
}

bool BluetoothGattAncsProfile::isAncsServiceSupported(LSMessage *requestMessage, std::string adapterAddress, std::string address)
{
	//TODO:Should get called after BSA_BLE_CL_SEARCH_CMPL_EVT instead of waiting for timeout
	BT_DEBUG("[%s](%d) getImpl->getServices\n", __FUNCTION__, __LINE__);
	BluetoothGattServiceList serviceList = getImpl<BluetoothGattProfile>(adapterAddress)->getServices(address);
	BT_DEBUG("%s: serviceList length for address %s %zu", __func__, address.c_str(), serviceList.size());
	bool found = false;
	for (auto service : serviceList) {
		if (service.getUuid()==mAncsUuid) {
			BT_DEBUG("%s: ANCS servicefound %s", __func__, service.getUuid().toString().c_str());
			found = true;
			break;
		}
	}
	return found;
}

void BluetoothGattAncsProfile::connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	std::string address = requestObj["address"].asString();
	if (isDeviceConnecting(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_CONNECTING);
		return;
	}
	if (!BluetoothProfileService::isDevicePaired(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return;
	}
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto isConnectedCallback = [this, requestMessage, adapterAddress, address](BluetoothError error, const BluetoothProperty &property)
	{
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			BT_DEBUG("ANCS Gatt:%s : isConnectedCallback called with error %d", __func__, error);
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		bool connected = property.getValue<bool>();

		if (connected)
		{
			markDeviceAsConnected(address);
			BT_DEBUG("ANCS Gatt:%s : isConnectedCallback profile is already connected %d", __func__, error);
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECTED);
			LSMessageUnref(request.get());
			return;
		}
		BT_INFO("ANCS", 0, "[%s](%d) connect from device %s complete\n", __FUNCTION__, __LINE__, address.c_str());
		markDeviceAsConnecting(address);
		notifyStatusSubscribers(adapterAddress, address, false);

		BT_DEBUG("[%s](%d) getImpl->connect\n", __FUNCTION__, __LINE__);
		mImpl->connect(address, std::bind(&BluetoothGattAncsProfile::connectCallback, this, requestMessage, adapterAddress, address, _1));
	};

	// Before we start to connect with the device we have to make sure
	// we're not already connected with it.
	BT_DEBUG("[%s](%d) getImpl->getProperty\n", __FUNCTION__, __LINE__);
	mImpl->getProperty(address, BluetoothProperty::Type::CONNECTED, isConnectedCallback);

}

bool BluetoothGattAncsProfile::connectCallback(LSMessage *requestMessage, const std::string adapterAddress, std::string address, BluetoothError error)
{
	BT_INFO("ANCS", 0, "ANCS Gatt Service connectCallback called");
	LS::Message request(requestMessage);
	if (error != BLUETOOTH_ERROR_NONE)
	{
		BT_DEBUG("ANCS Gatt:%s : called with error %d", __func__, error);
		LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
		LSMessageUnref(requestMessage);

		markDeviceAsNotConnecting(address);
		notifyStatusSubscribers(adapterAddress, address, false);
		return false;
	}
	if (request.isSubscription())
	{
		uint16_t appId = 0;
		uint16_t connectId = 0;
		auto watch = new LSUtils::ClientWatch(getManager()->get(), requestMessage,
				std::bind(&BluetoothGattAncsProfile::handleConnectClientDisappeared, this, appId, connectId, adapterAddress, address));
		mConnectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(address, watch));
	}

	//initiate discoverservices here, since reconnecting to an address does not load the service list cache in the SIL.
	auto discoverServicesCallback  = [this, requestMessage,  adapterAddress, address](BluetoothError error)
	{
		LS::Message request(requestMessage);
		BT_INFO("ANCS", 0, "discoverServicesCallback for device %s", address.c_str());
		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_SERVICE_DISCOVERY_FAIL);
			LSMessageUnref(requestMessage);
			mImpl->disconnect(address, std::bind(&BluetoothGattAncsProfile::disconnectCallback, this, requestMessage, adapterAddress, address, true, _1));
			return;
		}

		auto serviceTimeoutCallback = [] (gpointer timeout_data) -> gboolean
		{
			BT_INFO("ANCS", 0, "ANCS Gatt serviceTimeoutCallback");
			AncsServiceCheckTimeout* timeoutData =  static_cast<AncsServiceCheckTimeout*>(timeout_data);
			BluetoothGattAncsProfile* thisRef = static_cast<BluetoothGattAncsProfile*>(timeoutData->serviceRef);
			LS::Message request(timeoutData->requestMessage);

			if (!thisRef->isAncsServiceSupported(timeoutData->requestMessage, timeoutData->adapterAddress, timeoutData->address))
			{
				thisRef->mImpl->disconnect(timeoutData->address,
						std::bind(&BluetoothGattAncsProfile::disconnectCallback, thisRef, timeoutData->requestMessage, timeoutData->adapterAddress, timeoutData->address, true, _1));
				LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
				LSMessageUnref(timeoutData->requestMessage);
				return false;
			}
			thisRef->markDeviceAsNotConnecting(timeoutData->address);
			thisRef->markDeviceAsConnected(timeoutData->address);
			thisRef->notifyStatusSubscribers(timeoutData->adapterAddress, timeoutData->address, true);

			pbnjson::JValue responseObj = pbnjson::Object();

			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", timeoutData->adapterAddress);
			responseObj.put("address", timeoutData->address);
			responseObj.put("subscribed",request.isSubscription());
			LSUtils::postToClient(request, responseObj);
			LSMessageUnref(timeoutData->requestMessage);

			DELETE_OBJ(timeoutData)
			return false;
		};

		AncsServiceCheckTimeout *timeoutData = new AncsServiceCheckTimeout ();
		timeoutData->serviceRef = this;
		timeoutData->requestMessage = requestMessage;
		timeoutData->adapterAddress = adapterAddress;
		timeoutData->address = address;

		g_timeout_add_seconds(CONNECT_TIMEOUT, (GSourceFunc)serviceTimeoutCallback, (gpointer) timeoutData);

	};
	BT_DEBUG("[%s](%d) getImpl->discoverServices\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>(adapterAddress)->discoverServices(address, discoverServicesCallback);
	return true;
}

void BluetoothGattAncsProfile::disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);

	std::string address = requestObj["address"].asString();
	if (!getManager()->isDeviceAvailable(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return;
	}

	if (!isDeviceConnected(address))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	BT_DEBUG("[%s](%d) getImpl->disconnect\n", __FUNCTION__, __LINE__);
	mImpl->disconnect(address, std::bind(&BluetoothGattAncsProfile::disconnectCallback, this, requestMessage, adapterAddress, address, false, _1));
};

void BluetoothGattAncsProfile::disconnectCallback(LSMessage *requestMessage, const std::string adapterAddress, std::string address, bool quietDisconnect, BluetoothError error)
{

	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(requestMessage);

	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_DISCONNECT_FAIL);
		return;
	}
	auto unpairCallback = [requestMessage ](BluetoothError error) {
		if (error != BLUETOOTH_ERROR_NONE)
		{
			BT_WARNING(MSGID_UNPAIR_FROM_ANCS_FAILED, 0, "Unable to unpair device");
			return;
		}

	};
	if (isDevicePaired(address))
	{
		getManager()->getDefaultAdapter()->unpair(address, unpairCallback);
	}

	markDeviceAsNotConnecting(address);
	markDeviceAsNotConnected(address);
	if (!quietDisconnect)
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		LSUtils::postToClient(request, responseObj);
	}

	notifyStatusSubscribers(adapterAddress, address, false);
}


pbnjson::JValue BluetoothGattAncsProfile::buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
                                                                       std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();
	appendCommonProfileStatus(responseObj, connected, connecting, subscribed,
	                          returnValue, adapterAddress, deviceAddress);
	return responseObj;
}

/**
This method will monitor and indicate any incoming BLE connect/disconnect request from a remote bluetooth device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
adapterAddress | No | String | Address of the adapter executing this method
subscribe | Yes | Boolean | Must be set subscribe to true. The caller must subscribe to this method.

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if successfully subscribed, false otherwise.
subscribed | Yes | Boolean | subscribed will contain true if the user has subscribed to the method.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if successfully subscribed and when there is a connect/disconnect request from remote device.
subscribed | Yes | Boolean | subscribed will always contain true since subscription ends only when the client chooses to close it.
adapterAddress | Yes | String | Address of the adapter executing this method.
address | Yes | String | The address (bdaddr) of the remote device which initiates the connect/disconnect.
isConnectRequest | Yes | Boolean | True if the remote device sends a connection request (that is establishes a security link), false if the remote device sends a disconnect
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.
*/

bool BluetoothGattAncsProfile::awaitConnectionRequest(LSMessage &message)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string),
                                              PROP_WITH_VAL_1(subscribe, boolean, true))
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

	mGetConnectionRequestSubscriptions.setServiceHandle(getManager());
	mGetConnectionRequestSubscriptions.subscribe(request);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", getManager()->getAddress());
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);

	LSUtils::postToSubscriptionPoint(&mGetConnectionRequestSubscriptions, responseObj);
	return true;
}

void BluetoothGattAncsProfile::incomingLeConnectionRequest(const std::string &address, bool state)
{
	if (mGetConnectionRequestSubscriptions.getSubscribersCount() != 0)
	{
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("adapterAddress", getManager()->getAddress());
		responseObj.put("address", address);
		responseObj.put("returnValue",true);
		responseObj.put("subscribed",true);
		responseObj.put("isConnectRequest", state);

		LSUtils::postToSubscriptionPoint(&mGetConnectionRequestSubscriptions, responseObj);
	}
}

/**
This method will monitor for any notifications added, modified or removed on the connected iOS device.

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
adapterAddress | No | String | Address of the adapter executing this method.
subscribe | Yes | Boolean | Must be set subscribe to true. The caller must subscribe to this method.
address | Yes | String | The address (bdaddr) of the remote device.

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if successfully subscribed, false otherwise.
subscribed | Yes | Boolean | subscribed will contain true if the user has subscribed to the method.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if successfully subscribed and when there is are notifications from the remote device.
subscribed | Yes | Boolean | subscribed will contain true till the client chooses to close it or monitoring fails.
adapterAddress | Yes | String | Address of the adapter executing this method.
address | Yes | String | The address (bdaddr) of the remote device which provides the notifications.
notificationStatus | Yes | String | Indicates the state of a notification - values can be one of added, removed or modified.
notificationInfo | Yes | Object | bluetooth2ANCSObject - Information about the received notification.
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@Object bluetooth2ANCSObject
Name | Required | Type | Description
-----|--------|------|----------
notificationId | Yes | Number | 32-bit unique id for a notification.
notificationFlags | Yes | Object | bluetooth2ANCSFlagObject - Flags to inform the local device of specificities.
category | Yes | Object | bluetooth2ANCSCategoryObject - Category or type of notification.

@Object bluetooth2ANCSFlagObject
Name | Required | Type | Description
-----|--------|------|----------
flagId | Yes | Number | A bitmask whose set bits inform the local device of specificities with the iOS notification.
description | Yes | String | Value can be one of: Important, Pre-Existing, Silent, etc.

@Object bluetooth2ANCSCategoryObject
Name | Required | Type | Description
-----|--------|------|----------
categoryId | Yes | Number | Unique id of the category as given in the ANCS specification.
description | Yes | String | Name of the category. Example: Missed call, social, etc.
count | Yes | Number | Number of active notifications in that category.
*/

bool BluetoothGattAncsProfile::awaitNotifications(LSMessage &message)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string),
                                              PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress,string))
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

	//TODO:  subscribe to indication on service changed characteristic (characteristic 2a05 of service 1801) to look for ANCS service getting lost on remote device, etc

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		//Use default adapter if adapterAddress is not passed by the user
		adapterAddress = getManager()->getAddress();
	}

	std::string address = requestObj["address"].asString();

	if (!isDeviceConnected(address))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	LS::SubscriptionPoint *subscriptionPoint = 0;
	auto subscriptionIter = mAwaitNotificationSubscriptions.find(address);
	if (subscriptionIter == mAwaitNotificationSubscriptions.end())
	{
		subscriptionPoint = new LS::SubscriptionPoint;
		subscriptionPoint->setServiceHandle(getManager());
		mAwaitNotificationSubscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(address, subscriptionPoint));
	}
	else
	{
		subscriptionPoint = subscriptionIter->second;
	}
	subscriptionPoint->subscribe(request);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto monitorCallback = [this, requestMessage, adapterAddress, address] (BluetoothError error)
	{
		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, retrieveErrorText(BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL) + NOTIFICATION_SOURCE_UUID, BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL, true);
		}
		else
		{
			LS::Message request(requestMessage);
			auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
						std::bind(&BluetoothGattAncsProfile::handleNotificationClientDisappeared, this, adapterAddress, address));

			mNotificationWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(address, watch));

			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("adapterAddress", getManager()->getAddress());
			responseObj.put("returnValue", true);
			responseObj.put("subscribed", true);

			LSUtils::postToClient(request, responseObj);
		}
		LSMessageUnref(requestMessage);
	};

	getImpl<BluetoothGattProfile>(adapterAddress)->changeCharacteristicWatchStatus(address, mAncsUuid, BluetoothUuid(NOTIFICATION_SOURCE_UUID), true, monitorCallback);
	return true;
}

void BluetoothGattAncsProfile::handleNotificationClientDisappeared(const std::string &adapterAddress, const std::string &address)
{
	auto watchIter = mNotificationWatches.find(address);
	if (watchIter == mNotificationWatches.end())
		return;

	auto subscriptionIter = mAwaitNotificationSubscriptions.find(address);
	if (subscriptionIter != mAwaitNotificationSubscriptions.end())
	{
		auto subscriptionPoint = subscriptionIter->second;
		//If there are other subscribers on the same address, no need to disable the watch for just one subscriber dropping out
		if (subscriptionPoint->getSubscribersCount() != 0)
			return;
	}

	if (!mImpl)
		return;

	BT_DEBUG("Disabling characteristic watch to device %s", address.c_str());

	getImpl<BluetoothGattProfile>(adapterAddress)->changeCharacteristicWatchStatus(address, mAncsUuid, BluetoothUuid(NOTIFICATION_SOURCE_UUID), false, [this](BluetoothError error)
	{
		BT_WARNING(MSGID_SUBSCRIPTION_CLIENT_DROPPED, 0, "No LS2 error response can be issued since subscription client has dropped");
	});
}

void BluetoothGattAncsProfile::characteristicValueChanged(const std::string &address, const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic)
{
	BT_INFO("ANCS", 0, "characteristic %s", characteristic.getUuid().toString().c_str());
	if ((characteristic.getUuid() == BluetoothUuid(NOTIFICATION_SOURCE_UUID)) && (service == mAncsUuid))
	{
	//Value change is for awaitNotifications
		auto notificationIter = mAwaitNotificationSubscriptions.find(address);
		//Value change is for awaitNotifications
		if (notificationIter != mAwaitNotificationSubscriptions.end())
		{
			LS::SubscriptionPoint *subscriptionPoint = notificationIter->second;
			BluetoothGattValue values = characteristic.getValue();
			BT_DEBUG("Found notification source characteristic value of size %zu", values.size());

			int eventID = (int32_t) values[0];
			int eventFlags = (int32_t) values[1];
			int categoryID = (int32_t) values[2];
			int categoryCount = (int32_t) values[3];

			int32_t notificationUid = values[4] | (values[5] << 8) | (values[6] << 16) | (values[7] << 24);

			//Build response
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("returnValue", true);
			responseObj.put("subscribed", true);
			responseObj.put("adapterAddress", getManager()->getAddress());
			responseObj.put("address", address);

			if (ancsStatus.find(eventID) != ancsStatus.end())
				responseObj.put("notificationStatus", ancsStatus[eventID]);
			else if (ANCS_STATUS_MIN_RESERVED_VALUE < eventID && eventID < ANCS_STATUS_MAX_RESERVED_VALUE)
				responseObj.put("notificationStatus", ANCS_RESERVED);
			else
				responseObj.put("notificationStatus", "Unknown");

			pbnjson::JValue notificationInfoObj = pbnjson::Object();
			notificationInfoObj.put("notificationId", notificationUid);

			pbnjson::JValue notificationFlagObj = pbnjson::Object();
			notificationFlagObj.put("flagId", eventFlags);

			if (ancsFlags.find(eventFlags) != ancsFlags.end())
				notificationFlagObj.put("description", ancsFlags[eventFlags]);
			else if (ANCS_FLAGS_MIN_RESERVED_VALUE < eventFlags && eventFlags < ANCS_FLAGS_MAX_RESERVED_VALUE)
				notificationFlagObj.put("description", ANCS_RESERVED);

			pbnjson::JValue categoryObj = pbnjson::Object();
			categoryObj.put("categoryId", categoryID);

			if (ancsCategory.find(categoryID) != ancsCategory.end())
				categoryObj.put("description", ancsCategory[categoryID]);
			else if (ANCS_CATEGORY_MIN_RESERVED_VALUE < categoryID && categoryID < ANCS_CATEGORY_MAX_RESERVED_VALUE)
				categoryObj.put("description", ANCS_RESERVED);

			categoryObj.put("count", categoryCount);
			notificationInfoObj.put("category", categoryObj);
			notificationInfoObj.put("flag", notificationFlagObj);

			responseObj.put("notificationInfo", notificationInfoObj);

			LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
		}
	}
	else if ((service == mAncsUuid) && (characteristic.getUuid() == BluetoothUuid(DATA_SOURCE_UUID)))
	{
		BluetoothGattValue values = characteristic.getValue();
		int32_t vSize = values.size();
		BT_DEBUG("ANCS data source characteristic value len:%d ", vSize);
		std::stringstream buffer;
		std::copy (values.begin(), values.end(), std::ostream_iterator<uint8_t>(buffer, ", "));
		BT_DEBUG ("values %s", buffer.str().c_str());
		int index = 0;
		if (!mNotificationQueryInfo)
		{
			BT_DEBUG("ANCS QueryInfo object empty");
			return;
		}
		//Ancs block sizes are roughly 155 bytes long. When queried for multiple notification attributes,  the combined response length can
		//exceed this block size or message size may be longer than max block size.
		//Ancs chunks the response with only the first block containing the COMMAND_ID, and subsequent blocks containing only contiguous data
		//To handle responses spanning multiple blocks, we maintain required information in mNotificationQueryInfo object.
		//which is updated after reading a complete attribute or when a new block is received.
		if (values[index] == COMMAND_ID_GET_NOTIFICATION_ATTRIBUTES)
		{
			//Case 1:Incoming block format [0:CommandId], [1-4: NotificationId],[5-vSize : attr1]
			if (vSize < 8)
			{
				BT_DEBUG("ANCS data source characteristic value  has no attribute");
				return;
			}
			int32_t notificationId = values[index + 1] | (values[index + 2] << 8) | (values[index + 3] << 16) | (values[index + 4] << 24);
			index += 5;
			BT_DEBUG("ANCS notificationId=%d", notificationId);

		}
		else if (mNotificationQueryInfo->remainingLen == -1 && mNotificationQueryInfo->readingAttr != MAX_UINT16)
		{
			if (mNotificationQueryInfo->attrLenByte1 == MAX_UINT16)
			{
				//Case 3:Previous block: [0 to n :attr1-id-data][n+1 to (vSize -2): attr2 id and data][vsize-1: attr3 Id]
				//		 Current block:  [0 and 1: attr3 len_bytes_1_2][[1-n: attr3 data]....
				mNotificationQueryInfo->remainingLen = values[index] | (values[index + 1] << 8);
				index += 2;
			}
			else
			{
				//Case 2:Previous block: [0 to n :attr1-id-data][n+1 to (vSize -2): attr2 id and data][vsize-2: attr3Id] [vsize-1: attr3 len_1st_byte]
				//		 Current block:  [0 :attr3 len_2nd_byte][1-n: attr3 data]....
				mNotificationQueryInfo->remainingLen = mNotificationQueryInfo->attrLenByte1 | values[index++] <<8;
				mNotificationQueryInfo->attrLenByte1 = MAX_UINT16;
			}
		}
		if (mNotificationQueryInfo->remainingLen > 0  && mNotificationQueryInfo->readingAttr != MAX_UINT16)
		{
			//Case 4: [0 - x: data overflowed from previous block] [x+1 - vSize :attrN]
			//Continuation of case 2 and case 3
			for (auto attrStatus = mNotificationQueryInfo->attrList.begin(); attrStatus!=mNotificationQueryInfo->attrList.end(); attrStatus++)
			{
				if (attrStatus->attrId == mNotificationQueryInfo->readingAttr)
				{
					size_t len = mNotificationQueryInfo->remainingLen;
					if (len > values.size())
					{
						mNotificationQueryInfo->remainingLen = len - values.size();
						len = values.size();
					}
					else
					{
						mNotificationQueryInfo->remainingLen = -1;
						attrStatus->found = true;
						mNotificationQueryInfo->readingAttr = MAX_UINT16;
					}

					attrStatus->value.append(values.begin() + index , values.begin()+ index + len);
					index = index+len;
					break;
				}
			}
		}

		while(index < vSize)
		{
			int32_t attrId = values[index++];
			mNotificationQueryInfo->readingAttr = attrId;
			if (index+1 >= vSize)
			{
				//last byte was 1st byte of attrLen
				mNotificationQueryInfo->attrLenByte1 = values[index++];
				break;
			}
			if (index >= vSize)
			{ //last byte was attrId
				break;
			}
			int32_t attributeLen = values[index] | (values[index + 1] << 8);
			index += 2;

			BT_DEBUG("attrId %d attributeLen %d", attrId, attributeLen);

			std::string attrValue;
			int valLen = (index + attributeLen >= vSize) ? (vSize - index) : attributeLen;
			attrValue.assign(values.begin() + index, values.begin() + index + valLen);

			index = index + valLen;

			for (auto attrStatus = mNotificationQueryInfo->attrList.begin(); attrStatus != mNotificationQueryInfo->attrList.end(); attrStatus++)
			{
				if (attrStatus->attrId == attrId)
				{
					attrStatus->value = attrValue;
					if (valLen == attributeLen)
					{
						attrStatus->found = true;
						mNotificationQueryInfo->readingAttr = MAX_UINT16;
						BT_DEBUG("attributeId %d, value %s", attrId, attrValue.c_str());
					}
					mNotificationQueryInfo->remainingLen = attributeLen - valLen;
					break;
				}
			}
		}
		//compose the response
		bool foundAllAttributes = true;
		for (auto attrStatus = mNotificationQueryInfo->attrList.begin(); attrStatus != mNotificationQueryInfo->attrList.end(); attrStatus++)
		{
			if (!attrStatus->found)
			{
				foundAllAttributes = false;
			}
		}

		if (foundAllAttributes)
		{
			pbnjson::JValue attributeListObj = pbnjson::Array();
			for (auto attrStatus = mNotificationQueryInfo->attrList.begin(); attrStatus != mNotificationQueryInfo->attrList.end(); attrStatus++)
			{
				pbnjson::JValue attrObj = pbnjson::Object();
				attrObj.put("attributeId", attrStatus->attrId);
				attrObj.put("value", attrStatus->value);
				attributeListObj.append(attrObj);
			}
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", getManager()->getAddress());
			responseObj.put("address", address);
			responseObj.put("attributes", attributeListObj);
			responseObj.put("subscribed", false);

			LSUtils::postToClient(mNotificationQueryInfo->requestMessage, responseObj);

			LSMessageUnref(mNotificationQueryInfo->requestMessage);
			DELETE_OBJ(mNotificationQueryInfo)

			BT_DEBUG("[%s](%d) getImpl->changeCharacteristicWatchStatus\n", __FUNCTION__, __LINE__);
			getImpl<BluetoothGattProfile>(getManager()->getAddress())->changeCharacteristicWatchStatus (address, mAncsUuid, BluetoothUuid(DATA_SOURCE_UUID), false, [this](BluetoothError error)
			{
			      BT_DEBUG("Found all attributes. Remove CharacteristicWatch for DATA_SOURCE_UUID");
			});
		}
	}
}

/**
 This method will monitor for any notifications added, modified or removed on the connected iOS device.

 @par Parameters

 Name | Required | Type | Description
 -----|--------|------|----------
 adapterAddress | No | String | Address of the adapter executing this method.
 subscribe | Yes | Boolean | Must be set subscribe to true. The caller must subscribe to this method.
 address | Yes | String | The address (bdaddr) of the remote device.
 notificationId | Yes | Number | ANCS Notification id being queried
 attributes| Yes | Array of bluetooth2ANCSAttributeRequestObject

 @par Returns(Call)

 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | Yes | Boolean | Value is true if successfully subscribed, false otherwise.
 subscribed | Yes | Boolean | subscribed will contain true if the user has subscribed to the method.
 errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
 errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

 @par Returns(Subscription)

 Name | Required | Type | Description
 -----|--------|------|----------
 returnValue | Yes | Boolean | Value is true if successfully subscribed and when there is are notifications from the remote device.
 subscribed | Yes | Boolean | subscribed will contain true till the client chooses to close it or monitoring fails.
 adapterAddress | Yes | String | Address of the adapter executing this method.
 address | Yes | String | The address (bdaddr) of the remote device which provides the notifications.
 attributes| Yes | bluetooth2ANCSAttributeResponseObject | Value of attribute being queried.

 errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
 errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

 @Object bluetooth2ANCSAttributeRequestObject
 Name | Required | Type | Description
 -----|--------|------|----------
 attributeId | Yes | Number | Notification Attributes id .
 length | No | Number | Length of attribute value to fetch.

 @Object bluetooth2ANCSAttributeResponseObject
 Name | Required | Type | Description
 -----|--------|------|----------
 attributeId | Yes | Number | Notification Attributes id .
 value | Yes | String | Attribute Value returned.

 */

bool BluetoothGattAncsProfile::queryNotificationAttributes(LSMessage &message)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}
	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress,string), PROP(notificationId, integer),
			           PROP(subscribe, boolean), OBJARRAY(attributes, OBJSCHEMA_2(PROP(attributeId, integer), PROP(length, integer))))
			           REQUIRED_4(address, notificationId, attributes, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		BT_DEBUG("%s: Parse Payload error",__func__);
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("notificationId"))
			LSUtils::respondWithError(request, BT_ERR_ANCS_NOTIFICATIONID_PARAM_MISSING);

		else if (!requestObj.hasKey("attributes"))
			LSUtils::respondWithError(request, BT_ERR_ANCS_ATTRIBUTELIST_PARAM_MISSING);

		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress = getManager()->getAddress();
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}

	std::string deviceAddress = requestObj["address"].asString();

	if (mNotificationQueryInfo)
	{
		time_t currTime;
		time(&currTime);
		if (difftime(currTime, mNotificationQueryInfo->startTime) > MESSAGE_TIMEOUT)
		{
			//Notify other Query Subscriber of the timeout by setting subscribe status to false
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("returnValue", false);
			responseObj.put("subscribed", false);
			responseObj.put("adapterAddress", getManager()->getAddress());
			responseObj.put("address", deviceAddress);
			LSUtils::postToSubscriptionPoint(&mQueryNotificationSubscription, responseObj);

			LSMessageUnref(mNotificationQueryInfo->requestMessage);
			DELETE_OBJ(mNotificationQueryInfo)
		}
		else
		{
			LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_ANCS_QUERY);
			return true;
		}
	}

	if (!isDeviceConnected(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	int32_t notificationId = requestObj["notificationId"].asNumber<int32_t>();

	pbnjson::JValue attributesObj;
	if (requestObj.hasKey("attributes"))
	{
		attributesObj = requestObj["attributes"];
	}

	if (!requestObj.hasKey("attributes") || attributesObj.arraySize() < 1)
	{
		LSUtils::respondWithError(request, BT_ERR_ANCS_ATTRIBUTELIST_PARAM_MISSING);
		return true;
	}

	BluetoothGattValue value;
	value.push_back(COMMAND_ID_GET_NOTIFICATION_ATTRIBUTES);
	//TODO: is &0xff necessary?

	value.push_back(notificationId & 0xff);
	value.push_back(notificationId >> 8 & 0xff);
	value.push_back(notificationId >> 16 & 0xff);
	value.push_back(notificationId >> 24 & 0xff);

	mNotificationQueryInfo = new NotificationIdQueryInfo();
	mNotificationQueryInfo->readingAttr = MAX_UINT16;
	mNotificationQueryInfo->attrLenByte1 = MAX_UINT16;
	mNotificationQueryInfo->deviceAddress = deviceAddress;
	mNotificationQueryInfo->notificationId = notificationId;
	mNotificationQueryInfo->remainingLen = -1;

	for (int j = 0; j < attributesObj.arraySize(); j++)
	{
		auto attr = attributesObj[j];
		int32_t id = attr["attributeId"].asNumber<int32_t>();
		if (id > MAX_CHAR)
		{
			LSUtils::respondWithError(request, BT_ERR_ANCS_ATTRIBUTE_PARAM_INVAL);
			DELETE_OBJ(mNotificationQueryInfo)
			return true;
		}
		value.push_back(id);

		if (attr.hasKey("length"))
		{
			int32_t len = attr["length"].asNumber<int32_t>();
			if (len > MAX_UINT16)
			{
				LSUtils::respondWithError(request, BT_ERR_ANCS_ATTRIBUTE_PARAM_INVAL);
				DELETE_OBJ(mNotificationQueryInfo)
				return true;
			}
			value.push_back(len & 0xff);
			value.push_back((len >> 8) & 0xff);
		}
		mNotificationQueryInfo->attrList.push_back(NotificationAttr(id));
	}

	BluetoothGattCharacteristic controlPointCharacteristic;
	controlPointCharacteristic.setUuid(BluetoothUuid(CONTROL_POINT_UUID));
	controlPointCharacteristic.setValue(value);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto monitorCallback = [this, requestMessage, adapterAddress, deviceAddress] (BluetoothError error)
		{
			BT_INFO("ANCS", 0, "monitorCallback called with error %d for dataSourceUuid ", error);
			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(requestMessage, retrieveErrorText(BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL) + DATA_SOURCE_UUID, BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL, true);
			}
			LSMessageUnref(requestMessage);

		    return;

	};
	BT_DEBUG("[%s](%d) getImpl->changeCharacteristicWatchStatus\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>(adapterAddress)->changeCharacteristicWatchStatus(deviceAddress, mAncsUuid, BluetoothUuid(DATA_SOURCE_UUID), true, monitorCallback);

	time(&mNotificationQueryInfo->startTime);
	mNotificationQueryInfo->requestMessage = request.get();
	LSMessageRef(mNotificationQueryInfo->requestMessage);


	AncsServiceCheckTimeout *timeoutData = new AncsServiceCheckTimeout();
	timeoutData->serviceRef = this;
	timeoutData->requestMessage = request.get();
	LSMessageRef(timeoutData->requestMessage);
	timeoutData->adapterAddress = adapterAddress;
	timeoutData->address = deviceAddress;
	timeoutData->characteristic = BluetoothGattCharacteristic (controlPointCharacteristic);

	auto serviceTimeoutCallback = [] (gpointer timeout_data) -> gboolean
		{
			BT_INFO("ANCS", 0, "ANCS Gatt serviceTimeoutCallback");
			AncsServiceCheckTimeout* timeoutData = static_cast<AncsServiceCheckTimeout*>(timeout_data);
			BluetoothGattAncsProfile* thisRef = static_cast<BluetoothGattAncsProfile*>(timeoutData->serviceRef);

			auto writeCharacteristicCallback = [timeoutData](BluetoothError error)
			{
				BT_INFO("ANCS", 0, "writeCharacteristicCallback called with error %d for characteristic %s", error, timeoutData->characteristic.getUuid().toString().c_str());
				if (error != BLUETOOTH_ERROR_NONE)
				{
					LSUtils::respondWithError(timeoutData->requestMessage, BT_ERR_GATT_WRITE_CHARACTERISTIC_FAIL);

					LSMessageUnref(timeoutData->serviceRef->mNotificationQueryInfo->requestMessage);
					DELETE_OBJ (timeoutData->serviceRef->mNotificationQueryInfo)
				}

				LSMessageUnref(timeoutData->requestMessage);
				delete timeoutData;
			};

			BT_DEBUG("[%s](%d) getImpl->writeCharacteristic\n", __FUNCTION__, __LINE__);
			thisRef->getImpl<BluetoothGattProfile>(timeoutData->adapterAddress)->writeCharacteristic(timeoutData->address, thisRef->mAncsUuid, timeoutData->characteristic, writeCharacteristicCallback);

			return true;
		};

	g_timeout_add_seconds(WRITE_TIMEOUT, (GSourceFunc) serviceTimeoutCallback, (gpointer) timeoutData);

	mQueryNotificationSubscription.setServiceHandle(getManager());
	mQueryNotificationSubscription.subscribe(request);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothGattAncsProfile::performNotificationAction(LSMessage &message)
{
	BT_INFO("ANCS", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}
	const std::string schema = STRICT_SCHEMA (PROPS_4 (PROP(adapterAddress, string), PROP(address, string),
                                               PROP(notificationId, integer), PROP(actionId, integer))
                                               REQUIRED_3(address, notificationId, actionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("notificationId"))
			LSUtils::respondWithError(request, BT_ERR_ANCS_NOTIFICATIONID_PARAM_MISSING);

		else if (!requestObj.hasKey("actionId"))
			LSUtils::respondWithError(request, BT_ERR_ANCS_ACTIONID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string address = requestObj["address"].asString();
	if (!isDeviceConnected(address))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		//Use default adapter if adapterAddress is not passed by the user
		adapterAddress = getManager()->getAddress();
	}

	uint32_t notificationId = requestObj["notificationId"].asNumber<int32_t>();
	uint8_t actionId = (uint8_t) requestObj["actionId"].asNumber<int32_t>();

	// Build perform notification command as per ANCS spec
	// CommandID - Byte 1
	// NotificationUID - Byte 2-5
	// ActionID - Byte 6

	BluetoothGattValue value;
	value.push_back(COMMAND_ID_NOTIFICATION_ACTION);
	//TODO: replace with new method (to be introduced)
	value.push_back((uint8_t) (notificationId & 0xFF));
	value.push_back((uint8_t) ((notificationId >> 8) & 0xFF));
	value.push_back((uint8_t) ((notificationId >> 16) & 0xFF));
	value.push_back((uint8_t) ((notificationId >> 24) & 0xFF));
	value.push_back(actionId);

	BluetoothGattCharacteristic characteristicToWrite;
	if (!isCharacteristicValid(adapterAddress, address, ANCS_UUID, CONTROL_POINT_UUID, &characteristicToWrite))
	{
		LSUtils::respondWithError(request, BT_ERR_ANCS_NOTIF_ACTION_NOT_ALLOWED);
		return true;
	}
	characteristicToWrite.setValue(value);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto writeCharacteristicCallback  = [this, requestMessage, adapterAddress](BluetoothError error) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_ANCS_NOTIFICATION_ACTION_FAIL);
			return;
		}

		BT_INFO("ANCS", 0, "write characteristic complete for control point characteristic of ANCS service");
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(requestMessage, responseObj);

		LSMessageUnref(requestMessage);
	};

	BT_DEBUG("[%s](%d) getImpl->writeCharacteristic\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>(adapterAddress)->writeCharacteristic(address, BluetoothUuid(ANCS_UUID), characteristicToWrite, writeCharacteristicCallback);

	return true;
}


bool BluetoothGattAncsProfile::queryAppAttributes(LSMessage &message)
{
	return true;
}

