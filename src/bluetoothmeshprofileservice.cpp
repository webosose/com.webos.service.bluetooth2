// Copyright (c) 2021 LG Electronics, Inc.
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

#include "bluetoothmeshprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothmanageradapter.h"
#include "bluetoothdevice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "bluetoothclientwatch.h"

BluetoothMeshProfileService::BluetoothMeshProfileService(BluetoothManagerService *manager) :
BluetoothProfileService(manager, "MESH", "00001827-0000-1000-8000-00805f9b34fb"),
mNetworkCreated(0),
mAppKeyIndex(0)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, scanUnprovisionedDevices)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, unprovisionedScanCancel)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, createNetwork)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, provision)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, supplyProvisioningOob)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, supplyProvisioningNumeric)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, createAppKey)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, modelconfig)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, get)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, set)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/mesh", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/mesh", this);

	//TODO: Get appkey index and application mappings from db and update mAppKeyIndex
	manager->registerCategory("/mesh/model/config", LS_CATEGORY_TABLE_NAME(modelconfig), NULL, NULL);
	manager->setCategoryData("/mesh/model/config", this);
}

BluetoothMeshProfileService::~BluetoothMeshProfileService()
{
}

void BluetoothMeshProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
	{
		getImpl<BluetoothMeshProfile>()->registerObserver(this);
	}
}

void BluetoothMeshProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
	{
		getImpl<BluetoothMeshProfile>(adapterAddress)->registerObserver(this);
	}

	pbnjson::JValue result;
	LSUtils::callDb8MeshGetAppKeys(getManager(), result);
	pbnjson::JValue results = result["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if (meshEntry.hasKey("appKey"))
			{
				uint16_t appKeyIndex = (uint16_t)meshEntry["appKey"].asNumber<int32_t>();
				std::string appName = meshEntry["appName"].asString();
				BT_DEBUG("MESH", 0, "appkey: %d, appname: %s", appKeyIndex, appName.c_str());
				mAppKeys.insert(std::pair<uint16_t, std::string>(appKeyIndex, appName));
			}
		}

		/* Keep the app key index to next available index */
		while (isAppKeyExist(mAppKeyIndex))
		{
			mAppKeyIndex++;
		}
	}
}

bool BluetoothMeshProfileService::isValidApplication(uint16_t appKeyIndex, LS::Message &request)
{
	const char *senderName = LSMessageGetApplicationID(request.get());
	if (senderName == NULL)
	{
		senderName = LSMessageGetSenderServiceName(request.get());
		if (senderName == NULL)
		{
			return false;
		}
	}

	/* If app index exists and the sender name is same as the stored app name, then
	 * valid application
	 */
	auto appIter = mAppKeys.find(appKeyIndex);
	if (appIter != mAppKeys.end() && appIter->second == senderName)
	{
		return true;
	}
	return false;
}

bool BluetoothMeshProfileService::addClientWatch(LS::Message &request, std::list<BluetoothClientWatch *> *clientWatch,
												 std::string adapterAddress, std::string deviceAddress)
{
	const char *senderName = LSMessageGetApplicationID(request.get());
	if (senderName == NULL)
	{
		senderName = LSMessageGetSenderServiceName(request.get());
		if (senderName == NULL)
		{
			return false;
		}
	}
	auto watch = new BluetoothClientWatch(getManager()->get(), request.get(),
										  std::bind(&BluetoothMeshProfileService::handleClientDisappeared,
													this, clientWatch, senderName),
										  adapterAddress, deviceAddress);
	clientWatch->push_back(watch);
	return true;
}

void BluetoothMeshProfileService::handleClientDisappeared(std::list<BluetoothClientWatch *> *clientWatch,
														  const std::string senderName)
{
	removeClientWatch(clientWatch, senderName);
}

