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

#define LOCAL_NODE_ADDRESS		1
#define MIN_NODE_ADDRESS		1
#define MAX_NODE_ADDRESS		32767
/* default wait time in seconds */
#define DEFAULT_WAIT_TIMEOUT	2
#define SECURE_KEY_STORAGE		"/var/lib/bluetooth/mesh/"

BluetoothMeshProfileService::BluetoothMeshProfileService(BluetoothManagerService *manager) :
BluetoothProfileService(manager, "MESH", "00001827-0000-1000-8000-00805f9b34fb"),
mNetworkCreated(0),
mAppKeyIndex(0)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothMeshProfileService, base)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, scanUnprovisionedDevices)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, unprovisionedScanCancel)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, createNetwork)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, provision)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, supplyProvisioningOob)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, supplyProvisioningNumeric)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, createAppKey)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, getMeshInfo)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, listProvisionedNodes)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, removeNode)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, keyRefresh)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothMeshProfileService, modelconfig)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, get)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, set)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, getCompositionData)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothMeshProfileService, model)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, send)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, receive)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothMeshProfileService, onOff)
	LS_CATEGORY_MAPPED_METHOD(set, setOnOff)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/mesh", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/mesh", this);

	manager->registerCategory("/mesh/model/config", LS_CATEGORY_TABLE_NAME(modelconfig), NULL, NULL);
	manager->setCategoryData("/mesh/model/config", this);

	manager->registerCategory("/mesh/model", LS_CATEGORY_TABLE_NAME(model), NULL, NULL);
	manager->setCategoryData("/mesh/model", this);

	manager->registerCategory("/mesh/model/onOff", LS_CATEGORY_TABLE_NAME(onOff), NULL, NULL);
	manager->setCategoryData("/mesh/model/onOff", this);

	applyCGroupSecurity(SECURE_KEY_STORAGE);
}

BluetoothMeshProfileService::~BluetoothMeshProfileService()
{
	for (auto iter = mScanResultWatch.begin();
		 iter != mScanResultWatch.end(); iter++)
	{
		delete (*iter);
	}
	mScanResultWatch.clear();

	for (auto iter = mModelOnOffResultWatch.begin();
		 iter != mModelOnOffResultWatch.end(); iter++)
	{
		delete (*iter);
	}
	mModelOnOffResultWatch.clear();

	for (auto iter = mNetworkIdWatch.begin();
		 iter != mNetworkIdWatch.end(); iter++)
	{
		delete (*iter);
	}
	mNetworkIdWatch.clear();

	for (auto iter = mProvResultWatch.begin();
		 iter != mProvResultWatch.end(); iter++)
	{
		delete (*iter);
	}
	mProvResultWatch.clear();

	for (auto iter = mModelConfigResultWatch.begin();
		 iter != mModelConfigResultWatch.end(); iter++)
	{
		delete (*iter);
	}
	mModelConfigResultWatch.clear();
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

	/* Get Appkeys from db */
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
				BT_DEBUG("appkey: %d, appname: %s", appKeyIndex, appName.c_str());
				mAppKeys.insert(std::pair<uint16_t, std::string>(appKeyIndex, appName));
			}
		}

		/* Keep the app key index to next available index */
		while (isAppKeyExist(mAppKeyIndex))
		{
			mAppKeyIndex++;
		}
	}

	/*Get node info from db */
	pbnjson::JValue nodeInfo;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	results = nodeInfo["results"];
	std::vector<uint16_t> unicastAddresses;

	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if (meshEntry.hasKey("unicastAddress"))
			{
				uint16_t uincastAddress = (uint16_t)meshEntry["unicastAddress"].asNumber<int32_t>();
				for (int i = 0; i < meshEntry["count"].asNumber<int32_t>(); ++i)
				{
					unicastAddresses.push_back(uincastAddress + i);
				}
			}
		}
		getImpl<BluetoothMeshProfile>(adapterAddress)->updateNodeInfo("PB-ADV", unicastAddresses);
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

