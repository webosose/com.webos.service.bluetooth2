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

BluetoothMapProfileService::BluetoothMapProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "MAP", "00001132-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, connect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMASInstances)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMessageFilters)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getFolderList)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, setFolder)
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

	BluetoothDevice *device = getManager()->findDevice(adapterAddress,deviceAddress);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (!BluetoothProfileService::isDevicePaired(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return false;
	}
	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothPbapProfile>(adapterAddress))
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

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothMapProfile>(adapterAddress))
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
