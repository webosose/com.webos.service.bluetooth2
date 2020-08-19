// Copyright (c) 2020 LG Electronics, Inc.
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


#include "bluetoothmapprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "config.h"

using namespace std::placeholders;
const std::array<std::pair<std::string,BluetoothMapProperty::Type>,11> filterParam = { std::make_pair("startOffset",BluetoothMapProperty::Type::STARTOFFSET),
																				std::make_pair("maxCount",BluetoothMapProperty::Type::MAXCOUNT),
																				std::make_pair("subjectLength",BluetoothMapProperty::Type::SUBJECTLENGTH),
																				std::make_pair("periodBegin",BluetoothMapProperty::Type::PERIODBEGIN),
																				std::make_pair("periodEnd",BluetoothMapProperty::Type::PERIODEND),
																				std::make_pair("recipient",BluetoothMapProperty::Type::RECIPIENT),
																				std::make_pair("sender",BluetoothMapProperty::Type::SENDER),
																				std::make_pair("priority",BluetoothMapProperty::Type::PRIORITY),
																				std::make_pair("read",BluetoothMapProperty::Type::READ),
																				std::make_pair("messageTypes",BluetoothMapProperty::Type::MESSAGETYPES),
																				std::make_pair("fields",BluetoothMapProperty::Type::FIELDS)};

BluetoothMapProfileService::BluetoothMapProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "MAP", "00001132-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, connect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMASInstances)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMessageFilters)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMessageList)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getFolderList)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, setFolder)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMessage)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, setMessageStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, pushMessage)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMessageNotification)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/map", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/map", this);
}

BluetoothMapProfileService::~BluetoothMapProfileService()
{

}

void BluetoothMapProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothMapProfile>()->registerObserver(this);
}

void BluetoothMapProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothMapProfile>(adapterAddress)->registerObserver(this);
}

void BluetoothMapProfileService::propertiesChanged(const std::string &adapterAddress, const std::string &sessionKey, BluetoothPropertiesList properties)
{
	bool connected = false;

	for (auto prop : properties)
	{
		switch (prop.getType())
		{
			case BluetoothProperty::Type::CONNECTED:
				connected = prop.getValue<bool>();
				break;

			default:
				break;
		}
	}

	if(!connected)
	{
		auto watchesIter = mConnectWatchesForMultipleAdaptersWithSessionKey.find(adapterAddress);
		if (watchesIter == mConnectWatchesForMultipleAdaptersWithSessionKey.end())
			return;
		auto watchIter = (watchesIter->second).find(sessionKey);
		if (watchIter == (watchesIter->second).end())
			return;
		handleDeviceClientDisappeared(adapterAddress, sessionKey);
	}

}

pbnjson::JValue BluetoothMapProfileService::appendMasInstances(const std::string &adapterAddress, const std::string &deviceAddress)
{
	std::map<std::string, std::vector<std::string>> mapInstancesSupports;
	pbnjson::JValue platformObjArr = pbnjson::Array();

	BluetoothDevice *device = getManager()->findDevice(adapterAddress, deviceAddress);
	if (device)
		mapInstancesSupports = device->getSupportedMessageTypes();

	for (auto supports = mapInstancesSupports.begin(); supports != mapInstancesSupports.end(); supports++)
	{
		pbnjson::JValue object = pbnjson::Object();

		object.put("instanceName",supports->first);
		object.put("supportedMessageTypes", appendMasInstanceSupportedtypes(supports->second));
		platformObjArr.append(object);
	}

	return platformObjArr;
}

pbnjson::JValue BluetoothMapProfileService::appendMasInstanceSupportedtypes(std::vector<std::string> supportedtypes)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto data = supportedtypes.begin(); data != supportedtypes.end(); data++)
	{
		platformObjArr.append(data->c_str());
	}
	return platformObjArr;
}

void BluetoothMapProfileService::notifyGetMasInstaces(pbnjson::JValue responseObj, const std::string &adapterAddress, const std::string &deviceAddress)
{
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	responseObj.put("masInstances", appendMasInstances(adapterAddress, deviceAddress));
}