void BluetoothMeshProfileService::handleKeyRefreshClientDisappeared(
					std::map<uint16_t, BluetoothClientWatch *> &keyRefreshWatch,
					const uint16_t netKeyIndex)
{
	auto watchIter = keyRefreshWatch.find(netKeyIndex);
	if (watchIter != keyRefreshWatch.end())
	{
		BluetoothClientWatch *watch = watchIter->second;
		delete(watch);
		keyRefreshWatch.erase(netKeyIndex);
	}
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

void BluetoothMeshProfileService::modelSetOnOffResult(const std::string &adapterAddress, bool onOffState, BluetoothError error)
{
	for (auto watch : mModelOnOffResultWatch)
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
			object.put("onOFF", onOffState);

			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothMeshProfileService::modelConfigResult(const std::string &adapterAddress, BleMeshConfiguration &configuration, BluetoothError error)
{

	BT_INFO("MESH", 0, "[%s : %d], getConfig: %s srcAddress:%d \n", __FUNCTION__, __LINE__, configuration.getConfig().c_str(), configuration.getNodeAddress());

	std::string config(configuration.getConfig());
	std::string configSub(configuration.getConfig());

	eraseAllSubStr(configSub, "_SET");
	eraseAllSubStr(configSub, "_GET");

	std::string key = adapterAddress + configSub + std::to_string(configuration.getNodeAddress());

	auto watch = mModelConfigResultWatch.begin();
	while (watch != mModelConfigResultWatch.end())
	{
		BT_INFO("MESH", 0, "key: %s --- %s", key.c_str(), (*watch)->getAdapterAddress().c_str());
		if (convertToLower(key) == convertToLower((*watch)->getAdapterAddress()))
		{
			if (BLUETOOTH_ERROR_NONE != error)
			{
				LSUtils::respondWithError((*watch)->getMessage(), error, true);
				return;
			}
			pbnjson::JValue object = pbnjson::Object();
			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);

			if (config == "DEFAULT_TTL_GET")
			{
				object.put("ttl", configuration.getTTL());
			}
			else if (config == "GATT_PROXY_GET")
			{
				object.put("gattProxyState", configuration.getGattProxyState());
			}
			else if (config == "RELAY_GET")
			{
				object.put("relayStatus", appendRelayStatus(configuration.getRelayStatus()));
			}
			else if (config == "APPKEYINDEX")
			{
				object.put("appKeyIndexes", appendAppKeyIndexes(configuration.getAppKeyIndexes()));
				LSUtils::callDb8UpdateAppkey(getManager(), configuration.getNodeAddress(), configuration.getAppKeyIndexes());
			}
			else if (config == "APPKEY_ADD")
			{
				updateAppkeyList(configuration.getNodeAddress(),configuration.getAppKeyIndex());
			}
			else if (config == "APPKEY_DELETE")
			{
				updateAppkeyList(configuration.getNodeAddress(),configuration.getAppKeyIndex(), true);
			}

			if (config == "COMPOSITION_DATA")
			{
				object.put("compositionData", appendCompositionData(configuration.getCompositionData()));
			}
			else
			{
				object.put("config", configSub);
			}
			LSUtils::postToClient((*watch)->getMessage(), object);
			delete (*watch);
			watch = mModelConfigResultWatch.erase(watch);
		}
		else
		{
			watch++;
		}
	}
}

pbnjson::JValue BluetoothMeshProfileService::appendCompositionData(BleMeshCompositionData compositionData)
{
	pbnjson::JValue object = pbnjson::Object();
	object.put("companyId", compositionData.getCompanyId());
	object.put("productId", compositionData.getProductId());
	object.put("versionId", compositionData.getVersionId());
	object.put("numRplEnteries", compositionData.getNumRplEnteries());

	pbnjson::JValue featureObject = pbnjson::Object();
	BleMeshFeature features = compositionData.getFeatures();
	featureObject.put("relay", features.getRelaySupport());
	featureObject.put("proxy", features.getProxySupport());
	featureObject.put("friend", features.getFriendSupport());
	featureObject.put("lowPower", features.getLowPowerSupport());
	object.put("features", featureObject);

	pbnjson::JValue elementsObjectArr = pbnjson::Array();
	std::vector<BleMeshElement> elements = compositionData.getElements();
	for (auto i = 0; i < elements.size(); ++i)
	{
		pbnjson::JValue elementobject = pbnjson::Object();
		elementobject.put("loc", elements[i].getLoc());
		elementobject.put("numS",  elements[i].getNumS());
		std::vector<uint32_t> sigModelIds = elements[i].getSigModelIds();
		pbnjson::JValue sigModIdsArray = pbnjson::Array();
		for (auto j = 0; j < sigModelIds.size(); ++j)
		{
			sigModIdsArray.append((int32_t)(sigModelIds[j]));
		}
		elementobject.put("sigModelIds", sigModIdsArray);
		elementobject.put("numV",  elements[i].getNumV());
		std::vector<uint32_t> vendorModelIds = elements[i].getVendorModelIds();
		pbnjson::JValue vendorModIdsArray = pbnjson::Array();
		for (auto k = 0; k < vendorModelIds.size(); ++k)
		{
			vendorModIdsArray.append((int32_t)(vendorModelIds[k]));
		}
		elementobject.put("vendorModelIds", vendorModIdsArray);
		elementsObjectArr.append(elementobject);
	}
	object.put("elements", elementsObjectArr);
	return object;
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
				BT_DEBUG("Db8 set mesh token success");
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

	if (isScanDevicePresent(adapterAddress, requestObj["uuid"].asString()))
	{
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

bool BluetoothMeshProfileService::setOnOff(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(bearer, string),
							PROP(destAddress, integer), PROP(appKeyIndex, integer),
							PROP(state, boolean), PROP(subscribe, boolean))
							REQUIRED_3(destAddress, appKeyIndex, state));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("destAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("appKeyIndex"))
			LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_PARAM_MISSING);
		else if (!requestObj.hasKey("state"))
			LSUtils::respondWithError(request, BT_ERR_MESH_ONOFF_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	bool value = requestObj["state"].asBool();

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothMeshProfile *impl = getImpl<BluetoothMeshProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	bool isSubscribe = false;
	if (requestObj.hasKey("subscribe"))
	{
		isSubscribe = requestObj["subscribe"].asBool();
	}

	if (isSubscribe)
	{
		bool retVal = addClientWatch(request, &mModelOnOffResultWatch,
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

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	if (!isAppKeyExist((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>()))
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_APP_KEY_INDEX_DOES_NOT_EXIST);
		return true;
	}

	if (!isValidApplication((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>(),request))
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_INVALID);
		return true;
	}

	BluetoothError error = impl->setOnOff(bearer,
			(uint16_t)requestObj["destAddress"].asNumber<int32_t>(),
			(uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>(),
			value, isSubscribe);
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

bool BluetoothMeshProfileService::send(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_7(PROP(adapterAddress, string),
							PROP(bearer, string), PROP(srcAddress, integer),
							PROP(destAddress, integer), PROP(appKeyIndex, integer),
							PROP(command, string), OBJECT(payload, SCHEMA_ANY))
							REQUIRED_5(srcAddress, destAddress, appKeyIndex, command, payload));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("srcAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_SRC_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("destAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("appKeyIndex"))
			LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_PARAM_MISSING);
		else if (!requestObj.hasKey("payload"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DATA_PARAM_MISSING);
		else if (!requestObj.hasKey("command"))
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_PARAM_INVALID);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string cmd = requestObj["command"].asString();
	BleMeshPayload meshSendPayload;
	BleMeshPayloadOnOff payloadOnOff;
	BleMeshPayloadPassthrough payloadPassThr;
	auto sendPayload = requestObj["payload"];

	if (cmd.compare("onOff") == 0)
	{
		pbnjson::JValue requestPayloadObj;
		const std::string payloadSchema = STRICT_SCHEMA (PROPS_1(PROP(value, boolean))
					REQUIRED_1(value));
		std::string str1 = sendPayload.stringify(NULL);
		BT_INFO("MESH", 0, "onOFF: [%s : %s]", str1.c_str(), payloadSchema.c_str());
		if (!LSUtils::parsePayload(sendPayload.stringify(NULL), requestPayloadObj, payloadSchema, &parseError))
		{
			if (parseError != JSON_PARSE_SCHEMA_ERROR)
				LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
			else
				LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

			return true;
		}

		payloadOnOff.value = sendPayload["value"].asBool();
		meshSendPayload.setPayloadOnOff(payloadOnOff);
	}
	else if(cmd.compare("passThrough") == 0)
	{
		pbnjson::JValue requestPayloadObj;
		const std::string payloadSchema = STRICT_SCHEMA (PROPS_1(ARRAY(value, integer))
					REQUIRED_1(value));
		std::string str1 = sendPayload.stringify(NULL);
		BT_INFO("MESH", 0, "passThrough: [%s : %s]", str1.c_str(), payloadSchema.c_str());
		if (!LSUtils::parsePayload(sendPayload.stringify(NULL), requestPayloadObj, payloadSchema, &parseError))
		{
			if (parseError != JSON_PARSE_SCHEMA_ERROR)
				LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
			else
				LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

			return true;
		}

		auto payloadData = sendPayload["value"];

		for(int i = 0; i < payloadData.arraySize(); i++)
		{
			pbnjson::JValue val = payloadData[i];
			payloadPassThr.value.push_back((uint8_t)val.asNumber<int32_t>());
		}
		meshSendPayload.setPayloadPassthrough(payloadPassThr);
	}
	else
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_INVALID_COMMAND);
		return true;
	}

	if (!isAppKeyExist((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>()))
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_APP_KEY_INDEX_DOES_NOT_EXIST);
		return true;
	}

	if (!isValidApplication((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>(),request))
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_INVALID);
		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
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

	BT_INFO("MESH", 0, "cmd: [%s : %d]", cmd.c_str(), __LINE__);
	BluetoothError error = impl->modelSend(bearer, (uint16_t)requestObj["srcAddress"].asNumber<int32_t>(),
				(uint16_t)requestObj["destAddress"].asNumber<int32_t>(),
				(uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>(),
				cmd, meshSendPayload);
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

bool BluetoothMeshProfileService::receive(LSMessage &message)
{
	BT_INFO("MESH", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string),
							PROP(bearer, string), PROP(appKeyIndex, integer),
							PROP_WITH_VAL_1(subscribe, boolean, true))
							REQUIRED_2(appKeyIndex, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("appKeyIndex"))
			LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
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

	if (!isAppKeyExist((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>()))
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_APP_KEY_INDEX_DOES_NOT_EXIST);
		return true;
	}

	if (!isValidApplication((uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>(),request))
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_APP_KEY_INDEX_INVALID);
		return true;
	}

	std::string bearer = "PB-ADV"; // default value

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}

	if (bearer.compare("PB-ADV") == 0 || bearer.compare("PB-GATT") == 0)
	{
		LS::SubscriptionPoint *subscriptionPoint = 0;
		uint16_t appKey = (uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>();
		auto modelAppKeySubsIter = recvSubscriptions.find(appKey);
		if (modelAppKeySubsIter == recvSubscriptions.end())
		{
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			recvSubscriptions.insert(std::pair<uint16_t, LS::SubscriptionPoint*>(appKey, subscriptionPoint));
		}
		else
		{
			subscriptionPoint = modelAppKeySubsIter->second;
		}

		subscriptionPoint->subscribe(request);

		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(request, responseObj);
	}
	else
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_PARAM_INVALID);
	}
	return true;

}