void BluetoothMeshProfileService::removeClientWatch(std::list<BluetoothClientWatch *> *clientWatch,
													const std::string &senderName)
{
	auto watch = clientWatch->begin();
	while (watch != clientWatch->end())
	{

		const char *senderNameWatch = LSMessageGetApplicationID((*watch)->getMessage());
		if (senderNameWatch == NULL)
		{
			senderNameWatch = LSMessageGetSenderServiceName((*watch)->getMessage());
			if (senderNameWatch == NULL)
			{
				return;
			}
		}

		if (senderName == senderNameWatch)
		{
			auto watchToRemove = watch;
			delete (*watchToRemove);
			watch = clientWatch->erase(watch);
		}
		else
		{
			++watch;
		}
	}
}

bool BluetoothMeshProfileService::scanUnprovisionedDevices(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string),
	PROP(bearer, string), PROP(scanTimeout, integer), PROP(subscribe, boolean)) REQUIRED_1( subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}
	/* Need not check if the method is subscribed. This method need to be
		* subscribed, else schema check will return error
		*/
	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addClientWatch(request, &mScanResultWatch,
																adapterAddress, "");
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
	}

	std::string bearer = "PB-ADV"; // default value

	if (requestObj.hasKey("bearer"))
		bearer = requestObj["bearer"].asString();

	uint16_t scanTimeout = 20; // default value

	if (requestObj.hasKey("scanTimeout"))
		scanTimeout = (uint16_t)requestObj["scanTimeout"].asNumber<int32_t>();

	BluetoothError error = impl->scanUnprovisionedDevices(bearer, scanTimeout);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("devices", appendDevices(adapterAddress));
	LSUtils::postToClient(request, responseObj);
	return true;
}