bool BluetoothMapProfileService::prepareGetMasInstances(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
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

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

bool BluetoothMapProfileService::requiredCheckForMapProfile(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
	return false;

	std::string deviceAddress = requestObj["address"].asString();

	if (!getManager()->findDevice(adapterAddress,deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return false;
	}

	if (!BluetoothProfileService::isDevicePaired(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return false;
	}

	if (!getImpl<BluetoothMapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	return true;
}

bool BluetoothMapProfileService::getMASInstances(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!prepareGetMasInstances(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();
	std::string masInstance;
	if (requestObj.hasKey("masInstance"))
		masInstance = requestObj["masInstance"].asString();

	pbnjson::JValue responseObj = pbnjson::Object();
	notifyGetMasInstaces(responseObj, adapterAddress, deviceAddress);

	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMapProfileService::isInstanceNameValid(const std::string &instance, const std::string &adapterAddress, const std::string &deviceAddress)
{
	std::map<std::string, std::vector<std::string>> mapInstancesSupports;

	BluetoothDevice *device = getManager()->findDevice(adapterAddress, deviceAddress);
	if (device)
	{
		mapInstancesSupports = device->getSupportedMessageTypes();
		auto it = mapInstancesSupports.find(instance);
		if(it != mapInstancesSupports.end())
			return true;
	}
	return false;
}

bool BluetoothMapProfileService::isSessionIdValid(const std::string &adapterAddress, const std::string &deviceAddress,const std::string &sessionId, std::string &sessionKey)
{
	auto connectedDevicesiter = mConnectedDevicesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (connectedDevicesiter != mConnectedDevicesForMultipleAdaptersWithSessionKey.end())
	{
		std::map<std::string, std::string> connectedDevicesWithSessonKey = connectedDevicesiter->second;
		for (auto it = connectedDevicesWithSessonKey.begin(); it != connectedDevicesWithSessonKey.end(); ++it)
		{
			if (it->second == sessionId)
			{
				sessionKey = it->first;
				return (sessionKey.find(deviceAddress) != std::string::npos ? true: false);
			}
		}
	}
	return false;
}

inline std::string BluetoothMapProfileService::generateSessionKey(const std::string &deviceAddress, const std::string &instanceName)
{
	return deviceAddress + "_" + instanceName;
}

bool BluetoothMapProfileService::connect(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	bool subscribed = false;
	std::string adapterAddress;

	if (!prepareConnect(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();

	std::string instanceName;
	if (requestObj.hasKey("instanceName"))
	{
		instanceName = requestObj["instanceName"].asString();
		if( !isInstanceNameValid(instanceName, adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_MAP_INSTANCE_NOT_EXIST);
			return true;
		}
	}
	else
	{
		std::map<std::string, std::vector<std::string>> mapInstancesSupports;
		BluetoothDevice *device = getManager()->findDevice(adapterAddress, deviceAddress);
		if (device)
		{
			mapInstancesSupports = device->getSupportedMessageTypes();
			if(mapInstancesSupports.size())
				instanceName = mapInstancesSupports.begin()->first;
		}
		if(instanceName.empty())
		{
			LSUtils::respondWithError(request, BT_ERR_MAP_INSTANCE_NOT_EXIST);
			return true;
		}
	}

	std::string sessionKey = generateSessionKey(deviceAddress, instanceName);
	if (isDeviceConnecting(adapterAddress, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_CONNECTING);
		return true;
	}

	if (isDeviceConnected(adapterAddress, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_INSTANCE_ALREADY_CONNECTED);
		return true;
	}

	if (request.isSubscription())
	{
		auto watchesIter = mConnectWatchesForMultipleAdaptersWithSessionKey.find(adapterAddress);
		if (watchesIter == mConnectWatchesForMultipleAdaptersWithSessionKey.end())
		{
			std::map<std::string, LSUtils::ClientWatch*> connectWatches;
			auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
								std::bind(&BluetoothMapProfileService::handleConnectClientDisappeared, this, adapterAddress, sessionKey));

			connectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(sessionKey, watch));
			mConnectWatchesForMultipleAdaptersWithSessionKey.insert(std::pair<std::string, std::map<std::string, LSUtils::ClientWatch*>>(adapterAddress, connectWatches));
		}
		else
		{
			auto watchIter = (watchesIter->second).find(sessionKey);
			if (watchIter == (watchesIter->second).end())
			{
				std::map<std::string, LSUtils::ClientWatch*> connectWatches;
				auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
									std::bind(&BluetoothMapProfileService::handleConnectClientDisappeared, this, adapterAddress, sessionKey));
				(watchesIter->second).insert(std::pair<std::string, LSUtils::ClientWatch*>(sessionKey, watch));
			}
		}
		subscribed = true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto connectCallback = [ = ](BluetoothError error, const std::string& sessionId) {
		LS::Message request(requestMessage);

		pbnjson::JValue responseObj;

		if (BLUETOOTH_ERROR_NONE != error)
		{
			markDeviceAsNotConnecting(adapterAddress, sessionKey);
			notifyGetStatusSubscribers(adapterAddress, sessionKey);
			LSUtils::respondWithError(request, error);
			LSMessageUnref(request.get());
		}
		else
		{
			markDeviceAsNotConnecting(adapterAddress, sessionKey);
			markDeviceAsConnected(adapterAddress, sessionKey);
			markDeviceAsConnectedWithSessionKey(adapterAddress, sessionId, sessionKey);
			notifyGetStatusSubscribers(adapterAddress, sessionKey);
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("subscribed", subscribed);
			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("address", deviceAddress);
			responseObj.put("sessionId", sessionId);
			responseObj.put("instanceName", instanceName);
			LSUtils::postToClient(request, responseObj);

		}
	};
	markDeviceAsConnecting(adapterAddress, sessionKey);
	notifyGetStatusSubscribers(adapterAddress, sessionKey);
	getImpl<BluetoothMapProfile>(adapterAddress)->connect(deviceAddress, instanceName, connectCallback);
	return true;
}

std::string BluetoothMapProfileService::getSessionId(const std::string &adapterAddress, const std::string &sessionKey)
{
	std::string sessionId;

	auto connectedDevicesiter = mConnectedDevicesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (connectedDevicesiter != mConnectedDevicesForMultipleAdaptersWithSessionKey.end())
	{
		auto connectedDeviceiter = (connectedDevicesiter->second).find(sessionKey);
		if (connectedDeviceiter != (connectedDevicesiter->second).end())
		{
			sessionId = connectedDeviceiter->second;
		}
	}
	return sessionId;
}

void BluetoothMapProfileService::handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey)
{
	auto watchesIter = mConnectWatchesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (watchesIter == mConnectWatchesForMultipleAdaptersWithSessionKey.end())
		return;
	auto watchIter = (watchesIter->second).find(sessionKey);
	if (watchIter == (watchesIter->second).end())
		return;

	 auto disconnectCallback = [ = ](BluetoothError error, const std::string &instanceName) {
		handleMessageNotificationClientDisappeared(adapterAddress,sessionKey);
		removeDeviceAsConnectedWithSessionKey(adapterAddress, sessionKey);
		markDeviceAsNotConnected(adapterAddress, sessionKey);
		markDeviceAsNotConnecting(adapterAddress, sessionKey);
		notifyGetStatusSubscribers(adapterAddress, sessionKey);
		if(watchIter->second)
		{
			LSUtils::ClientWatch *watch = watchIter->second;
			(watchesIter->second).erase(watchIter);
			delete watch;
		}
	};

	std::string address = parseAddressFromSessionKey(sessionKey);
	std::string sessionId = getSessionId(adapterAddress, sessionKey);

	if(!sessionId.empty())
	{
		getImpl<BluetoothMapProfile>(adapterAddress)->disconnect(sessionKey, sessionId, disconnectCallback);
	}
}


void BluetoothMapProfileService::handleDeviceClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey)
{
	std::string address = parseAddressFromSessionKey(sessionKey);
	std::string sessionId = getSessionId(adapterAddress, sessionKey);
	handleMessageNotificationClientDisappeared(adapterAddress,sessionKey);
	removeDeviceAsConnectedWithSessionKey(adapterAddress, sessionKey);
	markDeviceAsNotConnected(adapterAddress, sessionKey);
	markDeviceAsNotConnecting(adapterAddress, sessionKey);
	notifyGetStatusSubscribers(adapterAddress, sessionKey);
	removeConnectWatchForDevice(address, adapterAddress, sessionKey, sessionId, true, true);
}


bool BluetoothMapProfileService::prepareConnect(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string),
			                         PROP(instanceName, string), PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	std::string address = requestObj["address"].asString();

	if (!BluetoothProfileService::isDevicePaired(adapterAddress, address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return false;
	}

	if (!getImpl<BluetoothMapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	return true;
}

void BluetoothMapProfileService::markDeviceAsConnectedWithSessionKey(const std::string &adapterAddress, const std::string &sessionId, const std::string &sessionKey)
{
	auto connectedDevicesiter = mConnectedDevicesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (connectedDevicesiter == mConnectedDevicesForMultipleAdaptersWithSessionKey.end())
	{
		std::map<std::string, std::string> connectedDevices;
		connectedDevices.insert(std::pair<std::string, std::string>(sessionKey, sessionId));
		mConnectedDevicesForMultipleAdaptersWithSessionKey.insert(std::pair<std::string, std::map<std::string, std::string>>(adapterAddress, connectedDevices));
	}
	else
	{
		auto connectedDeviceiter = (connectedDevicesiter->second).find(sessionKey);
		if (connectedDeviceiter == (connectedDevicesiter->second).end())
		{
			(connectedDevicesiter->second).insert(std::pair<std::string, std::string>(sessionKey, sessionId));
		}
	}
}

void BluetoothMapProfileService::removeDeviceAsConnectedWithSessionKey(const std::string &adapterAddress, const std::string &sessionKey)
{
	auto connectedDevicesiter = mConnectedDevicesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (connectedDevicesiter != mConnectedDevicesForMultipleAdaptersWithSessionKey.end())
	{
		auto connectedDeviceiter = (connectedDevicesiter->second).find(sessionKey);
		if (connectedDeviceiter != (connectedDevicesiter->second).end())
		{
			(connectedDevicesiter->second).erase(connectedDeviceiter);
		}
	}
}

std::string BluetoothMapProfileService::parseInstanceNameFromSessionKey(const std::string &sessionKey)
{
	std::size_t keyPos = sessionKey.find("_");
	return ( keyPos == std::string::npos ? "" : sessionKey.substr(keyPos + 1));
}

std::string BluetoothMapProfileService::parseAddressFromSessionKey(const std::string &sessionKey)
{
	std::size_t keyPos = sessionKey.find("_");
	return ( keyPos == std::string::npos ? "" : sessionKey.substr(0, keyPos));
}

bool BluetoothMapProfileService::disconnect(LSMessage &message)
{

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isSessionIdSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto disconnectCallback = [ = ](BluetoothError error, const std::string &instanceName) {

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
		responseObj.put("address", address);
		responseObj.put("instanceName", instanceName);
		responseObj.put("sessionId", sessionId);
		LSUtils::postToClient(request, responseObj);
		removeDeviceAsConnectedWithSessionKey(adapterAddress, sessionKey);
		markDeviceAsNotConnected(adapterAddress, sessionKey);
		markDeviceAsNotConnecting(adapterAddress, sessionKey);
		notifyGetStatusSubscribers(adapterAddress, sessionKey);
		removeConnectWatchForDevice(address, adapterAddress, sessionKey, sessionId, true, false);
		LSMessageUnref(request.get());

	};
	getImpl<BluetoothMapProfile>(adapterAddress)->disconnect(sessionKey, sessionId, disconnectCallback);
	return true;
}

bool BluetoothMapProfileService::isSessionIdSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string))  REQUIRED_2(address, sessionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

void BluetoothMapProfileService::removeConnectWatchForDevice(const std::string &address, const std::string &adapterAddress, const std::string &sessionKey, const std::string &sessionId, bool disconnected, bool remoteDisconnect)
{
	auto watchesIter = mConnectWatchesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (watchesIter == mConnectWatchesForMultipleAdaptersWithSessionKey.end())
		return;
	auto watchIter = (watchesIter->second).find(sessionKey);
	if (watchIter == (watchesIter->second).end())
		return;
	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("address", address);
	responseObj.put("instanceName", parseInstanceNameFromSessionKey(sessionKey));
	responseObj.put("sessionId", sessionId);
	responseObj.put("subscribed", false);
	responseObj.put("returnValue", true);
	if (disconnected)
	{
		if (remoteDisconnect)
			responseObj.put("disconnectByRemote", true);
		else
			responseObj.put("disconnectByRemote", false);
	}
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(watch->getMessage(), responseObj);
	(watchesIter->second).erase(watchIter);
	delete watch;
}

bool BluetoothMapProfileService::getStatus(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;
	bool subscribed = false;

	if (!prepareGetStatus(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();
	std::string instanceName;
	if (requestObj.hasKey("instanceName"))
		instanceName = requestObj["instanceName"].asString();

	if (request.isSubscription())
	{
		LS::SubscriptionPoint *subscriptionPoint = 0;

		auto subscriptionsIter = mMapGetStatusSubscriptionsForMultipleAdapters.find(adapterAddress);
		if (subscriptionsIter == mMapGetStatusSubscriptionsForMultipleAdapters.end())
		{
			std::map<std::string, LS::SubscriptionPoint*> subscriptions;
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			subscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(deviceAddress, subscriptionPoint));

			mMapGetStatusSubscriptionsForMultipleAdapters.insert(std::pair<std::string, std::map<std::string, LS::SubscriptionPoint*>>(adapterAddress, subscriptions));

		}
		else
		{
			auto subscriptionIter = (subscriptionsIter->second).find(deviceAddress);
			if (subscriptionIter == (subscriptionsIter->second).end())
			{
				std::map<std::string, LS::SubscriptionPoint*> subscriptions;
				subscriptionPoint = new LS::SubscriptionPoint;
				subscriptionPoint->setServiceHandle(getManager());
				(subscriptionsIter->second).insert(std::pair<std::string, LS::SubscriptionPoint*>(deviceAddress, subscriptionPoint));

			}
			else
			{
				subscriptionPoint = subscriptionIter->second;
			}
		}

		subscriptionPoint->subscribe(request);
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj = buildMapGetStatusResp(adapterAddress, deviceAddress, instanceName);
	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMapProfileService::prepareGetStatus(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{

	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string), PROP(instanceName, string),
			                                  PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;
	return true;
}

pbnjson::JValue BluetoothMapProfileService::buildMapGetStatusResp(const std::string &adapterAddress, const std::string &deviceAddress, const std::string &instanceName)
{
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("status", appendMasInstanceStatus(adapterAddress, deviceAddress, instanceName));
	responseObj.put("subscribed", false);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	return responseObj;
}

void BluetoothMapProfileService::notifyGetStatusSubscribers(const std::string &adapterAddress, const std::string &sessionKey)
{
	auto subscriptionsIter = mMapGetStatusSubscriptionsForMultipleAdapters.find(adapterAddress);
	if (subscriptionsIter == mMapGetStatusSubscriptionsForMultipleAdapters.end())
		return;
	std::string deviceAddress = parseAddressFromSessionKey(sessionKey);
	auto subscriptionIter = (subscriptionsIter->second).find(deviceAddress);
	if (subscriptionIter == (subscriptionsIter->second).end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;
	std::string instanceName = parseInstanceNameFromSessionKey(sessionKey);
	pbnjson::JValue responseObj = buildMapGetStatusResp(adapterAddress, deviceAddress, instanceName);

	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

pbnjson::JValue BluetoothMapProfileService::appendMasInstanceStatus(const std::string &adapterAddress, const std::string &deviceAddress, const std::string &masInstance)
{
	std::map<std::string, std::vector<std::string>> mapInstancesSupports;
	pbnjson::JValue platformObjArr = pbnjson::Array();

	std::string sessionKey = generateSessionKey(deviceAddress, masInstance);
	std::string sessionId = getSessionId(adapterAddress, sessionKey);
	bool Connected = isDeviceConnected(adapterAddress, sessionKey);

	BluetoothDevice *device = getManager()->findDevice(adapterAddress, deviceAddress);
	if (device)
		mapInstancesSupports = device->getSupportedMessageTypes();

	if(masInstance.empty())
	{
		for (auto supports = mapInstancesSupports.begin(); supports != mapInstancesSupports.end(); supports++)
		{
			pbnjson::JValue object = pbnjson::Object();
			sessionKey = generateSessionKey(deviceAddress, supports->first);
			sessionId = getSessionId(adapterAddress, sessionKey);
			Connected = isDeviceConnected(adapterAddress, sessionKey);

			object.put("instanceName", supports->first);
			if(Connected)
				object.put("sessionId", sessionId);
			object.put("Connecting", isDeviceConnecting(adapterAddress, sessionKey));
			object.put("Connected", Connected);
			platformObjArr.append(object);
		}
	}
	else
	{
			pbnjson::JValue object = pbnjson::Object();
			object.put("instanceName",masInstance);
			if(Connected)
				object.put("sessionId", sessionId);
			object.put("Connecting", isDeviceConnecting(adapterAddress, sessionKey));
			object.put("Connected", isDeviceConnected(adapterAddress, sessionKey));
			platformObjArr.append(object);
	}
	return platformObjArr;
}

bool BluetoothMapProfileService::getMessageFilters(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isSessionIdSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto getMessageFiltersCallback = [ = ](BluetoothError error,std::list<std::string> filters) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(request, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		responseObj.put("returnValue", true);
		responseObj.put("instanceName", parseInstanceNameFromSessionKey(sessionKey));
		responseObj.put("filters", createJsonFilterList(filters));
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};

	getImpl<BluetoothMapProfile>(adapterAddress)->getMessageFilters(sessionKey, sessionId, getMessageFiltersCallback);
	return true;

}

pbnjson::JValue BluetoothMapProfileService::createJsonFilterList(const std::list<std::string> &filters)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto data = filters.begin(); data != filters.end(); data++)
	{
		platformObjArr.append(data->c_str());
	}
	return platformObjArr;
}

bool BluetoothMapProfileService::getMessageList(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!isGetMessageListSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMapProfile *impl = getImpl<BluetoothMapProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}
	std::string folder = requestObj["folder"].asString();

	BluetoothMapPropertiesList filters;

	if (requestObj.hasKey("filter"))
		addGetMessageFilters(requestObj,filters);

	impl->getMessageList(sessionKey, sessionId, folder, filters, std::bind(&BluetoothMapProfileService::getMessageListCallback,
			this,request,address, sessionKey, adapterAddress,_1,_2));
	return true;
}

bool BluetoothMapProfileService::isGetMessageListSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string),
								PROP(folder, string), OBJECT(filter, OBJSCHEMA_11(PROP(startOffset, integer), PROP(maxCount, integer),
								PROP(subjectLength, integer), PROP(periodBegin, string), PROP(periodEnd, string), PROP(recipient, string),
								PROP(sender, string), PROP(priority, boolean), PROP(read, boolean), ARRAY(fields, string), ARRAY(messageTypes, string))))
								REQUIRED_2(address,sessionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}
	return true;
}

void BluetoothMapProfileService::addGetMessageFilters(const pbnjson::JValue &requestObj, BluetoothMapPropertiesList &filters)
{
	auto filterObj = requestObj["filter"];

	for (const auto& filter: filterParam)
	{
		if(filterObj.hasKey(filter.first))
		{
			switch (filter.second)
			{
			case BluetoothMapProperty::Type::STARTOFFSET:
			case BluetoothMapProperty::Type::MAXCOUNT:
				filters.push_back(BluetoothMapProperty(filter.second, (uint16_t)filterObj[filter.first].asNumber<int32_t>()));
				break;
			case BluetoothMapProperty::Type::SUBJECTLENGTH:
				filters.push_back(BluetoothMapProperty(filter.second, (uint8_t)filterObj[filter.first].asNumber<int32_t>()));
				break;
			case BluetoothMapProperty::Type::PERIODBEGIN:
			case BluetoothMapProperty::Type::PERIODEND:
			case BluetoothMapProperty::Type::RECIPIENT:
			case BluetoothMapProperty::Type::SENDER:
				filters.push_back(BluetoothMapProperty(filter.second, filterObj[filter.first].asString()));
				break;
			case BluetoothMapProperty::Type::PRIORITY:
			case BluetoothMapProperty::Type::READ:
				filters.push_back(BluetoothMapProperty(filter.second, filterObj[filter.first].asBool()));
				break;
			case BluetoothMapProperty::Type::MESSAGETYPES:
			case BluetoothMapProperty::Type::FIELDS:
				std::vector<std::string> objValue;
				auto ObjArray = filterObj[filter.first];
				for (int n = 0; n < ObjArray.arraySize(); n++)
					objValue.push_back(ObjArray[n].asString());
				filters.push_back(BluetoothMapProperty(filter.second, objValue));
			}
		}
	}
}

void BluetoothMapProfileService::getMessageListCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error, BluetoothMessageList& messageList)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("instanceName", parseInstanceNameFromSessionKey(sessionKey));
	appendMessageList(responseObj,messageList);
	LSUtils::postToClient(request, responseObj);
}

void BluetoothMapProfileService::appendMessageList(pbnjson::JValue &responseObject , BluetoothMessageList& messageList)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	pbnjson::JValue messageValue = pbnjson::Array();
	for(auto message : messageList)
	{
		pbnjson::JValue messageObject = pbnjson::Object();
		messageObject.put("handle", message.first);
		pbnjson::JValue messageProperty = pbnjson::Object();
		for (auto property : message.second)
		{
			BT_INFO("MAP", 0, "Luna API is called : [%s : %d , %d]", __FUNCTION__, __LINE__,property.getType());
			switch (property.getType())
			{
			case BluetoothMapProperty::Type::FOLDER:
				messageProperty.put("folder",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::SUBJECT:
				messageProperty.put("subject",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::TIMESTAMP:
				messageProperty.put("dateTime",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::SENDER:
				messageProperty.put("senderName",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::SENDERADDRESS:
				messageProperty.put("senderAddress",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::RECIPIENT:
				messageProperty.put("recipientName",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::RECIPIENTADDRESS:
				messageProperty.put("recipientAddress",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::MESSAGETYPES:
				messageProperty.put("type",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::STATUS:
				messageProperty.put("status",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::PRIORITY:
				messageProperty.put("priority",(property.getValue<bool>()));
				break;
			case BluetoothMapProperty::Type::READ:
				messageProperty.put("read",(property.getValue<bool>()));
				break;
			case BluetoothMapProperty::Type::SENT:
				messageProperty.put("sent",(property.getValue<bool>()));
				break;
			case BluetoothMapProperty::Type::PROTECTED:
				messageProperty.put("protected",(property.getValue<bool>()));
				break;
			case BluetoothMapProperty::Type::TEXTTYPE:
				messageProperty.put("textType",(property.getValue<bool>()));
				break;
			default:
				break;
			}
		}
		messageObject.put("properties", messageProperty);
		messageValue.append(messageObject);
	}
	responseObject.put("messages",messageValue);
}

bool BluetoothMapProfileService::getFolderList(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!isGetFolderListSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMapProfile *impl = getImpl<BluetoothMapProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	uint16_t startIndex = 0;
	if (requestObj.hasKey("startOffset"))
		startIndex = (uint16_t)requestObj["startOffset"].asNumber<int32_t>();

	uint16_t maxListCount = 1024;
	if (requestObj.hasKey("maxListCount"))
		maxListCount = (uint16_t)requestObj["maxListCount"].asNumber<int32_t>();

	if (maxListCount > 1024)
		maxListCount = 1024;

	impl->getFolderList(sessionKey, sessionId, startIndex, maxListCount,
		std::bind(&BluetoothMapProfileService::getFolderCallback,
			this,request,address, sessionKey, adapterAddress,_1,_2));
	return true;
}

void BluetoothMapProfileService::getFolderCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error,std::vector<std::string>& folderList)
{
	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("instanceName", parseInstanceNameFromSessionKey(sessionKey));
	pbnjson::JValue folderValue = pbnjson::Array();
	for(auto folderName : folderList)
		folderValue.append(folderName.c_str());

	responseObj.put("folders", folderValue);
	LSUtils::postToClient(request, responseObj);
}

bool BluetoothMapProfileService::isGetFolderListSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string),
								PROP(startOffset, integer),PROP(maxListCount, integer))  REQUIRED_2(address,sessionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}
	return true;
}

bool BluetoothMapProfileService::setFolder(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!isSetFolderSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMapProfile *impl = getImpl<BluetoothMapProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	std::string folder = requestObj["folder"].asString();
	impl->setFolder(sessionKey, sessionId,folder,std::bind(&BluetoothMapProfileService::setFolderCallback,
			this,request,address, sessionKey, adapterAddress,_1));
	return true;
}

bool BluetoothMapProfileService::isSetFolderSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string),
								PROP(folder, string))  REQUIRED_3(address,sessionId,folder));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else if (!requestObj.hasKey("folder"))
			LSUtils::respondWithError(request, BT_ERR_MAP_FOLDER_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}
	return true;
}

void BluetoothMapProfileService::setFolderCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error)
{
	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, error);
		return;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("instanceName", parseInstanceNameFromSessionKey(sessionKey));
	LSUtils::postToClient(request, responseObj);
}