void BluetoothMeshProfileService::provisionResult(BluetoothError error, const std::string &adapterAddress,
								 const std::string &request,
								 const std::string &stringToDisplay,
								 const uint32_t numberToDisplay,
								 const std::string &numberDisplayType,
								 const std::string &promptType,
								 uint16_t unicastAddress,
								 uint8_t count,
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
					storeProvisionedDevice(unicastAddress, deviceUUID, count);
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
	uint16_t appKeyIndex = mAppKeyIndex;
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
		appKeyIndex = mAppKeyIndex;
		BT_INFO("MESH", 0, "Next available appkeyindex: %d", mAppKeyIndex);
	}

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

	uint16_t unicastAddress = (uint16_t)requestObj["destAddress"].asNumber<int32_t>();

	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addSubscription(request, adapterAddress, config, unicastAddress);

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

	BluetoothError error = impl->configGet(bearer, unicastAddress, config, requestObj["netKeyIndex"].asNumber<int32_t>());

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

	if (config == "APPKEY_ADD" || config == "APPKEY_UPDATE" ||
			config == "APPKEY_DELETE")
	{
		if (!requestObj.hasKey("netKeyIndex"))
		{
			LSUtils::respondWithError(request,
									  BT_ERR_MESH_NET_KEY_INDEX_PARAM_MISSING);
			return true;
		 }
	}

	if (config == "APPKEY_ADD" || config == "APPKEY_UPDATE" ||
			config == "APPKEY_BIND" || config == "APPKEY_DELETE" ||
			config == "APPKEY_UNBIND")
	{
		if (!requestObj.hasKey("appKeyIndex"))
		{
			LSUtils::respondWithError(request,
									  BT_ERR_MESH_APP_KEY_INDEX_PARAM_MISSING);
			return true;
		}
	}

	if(config == "APPKEY_BIND" || config == "APPKEY_UNBIND")
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

	if(config == "APPKEY_ADD" || config == "APPKEY_UPDATE" || config == "APPKEY_BIND"
		|| config == "APPKEY_UNBIND" || config == "APPKEY_DELETE")
	{
		uint16_t appKeyIndex = (uint16_t)requestObj["appKeyIndex"].asNumber<int32_t>();

		if (!isAppKeyExist(appKeyIndex))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_MESH_APP_KEY_INDEX_DOES_NOT_EXIST);
			return true;
		}

		if(!isValidApplication(appKeyIndex, request))
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_NOT_ALLOWED);
			return true;
		}
	}

	uint16_t unicastAddress = (uint16_t)requestObj["destAddress"].asNumber<int32_t>();

	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addSubscription(request, adapterAddress, config, unicastAddress);

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

	BluetoothError error = impl->configSet(bearer, unicastAddress, config,
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

bool BluetoothMeshProfileService::addSubscription(LS::Message &request, const std::string &adapterAddress, const std::string &config,
													uint16_t unicastAddress)
{
	std::string key = adapterAddress + config + std::to_string(unicastAddress);
	return addClientWatch(request, &mModelConfigResultWatch, key, "");
}

void BluetoothMeshProfileService::modelDataReceived(const std::string &adapterAddress,
					   uint16_t srcAddress, uint16_t destAddress, uint16_t appKey,
					   uint8_t data[], uint32_t datalen)
{
	BT_INFO("MESH", 0, "[%s : %d]", __FUNCTION__, __LINE__);
	auto modelAppKeySubsIter = recvSubscriptions.find(appKey);
	if (modelAppKeySubsIter == recvSubscriptions.end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = modelAppKeySubsIter->second;
	pbnjson::JValue dataArray = pbnjson::Array();
	for (size_t j=0; j < datalen; j++)
		dataArray.append(data[j]);
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("srcAddress", srcAddress);
	responseObj.put("destAddress", destAddress);
	responseObj.put("data", dataArray);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

bool BluetoothMeshProfileService::getCompositionData(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string),
													 PROP(bearer, string), PROP(destAddress, integer),
													 PROP(subscribe, boolean))  REQUIRED_2(destAddress, subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("destAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	uint16_t unicastAddress = (uint16_t)requestObj["destAddress"].asNumber<int32_t>();

	if(requestObj["subscribe"].asBool())
	{
		bool retVal = addSubscription(request, adapterAddress, "COMPOSITION_DATA", unicastAddress);

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

	BluetoothError error = impl->getCompositionData(bearer, unicastAddress);

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

bool BluetoothMeshProfileService::getMeshInfo(LSMessage &message)
{
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

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("meshInfo", appendMeshInfo());
	LSUtils::postToClient(request, responseObj);
	return true;
}

pbnjson::JValue BluetoothMeshProfileService::appendMeshInfo()
{
	pbnjson::JValue object = pbnjson::Object();
	object.put("name", "Mesh Network");
	object.put("netKeys", appendNetKeys());
	object.put("appKeys", appendAppKeys());
	object.put("provisioners", appendProvisioners());
	return object;
}

pbnjson::JValue BluetoothMeshProfileService::appendNetKeys()
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	pbnjson::JValue object = pbnjson::Object();
	object.put("index", 0);
	object.put("keyRefresh",false);
	platformObjArr.append(object);
	return platformObjArr;
}

pbnjson::JValue BluetoothMeshProfileService::appendAppKeys()
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	for (auto itr = mAppKeys.begin(); itr != mAppKeys.end(); ++itr)
	{
		pbnjson::JValue object = pbnjson::Object();
		object.put("index", itr->first);
		object.put("boundNetKeyIndex",0);
		platformObjArr.append(object);
	}
	return platformObjArr;
}

pbnjson::JValue BluetoothMeshProfileService::appendProvisioners()
{
	pbnjson::JValue platformObjArr = pbnjson::Array();
	pbnjson::JValue object = pbnjson::Object();
	object.put("name", "BLE Mesh");
	object.put("unicastAddress",0001);
	platformObjArr.append(object);
	return platformObjArr;
}

bool BluetoothMeshProfileService::listProvisionedNodes(LSMessage &message)
{
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

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();

		if(bearer != "PB-GATT" && bearer != "PB-ADV")
		{
			LSUtils::respondWithError(request, BLUETOOTH_ERROR_PARAM_INVALID);
			return true;
		}
	}

	if(bearer == "PB-GATT")
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_UNSUPPORTED);
		return true;
	}

	if (!isNetworkCreated())
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NETWORK_NOT_CREATED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("nodes", appendNodesInfo());
	LSUtils::postToClient(request, responseObj);
	return true;

}