void BluetoothMeshProfileService::setModelConfigResult(const std::string &adapterAddress, BleMeshConfiguration &configuration, BluetoothError error)
{
	BT_INFO("MESH", 0, "[%s : %d], mSetModelConfigResultWatch: %d", __FUNCTION__, __LINE__, mSetModelConfigResultWatch.size());
	for (auto watch : mSetModelConfigResultWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			if (BLUETOOTH_ERROR_NONE != error)
			{
				LSUtils::respondWithError(watch->getMessage(), error);
				return;
			}
			pbnjson::JValue object = pbnjson::Object();
			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			object.put("config", configuration.getConfig());
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothMeshProfileService::modelConfigResult(const std::string &adapterAddress, BleMeshConfiguration &configuration, BluetoothError error)
{

	BT_INFO("MESH", 0, "[%s : %d], getConfig: %s", __FUNCTION__, __LINE__, configuration.getConfig().c_str());

	setModelConfigResult(adapterAddress, configuration, error);

	for (auto watch : mGetModelConfigResultWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			if (BLUETOOTH_ERROR_NONE != error)
			{
				LSUtils::respondWithError(watch->getMessage(), error);
				return;
			}
			std::string config(configuration.getConfig());
			pbnjson::JValue object = pbnjson::Object();
			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			object.put("config", configuration.getConfig());

			if (config == "DEFAULT_TTL")
			{
				object.put("ttl", configuration.getTTL());
			}
			else if (config == "GATT_PROXY")
			{
				object.put("gattProxyState", configuration.getGattProxyState());
			}
			else if (config == "RELAY")
			{
				object.put("relayStatus", appendRelayStatus(configuration.getRelayStatus()));
			}
			else if (config == "APPKEYINDEX")
			{
				object.put("appKeyIndexes", appendAppKeyIndexes(configuration.getAppKeyIndexes()));
			}
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

pbnjson::JValue BluetoothMeshProfileService::appendRelayStatus(BleMeshRelayStatus relayStatus)
{
	pbnjson::JValue object = pbnjson::Object();
	object.put("relay", relayStatus.getRelay());
	object.put("retransmitCount", relayStatus.getrelayRetransmitCount());
	object.put("retransmitIntervalSteps", relayStatus.getRelayRetransmitIntervalSteps());
	return object;
}

pbnjson::JValue BluetoothMeshProfileService::appendAppKeyIndexes(std::vector<uint16_t> appKeyList)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto i = appKeyList.begin(); i != appKeyList.end(); ++i)
	{
		platformObjArr.append(*i);
	}
	return platformObjArr;
}

void BluetoothMeshProfileService::scanResult(const std::string &adapterAddress,
											 const int16_t rssi, const std::string &uuid, const std::string &name)
{
	BT_INFO("MESH", 0, "[%s : %d], num_watch: %d", __FUNCTION__, __LINE__, mScanResultWatch.size());
	for (auto watch : mScanResultWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			updateDeviceList(adapterAddress, rssi, uuid, name);
			pbnjson::JValue object = pbnjson::Object();
			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			object.put("device", appendDevice(rssi, uuid, name));
			object.put("devices", appendDevices(adapterAddress));
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

bool BluetoothMeshProfileService::isScanDevicePresent(const std::string &adapterAddress, const std::string &uuid)
{
	auto it = mUnprovisionedDevices.find(adapterAddress);
	if (mUnprovisionedDevices.end() != it)
	{
		auto device = (it->second).find(uuid);
		if ((it->second).end() != device)
		{
			return true;
		}
	}
	return false;
}

bool BluetoothMeshProfileService::updateDeviceList(const std::string &adapterAddress, const int16_t rssi, const std::string &uuid, const std::string &name)
{

	auto it = mUnprovisionedDevices.find(adapterAddress);
	UnprovisionedDeviceInfo deviceInfo(rssi, uuid, name);

	if (mUnprovisionedDevices.end() == it)
	{
		std::map<std::string, UnprovisionedDeviceInfo> device;
		device.insert(std::pair<std::string, UnprovisionedDeviceInfo>(uuid, deviceInfo));
		mUnprovisionedDevices.insert(std::pair<std::string, std::map<std::string, UnprovisionedDeviceInfo>>(adapterAddress, device));
	}
	else
	{
		auto devices = (it->second).find(uuid);
		if ((it->second).end() != devices)
		{
			(it->second).erase(uuid);
		}
		(it->second).insert(std::pair<std::string, UnprovisionedDeviceInfo>(uuid, deviceInfo));
	}
	return true;
}

bool BluetoothMeshProfileService::removeFromDeviceList(const std::string &adapterAddress, const std::string &uuid)
{
	auto it = mUnprovisionedDevices.find(adapterAddress);

	if (mUnprovisionedDevices.end() != it)
	{
		auto devices = (it->second).find(uuid);
		if ((it->second).end() != devices)
		{
			(it->second).erase(uuid);
		}
	}
	return true;
}

pbnjson::JValue BluetoothMeshProfileService::appendDevice(const int16_t rssi, const std::string &uuid, const std::string &name)
{
	pbnjson::JValue object = pbnjson::Object();

	object.put("uuid", uuid);
	object.put("rssi", rssi);
	if(!name.empty())
		object.put("name", name);

	return object;
}

pbnjson::JValue BluetoothMeshProfileService::appendDevices(const std::string &adapterAddress)
{
	pbnjson::JValue platformObjArr = pbnjson::Array();


	auto it = mUnprovisionedDevices.find(adapterAddress);
	if (mUnprovisionedDevices.end() != it)
	{
		auto test = it->second;
		for (auto devices = test.begin(); devices != test.end(); devices++)
		{
			UnprovisionedDeviceInfo device = devices->second;
			platformObjArr.append(appendDevice(device.rssi, device.uuid, device.name));
		}
	}

	return platformObjArr;
}


bool BluetoothMeshProfileService::unprovisionedScanCancel(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string),
	PROP(bearer, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
	return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string bearer = "PB-ADV"; // default value

	if (requestObj.hasKey("bearer"))
		bearer = requestObj["bearer"].asString();

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	BluetoothError error = impl->unprovisionedScanCancel(bearer);
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothMeshProfileService::createNetwork(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string),
	PROP(bearer, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
	return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (isNetworkCreated())
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_NETWORK_EXISTS);
		return true;
	}

	std::string meshToken;
	bool networkTokenExists = LSUtils::callDb8MeshFindToken(getManager(), meshToken);
	if (networkTokenExists)
	{
		BT_INFO("MESH", 0, "network already exists, token : %s: [%s : %d]", meshToken.c_str(),
				 __FUNCTION__, __LINE__);
		mNetworkCreated = true;
		impl->attach("PB-ADV", meshToken);
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_NETWORK_EXISTS);
		return true;
	}

	bool retVal = addClientWatch(request, &mNetworkIdWatch,
															adapterAddress, "");
	if (!retVal)
	{
		LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
		return true;
	}
	std::string bearer = "PB-ADV"; // default value

	if (requestObj.hasKey("bearer"))
		bearer = requestObj["bearer"].asString();

	BluetoothError error = impl->createNetwork(bearer);
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	mNetworkCreated = true;

	return true;
}

void BluetoothMeshProfileService::updateNetworkId(const std::string &adapterAddress,
											 const uint64_t networkId)
{
	BT_INFO("MESH", 0, "[%s : %d], num_watch: %d", __FUNCTION__, __LINE__, mNetworkIdWatch.size());
	for (auto watch : mNetworkIdWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			pbnjson::JValue object = pbnjson::Object();
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			BT_INFO("MESH", 0, "networkId : [%s : %llu]", __FUNCTION__, networkId);
			std::string networkID = std::to_string(networkId);
			object.put("networkId", networkID);
			if (!LSUtils::callDb8MeshSetToken(getManager(), networkID))
			{
				BT_ERROR("MESH", 0, "Db8 set mesh token failed");
			}
			else
			{
				BT_DEBUG("MESH", 0, "Db8 set mesh token success");
			}
			LSUtils::postToClient(watch->getMessage(), object);


		}
	}
}

bool BluetoothMeshProfileService::provision(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(adapterAddress, string), PROP(timeout, integer),
													 PROP(bearer, string), PROP(uuid, string),
													 PROP_WITH_VAL_1(subscribe, boolean, true))
												 REQUIRED_2(uuid, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("uuid"))
			LSUtils::respondWithError(request, BT_ERR_MESH_UUID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	/* Need not check if the method is subscribed. This method need to be
     * subscribed, else schema check will return error
     */
	bool retVal = addClientWatch(request, &mProvResultWatch,
								 adapterAddress, "");
	if (!retVal)
	{
		LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
		return true;
	}

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	uint16_t timeout = 60;

	if (requestObj.hasKey("timeout"))
	{
		timeout = (uint16_t)requestObj["timeout"].asNumber<int32_t>();
	}

	BluetoothError error = impl->provision(bearer, requestObj["uuid"].asString(), timeout);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMeshProfileService::supplyProvisioningOob(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(bearer, string), PROP(oobData, string))
												 REQUIRED_1(oobData));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	BluetoothError error = impl->supplyProvisioningOob(bearer,
											  requestObj["oobData"].asString());

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMeshProfileService::supplyProvisioningNumeric(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(bearer, string), PROP(number, integer))
												 REQUIRED_1(number));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	BluetoothError error = impl->supplyProvisioningNumeric(bearer, requestObj["number"].asNumber<int32_t>());

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);
	return true;
}

