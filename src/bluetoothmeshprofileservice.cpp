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
mNetworkCreated(0)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, scanUnprovisionedDevices)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, unprovisionedScanCancel)
	LS_CATEGORY_CLASS_METHOD(BluetoothMeshProfileService, createNetwork)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/mesh", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/mesh", this);

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
	PROP(bearer, string),PROP(scanTimeout, integer), PROP(subscribe, boolean)) REQUIRED_1( subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
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

void BluetoothMeshProfileService::scanResult(const std::string &adapterAddress,
											 const int16_t rssi, const std::string &uuid, const std::string &name)
{
	BT_INFO("MESH", 0, "[%s : %d], num_watch: %d", __FUNCTION__, __LINE__, mScanResultWatch.size());
	for (auto watch : mScanResultWatch)
	{
		BT_INFO("MESH", 0, "AdapterAddress: %s --- %s", adapterAddress.c_str(), watch->getAdapterAddress().c_str());
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			if(isScanDevicePresent(adapterAddress, uuid))
				continue;
			pbnjson::JValue object = pbnjson::Object();
			object.put("subscribed", true);
			object.put("returnValue", true);
			object.put("adapterAddress", adapterAddress);
			object.put("device", appendDevice(rssi, uuid, name));
			object.put("devices", appendDevices(adapterAddress));
			LSUtils::postToClient(watch->getMessage(), object);
			updateDeviceList(adapterAddress, rssi, uuid, name);
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


	if (mUnprovisionedDevices.end() == it)
	{
		UnprovisionedDeviceInfo deviceInfo(rssi, uuid, name);
		std::map<std::string, UnprovisionedDeviceInfo> device;
		device.insert(std::pair<std::string, UnprovisionedDeviceInfo>(uuid, deviceInfo));
		mUnprovisionedDevices.insert(std::pair<std::string, std::map<std::string, UnprovisionedDeviceInfo>>(adapterAddress, device));
	}
	else
	{
		auto devices = (it->second).find(uuid);
		if ((it->second).end() == devices)
		{
			UnprovisionedDeviceInfo deviceInfo(rssi, uuid, name);
			(it->second).insert(std::pair<std::string, UnprovisionedDeviceInfo>(uuid, deviceInfo));
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
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}