pbnjson::JValue BluetoothMeshProfileService::appendNodesInfo()
{
	/*Get node info from db */
	pbnjson::JValue nodeInfo;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	pbnjson::JValue results = nodeInfo["results"];
	pbnjson::JValue nodeObjectArr = pbnjson::Array();

	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if (meshEntry.hasKey("unicastAddress"))
			{
				pbnjson::JValue object = pbnjson::Object();
				object.put("primaryElementAddress",meshEntry["unicastAddress"].asNumber<int32_t>());
				object.put("uuid", meshEntry["uuid"].asString());
				object.put("numberOfElements", meshEntry["count"].asNumber<int32_t>());
				object.put("netKeyIndex", meshEntry["netKeyIndex"].asNumber<int32_t>());
				object.put("appKeyIndexes", meshEntry["appKeyIndexes"]);
				nodeObjectArr.append(object);
			}
		}
	}
	return nodeObjectArr;
}

bool BluetoothMeshProfileService::removeNode(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(bearer, string), PROP(primaryElementAddress, integer)) REQUIRED_1(primaryElementAddress));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("primaryElementAddress"))
			LSUtils::respondWithError(request, BT_ERR_MESH_PRIMARY_ELEMENT_ADDRESS_PARAM_MISSING);
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

	uint16_t unicastAddress = (uint16_t)requestObj["primaryElementAddress"].asNumber<int32_t>();
	BT_INFO("MESH", 0, "primaryElementAddress :%d", unicastAddress);

	if(unicastAddress == LOCAL_NODE_ADDRESS)
	{
		BT_ERROR("MESH", 0, "Removing local node not allowed");
		LSUtils::respondWithError(request, BT_ERR_MESH_NODE_ADDRESS_INVALID);
		return true;
	}

	if(!(MIN_NODE_ADDRESS <= unicastAddress && unicastAddress <= MAX_NODE_ADDRESS))
	{
		BT_ERROR("MESH", 0, "primaryElementAddress is out of range, valid range: %d to %d", MIN_NODE_ADDRESS, MAX_NODE_ADDRESS);
		LSUtils::respondWithError(request, BT_ERR_MESH_NODE_ADDRESS_INVALID);
		return true;
	}

	if (!isValidUnicastAddress(unicastAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_NODE_ADDRESS_INVALID);
		return true;
	}

	uint8_t count = getElementCount(unicastAddress);

	BluetoothError error = impl->deleteNode(bearer, unicastAddress, count);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	if(!LSUtils::callDb8MeshDeleteNode(getManager(), unicastAddress))
	{
		BT_ERROR("MESH", 0, "Db8 delete node failed");
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("primaryElementAddress", unicastAddress);
	LSUtils::postToClient(request, responseObj);
	return true;

}