bool BluetoothMapProfileService::getMessage(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isGetMessageSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;
	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	std::string messageHandle = requestObj["handle"].asString();

	bool attachment = false;
	if (requestObj.hasKey("attachment"))
	{
		attachment = requestObj["attachment"].asBool();
	}

	std::string check_destinationFile;
	if (requestObj.hasKey("destinationFile"))
	{
		check_destinationFile = requestObj["destinationFile"].asString();
	}

	if(check_destinationFile.empty())
	{
		check_destinationFile = messageHandle;
	}

	std::string destinationFile = buildStorageDirPath(check_destinationFile, address);

	if (!checkPathExists(destinationFile)) {
		std::string errorMessage = "Supplied destination path ";
		errorMessage += destinationFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_DESTPATH_INVALID);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto getMessageCallback = [ = ](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(request, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		responseObj.put("returnValue", true);
		responseObj.put("destinationFile", destinationFile);
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};

	getImpl<BluetoothMapProfile>(adapterAddress)->getMessage(sessionKey, messageHandle, attachment, destinationFile, getMessageCallback);
	return true;
}

bool BluetoothMapProfileService::isGetMessageSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string), PROP(destinationFile, string), PROP(handle, string), PROP(attachment, boolean))  REQUIRED_3(address, handle, sessionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else if (!requestObj.hasKey("handle"))
			LSUtils::respondWithError(request, BT_ERR_MAP_HANDLE_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

std::string BluetoothMapProfileService::buildStorageDirPath(const std::string &path, const std::string &address)
{
	std::string result = WEBOS_MOUNTABLESTORAGEDIR;
	result += "/";
	result += "map";
	result += "/";
	result += replaceString(convertToLower(address),":","_");
	result += "/";
	if (g_mkdir_with_parents(result.c_str(), 0755) != 0)
	{
		BT_DEBUG("failed to create folder: %s", result.c_str());
	}
	result += path;
	return result;
}

bool BluetoothMapProfileService::setMessageStatus(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isSetMessageStatusSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;

	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	std::string messageHandle = requestObj["handle"].asString();
	std::string statusIndicator = requestObj["statusIndicator"].asString();
	bool statusValue = requestObj["statusValue"].asBool();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto setMessageStatusCallback = [ = ](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(request, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		responseObj.put("returnValue", true);
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};
	getImpl<BluetoothMapProfile>(adapterAddress)->setMessageStatus(sessionKey, messageHandle, statusIndicator, statusValue, setMessageStatusCallback);
	return true;
}

bool BluetoothMapProfileService::isSetMessageStatusSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string), PROP(handle, string), PROP(statusIndicator, string), PROP(statusValue, boolean))  REQUIRED_5(address, handle, statusIndicator, sessionId, statusValue));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else if (!requestObj.hasKey("handle"))
			LSUtils::respondWithError(request, BT_ERR_MAP_HANDLE_PARAM_MISSING);
		else if (!requestObj.hasKey("statusIndicator"))
			LSUtils::respondWithError(request, BT_ERR_MAP_STATUS_INDICATOR_PARAM_MISSING);
		else if (!requestObj.hasKey("statusValue"))
			LSUtils::respondWithError(request, BT_ERR_MAP_STATUS_VALUE_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

bool BluetoothMapProfileService::pushMessage(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isPushMessageSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;

	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	std::string sourceFile = requestObj["sourceFile"].asString();
	std::string folder = requestObj["folder"].asString();

	std::string charset = "utf8";
	if (requestObj.hasKey("charset"))
	{
		charset = requestObj["charset"].asString();
	}

	bool transparent = false;
	if (requestObj.hasKey("transparent"))
	{
		transparent =  requestObj["transparent"].asBool();
	}

	bool retry = true;
	if (requestObj.hasKey("retry"))
	{
		retry =  requestObj["retry"].asBool();
	}

	if (!checkFileIsValid(sourceFile)) {
		std::string errorMessage = "Supplied file ";
		errorMessage += sourceFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_SRCFILE_INVALID);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto pushMessageCallback = [ = ](BluetoothError error, const std::string &messageHandle) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(request, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		responseObj.put("returnValue", true);
		responseObj.put("handle", messageHandle);
		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};
	getImpl<BluetoothMapProfile>(adapterAddress)->pushMessage(sessionKey, sourceFile, folder, charset, transparent, retry, pushMessageCallback);
	return true;
}

bool BluetoothMapProfileService::isPushMessageSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_8(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string), PROP(sourceFile, string), PROP(folder, string), PROP(transparent, boolean), PROP(retry, boolean), PROP(charset, string))  REQUIRED_4(address, sourceFile, folder, sessionId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else if (!requestObj.hasKey("sourceFile"))
			LSUtils::respondWithError(request, BT_ERR_SRCFILE_PARAM_MISSING);
		else if (!requestObj.hasKey("folder"))
			LSUtils::respondWithError(request, BT_ERR_MAP_FOLDER_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

bool BluetoothMapProfileService::getMessageNotification(LSMessage &message)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	std::string adapterAddress;

	if (!isGetMessageNotificationSchemaAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();
	std::string sessionKey;

	if(!isSessionIdValid(adapterAddress, address, sessionId, sessionKey))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}

	LS::SubscriptionPoint *subscriptionPoint = 0;

	auto subscriptionsIter = mNotificationPropertiesSubscriptionsForMultipleAdapters.find(adapterAddress);
	if (subscriptionsIter == mNotificationPropertiesSubscriptionsForMultipleAdapters.end())
	{
		std::map<std::string, LS::SubscriptionPoint*> subscriptions;
		subscriptionPoint = new LS::SubscriptionPoint;
		subscriptionPoint->setServiceHandle(getManager());
		subscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(sessionId, subscriptionPoint));

		mNotificationPropertiesSubscriptionsForMultipleAdapters.insert(std::pair<std::string, std::map<std::string, LS::SubscriptionPoint*>>(adapterAddress, subscriptions));

	}
	else
	{
		auto subscriptionIter = (subscriptionsIter->second).find(sessionId);
		if (subscriptionIter == (subscriptionsIter->second).end())
		{
			std::map<std::string, LS::SubscriptionPoint*> subscriptions;
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			(subscriptionsIter->second).insert(std::pair<std::string, LS::SubscriptionPoint*>(sessionId, subscriptionPoint));

		}
		else
		{
			subscriptionPoint = subscriptionIter->second;
		}
	}

	subscriptionPoint->subscribe(request);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("sessionId", sessionId);
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	LSUtils::postToClient(request, responseObj);
	return true;
}