void BluetoothMeshProfileService::provisionResult(BluetoothError error, const std::string &adapterAddress,
								 const std::string &request,
								 const std::string &stringToDisplay,
								 const uint32_t numberToDisplay,
								 const std::string &numberDisplayType,
								 const std::string &promptType,
								 uint16_t unicastAddress,
								 const std::string &uuid)
{
	BT_INFO("MESH", 0, "[%s : %d], num_watch: %d", __FUNCTION__, __LINE__, mProvResultWatch.size());
	for (auto watch : mProvResultWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			LSMessage *message = watch->getMessage();
			std::string payload = LSMessageGetPayload(message);
			pbnjson::JValue replyObj = pbnjson::Object();

			if (!LSUtils::parsePayload(LSMessageGetPayload(message), replyObj))
			{
				BT_ERROR("MESH", 0, "provision payload pasing error");
			}
			std::string deviceUUID = replyObj["uuid"].asString();

			pbnjson::JValue object = pbnjson::Object();

			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			object.put("request", request);
			 if (request == "promptNumeric")
			{
				object.put("promptType", promptType);
			}
			else if (request == "displayString")
			{
				object.put("stringToDisplay", stringToDisplay);
			}
			else if (request == "displayNumeric")
			{
				object.put("numberToDisplay", (int32_t)numberToDisplay);
				object.put("numberDisplayType", numberDisplayType);
			}
			else if (request == "endProvision" )
			{
				if (BLUETOOTH_ERROR_NONE == error)
				{
					object.put("unicastAddress", unicastAddress);
					removeFromDeviceList(adapterAddress, deviceUUID);
				}
			}
			if (BLUETOOTH_ERROR_NONE != error)
			{
				object.put("errorCode", error);
				object.put("errorText", retrieveErrorCodeText(error));
			}
			object.put("uuid", deviceUUID);
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

bool BluetoothMeshProfileService::createAppKey(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string),
													 PROP(bearer, string), PROP(netKeyIndex, integer),
													 PROP(appKeyIndex, integer)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	const char *senderName = LSMessageGetApplicationID(request.get());
	if (senderName == NULL)
	{
		senderName = LSMessageGetSenderServiceName(request.get());
		if (senderName == NULL)
		{
			return false;
		}
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	uint16_t netKeyIndex = 0;
	uint16_t appKeyIndex = mAppKeyIndex++;
	if (requestObj.hasKey("netKeyIndex"))
	{
		netKeyIndex = (uint16_t)requestObj["netKeyIndex"].asNumber<int32_t>();
	}
	/* If appkey index is passed, then we use that. If it is not passed then we use
	 * from the pool and increment the index so that new index is available in the pool
	 * for next time
	 */
	if (requestObj.hasKey("appKeyIndex"))
	{
		appKeyIndex = (uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>();
		if (isAppKeyExist(appKeyIndex))
		{
			BT_INFO("MESH", 0, "AppKey already exist, choose another key");
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_APP_KEY_INDEX_ALREADY_EXISTS);
			return true;
		}
	}
	else
	{
		/* Increment until a non existing index. */
		while (isAppKeyExist(mAppKeyIndex))
		{
			mAppKeyIndex++;
		}
		BT_INFO("MESH", 0, "Next available appkeyindex: %d", mAppKeyIndex);
	}
	mAppKeyIndex++;

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	BluetoothError error = impl->createAppKey(bearer,
											  netKeyIndex, appKeyIndex);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	if (!LSUtils::callDb8MeshPutAppKey(getManager(), appKeyIndex, senderName))
	{
		BT_INFO("MESH", 0, "Db8 put appkey failed");
	}
	mAppKeys.insert(std::pair<uint16_t, std::string>(appKeyIndex, senderName));
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("netKeyIndex", netKeyIndex);
	responseObj.put("appKeyIndex", appKeyIndex);

	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMeshProfileService::isAppKeyExist(uint16_t appKeyIndex)
{
	return (mAppKeys.find(appKeyIndex) != mAppKeys.end());
}

bool BluetoothMeshProfileService::get(LSMessage &message)
{

	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string),
													PROP(bearer, string), PROP(destAddress, integer),
													PROP_WITH_VAL_1(subscribe, boolean, true),
													PROP(config, string),
													PROP(netKeyIndex, integer))
												 REQUIRED_2(destAddress, config));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("destAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("config"))
			LSUtils::respondWithError(request, BT_ERR_MESH_CONFIG_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string config = requestObj["config"].asString();

	if(config == "APPKEYINDEX")
	{
		if (!requestObj.hasKey("netKeyIndex"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_NET_KEY_INDEX_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addClientWatch(request, &mGetModelConfigResultWatch,
																adapterAddress, "");
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
	}

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	BluetoothError error = impl->configGet(bearer, requestObj["destAddress"].asNumber<int32_t>(), config, requestObj["netKeyIndex"].asNumber<int32_t>());

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", requestObj["subscribe"].asBool());
	LSUtils::postToClient(request, responseObj);
	return true;
}

bool BluetoothMeshProfileService::set(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	BleMeshRelayStatus relayStatus;

	const std::string schema = STRICT_SCHEMA(PROPS_11(PROP(adapterAddress, string),
													PROP(bearer, string), PROP(destAddress, integer),
													PROP_WITH_VAL_1(subscribe, boolean, true),
													PROP(config, string),
													PROP(netKeyIndex, integer), PROP(appKeyIndex, integer),
													PROP(modelId, integer), PROP(ttl, integer), PROP(gattProxyState, integer),
													OBJECT(relayStatus, OBJSCHEMA_3(PROP(relay, integer), PROP(retransmitCount, integer), PROP(retransmitIntervalSteps, integer))))
													REQUIRED_2(destAddress, config));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("destAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("config"))
			LSUtils::respondWithError(request, BT_ERR_MESH_CONFIG_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string config = requestObj["config"].asString();

	if(config == "APPKEY_ADD" || config == "APPKEY_UPDATE")
	{
		if (!requestObj.hasKey("netKeyIndex"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_NET_KEY_INDEX_PARAM_MISSING);
			return true;
		}
	}

	if(config == "APPKEY_ADD" || config == "APPKEY_UPDATE" || config == "APPKEY_BIND")
	{
		if (!requestObj.hasKey("appKeyIndex"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_PARAM_MISSING);
			return true;
		}
	}

	if(config == "APPKEY_BIND")
	{
		if (!requestObj.hasKey("modelId"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_MODELID_PARAM_MISSING);
			return true;
		}
	}

	if(config == "DEFAULT_TTL")
	{
		if (!requestObj.hasKey("ttl"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_TTL_PARAM_MISSING);
			return true;
		}
	}

	if(config == "GATT_PROXY")
	{
		if (!requestObj.hasKey("gattProxyState"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_GATT_PROXY_STATE_PARAM_MISSING);
			return true;
		}
	}

	if(config == "RELAY")
	{
		if (!requestObj.hasKey("relayStatus"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_RELAY_STATUS_PARAM_MISSING);
			return true;
		}

		auto relayStatusObj = requestObj["relayStatus"];

		if(!relayStatusObj.hasKey("relay"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_RELAY_STATUS_PARAM_MISSING);
			return true;
		}
		else if(!relayStatusObj.hasKey("retransmitCount"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_RETRANSMIT_COUNT_PARAM_MISSING);
			return true;
		}
		else if(!relayStatusObj.hasKey("retransmitIntervalSteps"))
		{
			LSUtils::respondWithError(request, BT_ERR_MESH_RETRANSMIT_INTERVAL_STEPS_PARAM_MISSING);
			return true;
		}

		relayStatus.setRelay(relayStatusObj["relay"].asNumber<int32_t>());
		relayStatus.setRelayRetransmitIntervalSteps(relayStatusObj["retransmitIntervalSteps"].asNumber<int32_t>());
		relayStatus.setRelayRetransmitCount(relayStatusObj["retransmitCount"].asNumber<int32_t>());
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addClientWatch(request, &mSetModelConfigResultWatch,
																adapterAddress, "");
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
	}

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	BluetoothError error = impl->configSet(bearer, requestObj["destAddress"].asNumber<int32_t>(), config,
											requestObj["gattProxyState"].asNumber<int32_t>(),
											requestObj["netKeyIndex"].asNumber<int32_t>(),
											requestObj["appKeyIndex"].asNumber<int32_t>(),requestObj["modelId"].asNumber<int32_t>() ,
											requestObj["ttl"].asNumber<int32_t>(), &relayStatus
											);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", requestObj["subscribe"].asBool());
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);
	return true;
}