bool BluetoothMeshProfileService::isValidUnicastAddress(uint16_t unicastAddress)
{

	if(unicastAddress == LOCAL_NODE_ADDRESS)
		return true;

	/*Get node info from db */
	pbnjson::JValue nodeInfo;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	pbnjson::JValue results = nodeInfo["results"];

	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if (meshEntry.hasKey("unicastAddress"))
			{
				if(unicastAddress == meshEntry["unicastAddress"].asNumber<int32_t>())
					return true;
			}
		}
	}
	return false;
}

uint8_t BluetoothMeshProfileService::getElementCount(uint16_t unicastAddress)
{
	/*Get node info from db */
	pbnjson::JValue nodeInfo;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	pbnjson::JValue results = nodeInfo["results"];

	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if (meshEntry.hasKey("unicastAddress"))
			{
				if(unicastAddress == meshEntry["unicastAddress"].asNumber<int32_t>())
				{
					return meshEntry["count"].asNumber<int32_t>();
				}
			}
		}
	}
	return 0;
}

void BluetoothMeshProfileService::updateAppkeyList(uint16_t unicastAddress, uint16_t appKeyIndex, bool remove)
{
	BT_DEBUG("unicastAddress: %d, appKeyIndex: %d remove: %d", unicastAddress, appKeyIndex, remove);

	pbnjson::JValue nodeInfo;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	pbnjson::JValue results = nodeInfo["results"];

	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue meshEntry = results[i];
			if(unicastAddress == meshEntry["unicastAddress"].asNumber<int32_t>())
			{
				if (meshEntry.hasKey("appKeyIndexes"))
				{
					std::vector<uint16_t> appKeyIndexesList;
					auto appKeyIndexesObjArray = meshEntry["appKeyIndexes"];
					for (int n = 0; n < appKeyIndexesObjArray.arraySize(); n++)
					{
						pbnjson::JValue element = appKeyIndexesObjArray[n];
						appKeyIndexesList.push_back(element.asNumber<int32_t>());
					}

					auto findIter = std::find(appKeyIndexesList.begin(), appKeyIndexesList.end(), appKeyIndex);
					if(remove)
					{
						if (findIter == appKeyIndexesList.end())
							return;
						appKeyIndexesList.erase(findIter);
					}
					else
					{
						if (findIter != appKeyIndexesList.end())
							return;
						appKeyIndexesList.push_back(appKeyIndex);
					}
					LSUtils::callDb8UpdateAppkey(getManager(), unicastAddress, appKeyIndexesList);
				}
			}
		}
	}
}

