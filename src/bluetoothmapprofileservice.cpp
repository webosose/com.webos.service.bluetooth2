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

BluetoothMapProfileService::BluetoothMapProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "MAP", "00001132-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, connect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothMapProfileService, getMASInstances)
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

pbnjson::JValue BluetoothMapProfileService::appendMasInstances(const std::string &deviceAddress)
{
	std::map<std::string, std::vector<std::string>> mapInstancesSupports;
	pbnjson::JValue platformObjArr = pbnjson::Array();

	BluetoothDevice *device = getManager()->findDevice(deviceAddress);
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
	responseObj.put("masInstances", appendMasInstances(deviceAddress));
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

	std::string address = requestObj["address"].asString();

	if (!BluetoothProfileService::isDevicePaired(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return false;
	}

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

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

bool BluetoothMapProfileService::isInstanceNameValid(const std::string &instance, const std::string &deviceAddress)
{
	std::map<std::string, std::vector<std::string>> mapInstancesSupports;

	BluetoothDevice *device = getManager()->findDevice(deviceAddress);
	if (device)
	{
		mapInstancesSupports = device->getSupportedMessageTypes();
		auto it = mapInstancesSupports.find(instance);
		if(it != mapInstancesSupports.end())
			return true;
	}
	return false;
}

bool BluetoothMapProfileService::isSessionIdValid(const std::string &sessionId)
{

	auto connectedDevicesiter = mConnectedDevicesWithSessionId.find(sessionId);
	if (connectedDevicesiter != mConnectedDevicesWithSessionId.end())
	{
		return true;
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
		if( !isInstanceNameValid(instanceName, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_MAP_INSTANCE_NOT_EXIST);
			return true;
		}
	}
	else
	{
		std::map<std::string, std::vector<std::string>> mapInstancesSupports;
		BluetoothDevice *device = getManager()->findDevice(deviceAddress);
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
			LSUtils::respondWithError(request, error);
			LSMessageUnref(request.get());
		}
		else
		{
			markDeviceAsConnectedWithSessionId(sessionId, sessionKey);
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

	getImpl<BluetoothMapProfile>(adapterAddress)->connect(deviceAddress, instanceName, connectCallback);
	return true;
}

std::string BluetoothMapProfileService::getSessionId(const std::string &sessionKey)
{
	std::string sessionId;
	for (auto it = mConnectedDevicesWithSessionId.begin(); it != mConnectedDevicesWithSessionId.end(); ++it)
	{
		if (it->second == sessionKey)
		{
			sessionId = it->first;
			break;
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
		LSUtils::ClientWatch *watch = watchIter->second;
		(watchesIter->second).erase(watchIter);
		delete watch;
	};

	std::string address = parseAddressFromSessionKey(sessionKey);
	std::string sessionId = getSessionId(sessionKey);

	if(!sessionId.empty())
	{
		removeDeviceAsConnectedWithSessionId(sessionId);
		getImpl<BluetoothMapProfile>(adapterAddress)->disconnect(address, sessionId, disconnectCallback);
	}
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

	std::string address = requestObj["address"].asString();

	if (!BluetoothProfileService::isDevicePaired(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED);
		return false;
	}

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothMapProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}

	return true;
}

void BluetoothMapProfileService::markDeviceAsConnectedWithSessionId(const std::string &sessionId, const std::string &sessionKey)
{
	auto connectedDevicesiter = mConnectedDevicesWithSessionId.find(sessionId);
	if (connectedDevicesiter == mConnectedDevicesWithSessionId.end())
	{
		mConnectedDevicesWithSessionId.insert(std::pair<std::string, std::string>(sessionId, sessionKey));
	}
}

void BluetoothMapProfileService::removeDeviceAsConnectedWithSessionId(const std::string &sessionId)
{
	auto connectedDevicesiter = mConnectedDevicesWithSessionId.find(sessionId);
	if (connectedDevicesiter != mConnectedDevicesWithSessionId.end())
	{
		mConnectedDevicesWithSessionId.erase(connectedDevicesiter);
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

	if (!isDisconnectSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	std::string address = requestObj["address"].asString();
	std::string sessionId = requestObj["sessionId"].asString();

	if( !isSessionIdValid(sessionId))
	{
		LSUtils::respondWithError(request, BT_ERR_MAP_SESSION_ID_NOT_EXIST);
		return true;
	}
	auto connectedDevicesiter = mConnectedDevicesWithSessionId.find(sessionId);
	std::string sessionKey = connectedDevicesiter->second;
	if (sessionKey.find(address) == std::string::npos)
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
		removeDeviceAsConnectedWithSessionId(sessionId);
		removeConnectWatchForDevice(address, adapterAddress, sessionKey, sessionId, true, false);
		LSMessageUnref(request.get());

	};
	getImpl<BluetoothMapProfile>(adapterAddress)->disconnect(address, sessionId, disconnectCallback);
	return true;
}

bool BluetoothMapProfileService::isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string), PROP(sessionId, string))  REQUIRED_1(address));

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