void BluetoothMapProfileService::handleMessageNotificationClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey)
{
	std::string sessionId = getSessionId(adapterAddress, sessionKey);
	std::string address = parseAddressFromSessionKey(sessionKey);
	auto subscriptionsIter = mNotificationPropertiesSubscriptionsForMultipleAdapters.find(adapterAddress);
	if (subscriptionsIter == mNotificationPropertiesSubscriptionsForMultipleAdapters.end())
		return;
	auto subscriptionIter = (subscriptionsIter->second).find(sessionId);
	if (subscriptionIter == (subscriptionsIter->second).end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	responseObj.put("sessionId", sessionId);
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", false);
	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);

	if(subscriptionPoint)
	{
		(subscriptionsIter->second).erase(sessionId);
		delete subscriptionPoint;
	}
}

bool BluetoothMapProfileService::isGetMessageNotificationSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string), PROP(subscribe, boolean), PROP_WITH_VAL_1(subscribe, boolean, true))  REQUIRED_3(address, sessionId, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else if (!requestObj.hasKey("subscribe"))
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("sessionId"))
			LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	if(!requiredCheckForMapProfile(request, requestObj, adapterAddress))
		return false;

	return true;
}

void BluetoothMapProfileService::messageNotificationEvent(const std::string &adapterAddress, const std::string &sessionId, BluetoothMessageList &messageList)
{
	BT_INFO("MAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	notifySubscribersAboutPropertiesChange(adapterAddress, sessionId, messageList);
}

void BluetoothMapProfileService::notifySubscribersAboutPropertiesChange(const std::string &adapterAddress, const std::string &sessionId, BluetoothMessageList &messageList)
{
	BT_INFO("MAP_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	std::string address = parseAddressFromSessionKey(findSessionKey(adapterAddress, sessionId));
	auto subscriptionsIter = mNotificationPropertiesSubscriptionsForMultipleAdapters.find(adapterAddress);
	if (subscriptionsIter == mNotificationPropertiesSubscriptionsForMultipleAdapters.end())
		return;
	auto subscriptionIter = (subscriptionsIter->second).find(sessionId);
	if (subscriptionIter == (subscriptionsIter->second).end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);
	appendNotificationEvent(responseObj,messageList);
	responseObj.put("subscribed", true);
	responseObj.put("sessionId", sessionId);
	responseObj.put("returnValue", true);
	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

void BluetoothMapProfileService::appendNotificationEvent(pbnjson::JValue &responseObject , BluetoothMessageList& messageList)
{
	BT_INFO("MAP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	std::string notificationType;
	pbnjson::JValue messageObject = pbnjson::Object();
	for(auto message : messageList)
	{
		pbnjson::JValue newMessageObject = pbnjson::Object();
		messageObject.put("handle", message.first);
		pbnjson::JValue messageProperty = pbnjson::Object();
		for (auto property : message.second)
		{
			BT_INFO("MAP", 0, "Luna API is called : [%s : %d , %d]", __FUNCTION__, __LINE__,property.getType());
			switch (property.getType())
			{
			case BluetoothMapProperty::Type::EVENTTYPE:
				notificationType = property.getValue<std::string>();
				responseObject.put("notificationType", notificationType);
				break;
			case BluetoothMapProperty::Type::FOLDER:
				messageProperty.put("folder",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::OLDFOLDER:
				messageProperty.put("oldFolder",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::MESSAGETYPES:
				messageProperty.put("type",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::SENDER:
				newMessageObject.put("sender",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::SUBJECT:
				newMessageObject.put("subject",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::TIMESTAMP:
				newMessageObject.put("dateTime",(property.getValue<std::string>()));
				break;
			case BluetoothMapProperty::Type::PRIORITY:
				newMessageObject.put("priority",(property.getValue<bool>()));
				break;
			default:
				break;
			}
		}
		if(notificationType == "NewMessage")
			messageProperty.put("newMessageProperties",newMessageObject);
		messageObject.put("properties", messageProperty);
	}
	responseObject.put("messages",messageObject);
}


std::string BluetoothMapProfileService::findSessionKey(const std::string &adapterAddress, const std::string &sessionId)
{
	std::string sessionKey;
	auto connectedDevicesiter = mConnectedDevicesForMultipleAdaptersWithSessionKey.find(adapterAddress);
	if (connectedDevicesiter != mConnectedDevicesForMultipleAdaptersWithSessionKey.end())
	{
		std::map<std::string, std::string> connectedDevicesWithSessonKey = connectedDevicesiter->second;
		for (auto it = connectedDevicesWithSessonKey.begin(); it != connectedDevicesWithSessonKey.end(); ++it)
		{
			if (it->second == sessionId)
			{
				sessionKey = it->first;
				return sessionKey;
			}
		}
	}
	return sessionKey;
}