bool BluetoothMeshProfileService::keyRefresh(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_8(PROP(adapterAddress, string),
												PROP(bearer, string),
												PROP(subscribe, boolean),
												PROP(netKeyIndex, integer),
												PROP(refreshAppKeys, boolean),
												PROP(waitTimeout, integer),
												ARRAY(appKeyIndexes, integer),
												ARRAY(blacklistedNodes, integer))
												REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
		{
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		}
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

	std::string bearer = "PB-ADV";

	if (requestObj.hasKey("bearer"))
	{
		bearer = requestObj["bearer"].asString();
	}
	uint16_t netKeyIndex = 0;
	if (requestObj.hasKey("netKeyIndex"))
	{
		netKeyIndex = (uint16_t)requestObj["netKeyIndex"].asNumber<int32_t>();
	}
	bool refreshAppKeys = false;
	if (requestObj.hasKey("refreshAppKeys"))
	{
		refreshAppKeys = requestObj["refreshAppKeys"].asBool();
	}
	std::vector<uint16_t> appKeyIndexesToRefresh;
	if (refreshAppKeys)
	{
		pbnjson::JValue indexes = pbnjson::Array();
		if (requestObj.hasKey("appKeyIndexes"))
		{
			indexes = requestObj["appKeyIndexes"];
		}
		if (0 == indexes.arraySize())
		{
			for (auto appKeys : mAppKeys)
			{
				appKeyIndexesToRefresh.push_back(appKeys.first);
			}
		}
		else
		{
			for (int i = 0; i < indexes.arraySize(); ++i)
			{
				if (mAppKeys.end() != mAppKeys.find(indexes[i].asNumber<int32_t>()))
				{
					appKeyIndexesToRefresh.push_back((uint16_t)indexes[i].asNumber<int32_t>());
				}
			}
		}
	}

	/* Check if keyRefresh for the provided network index is already active */
	if (mKeyRefreshWatch.end() != mKeyRefreshWatch.find(netKeyIndex))
	{
		LSUtils::respondWithError(request, BT_ERR_MESH_KEY_REFRESH_IN_PROGRESS);
		return true;
	}

	std::vector<BleMeshNode> nodes = getProvisionedNodes();
	std::vector<uint16_t> blackListedNodes;
	if (requestObj.hasKey("blacklistedNodes"))
	{
		pbnjson::JValue blackListedNodesObj = requestObj["blacklistedNodes"];
		for (int i = 0; i < blackListedNodesObj.arraySize(); ++i)
		{
			blackListedNodes.push_back((uint16_t)blackListedNodesObj[i].asNumber<int32_t>());
		}
	}
	int32_t waitTimeout = DEFAULT_WAIT_TIMEOUT;
	if (requestObj.hasKey("waitTimeout"))
	{
		waitTimeout = requestObj["waitTimeout"].asNumber<int32_t>();
	}
	auto watch = new BluetoothClientWatch(getManager()->get(), request.get(),
										  std::bind(&BluetoothMeshProfileService::handleKeyRefreshClientDisappeared,
													this, mKeyRefreshWatch, netKeyIndex),
										  adapterAddress, "");
	mKeyRefreshWatch.insert(std::pair<uint16_t, BluetoothClientWatch*>(netKeyIndex, watch));

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto keyRefreshCallback = [this, requestMessage, netKeyIndex, blackListedNodes, adapterAddress](BluetoothError error)
	{
		BT_INFO("MESH", 0, "keyRefreshCallback");
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("subscribed", true);
		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(requestMessage, error, true);
			handleKeyRefreshClientDisappeared(mKeyRefreshWatch, netKeyIndex);
		}
		else
		{
			responseObj.put("adapterAddress", adapterAddress);
			for (int i = 0; i < blackListedNodes.size(); ++i)
			{
				if(!LSUtils::callDb8MeshDeleteNode(getManager(), blackListedNodes[i]))
				{
					BT_ERROR("MESH", 0, "Db8 delete node failed");
				}
			}
			LSUtils::postToClient(requestMessage, responseObj);
		}
		LSMessageUnref(requestMessage);
	};

	impl->keyRefresh(keyRefreshCallback, bearer, refreshAppKeys,
						appKeyIndexesToRefresh, blackListedNodes, nodes,
						netKeyIndex, waitTimeout);
	return true;
}

void BluetoothMeshProfileService::keyRefreshResult(BluetoothError error,
									const std::string &adapterAddress,
									uint16_t netKeyIndex,
									std::string &status,
									uint16_t keyRefreshPhase,
									uint16_t nodeAddress,
									uint16_t appKeyIndex)
{
	BT_INFO("MESH", 0, "[%s : %d]", __FUNCTION__, __LINE__);
	auto watch = mKeyRefreshWatch.find(netKeyIndex);
	/* There can be only once client for a key refresh for a given netKeyIndex */
	if (watch != mKeyRefreshWatch.end())
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->second->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->second->getAdapterAddress()))
		{
			responseObj.put("status", status);
			responseObj.put("subscribed", true);
		        responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("netKeyIndex", netKeyIndex);
			responseObj.put("keyRefreshPhase", keyRefreshPhase);
			responseObj.put("returnValue", true);
			if ("completed" == status)
			{
				responseObj.put("subscribed", false);
			}
			if (BLUETOOTH_ERROR_NONE != error)
			{
				pbnjson::JValue keyUpdateResponseObj = pbnjson::Object();
				keyUpdateResponseObj.put("primaryElementAddress", nodeAddress);
				keyUpdateResponseObj.put("responseCode", error);
				keyUpdateResponseObj.put("responseText", retrieveErrorCodeText(error));
				if (BLUETOOTH_ERROR_MESH_CANNOT_UPDATE_APPKEY == error)
				{
					keyUpdateResponseObj.put("appKeyIndex", appKeyIndex);
				}
				else if (BLUETOOTH_ERROR_MESH_NETKEY_UPDATE_FAILED == error)
				{
					if (!LSUtils::callDb8MeshDeleteNode(getManager(), nodeAddress))
					{
						BT_ERROR("MESH", 0, "Db8 delete node failed");
					}
				}
				else
				{
					LSUtils::respondWithError(watch->second->getMessage(), error);
					return;
				}
				responseObj.put("keyUpdateResponse", keyUpdateResponseObj);
			}
			LSUtils::postToClient(watch->second->getMessage(), responseObj);
			if ("completed" == status)
			{
				delete(watch->second);
				mKeyRefreshWatch.erase(watch);
			}
		}
	}

}

std::vector<BleMeshNode> BluetoothMeshProfileService::getProvisionedNodes()
{
	pbnjson::JValue nodeInfo;
	std::vector<BleMeshNode> meshNodes;
	LSUtils::callDb8MeshGetNodeInfo(getManager(), nodeInfo);
	pbnjson::JValue results = nodeInfo["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			pbnjson::JValue result = results[i];
			std::vector<uint16_t> appKeyIndexes;
			pbnjson::JValue appKeyIndexesObj = result["appKeyIndexes"];
			for (int j = 0; j < appKeyIndexesObj.arraySize(); ++j)
			{
				appKeyIndexes.push_back(appKeyIndexesObj[j].asNumber<int32_t>());
			}
			std::string uuid = result["uuid"].asString();
			BleMeshNode node(uuid,
				result["unicastAddress"].asNumber<int32_t>(),
				result["count"].asNumber<int32_t>(),
				result["netKeyIndex"].asNumber<int32_t>(), appKeyIndexes);
			meshNodes.push_back(node);
		}
	}
	return meshNodes;
}

void BluetoothMeshProfileService::storeProvisionedDevice(uint16_t unicastAddress, const std::string &uuid, uint8_t count)
{
	std::string id =  LSUtils::getObjectIDByUUID(getManager(), uuid);

	if(!id.empty() && !LSUtils::callDb8DeleteId(getManager(), id))
	{
		BT_INFO("MESH", 0, "delete id from db failed: %s", id.c_str());
	}

	if (!LSUtils::callDb8MeshPutNodeInfo(getManager(), unicastAddress, uuid, count))
	{
		BT_ERROR("MESH", 0, "Failed to store unicastAddresse: %d", unicastAddress);
	}
}

void BluetoothMeshProfileService:: applyCGroupSecurity(const std::string &folder)
{
	BT_INFO("MESH", 0, "[%s : %d] Path:%s", __FUNCTION__, __LINE__, folder.c_str());

	if(folder.empty())
		return;

	if (!changeFolderGroup("blemesh", folder))
	{
		BT_DEBUG("changing Group failed for %s", folder.c_str());
	}

	if (!changeFolderPermission("660", folder))
	{
		BT_DEBUG("changing FolderPermission failed for %s", folder.c_str());
	}

	if (!setGroupID(folder))
	{
		BT_DEBUG("setGroupID failed for %s", folder.c_str());
	}
}
