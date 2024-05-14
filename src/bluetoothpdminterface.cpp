// Copyright (c) 2024 LG Electronics, Inc.
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

#ifdef MULTI_SESSION_SUPPORT

#include "bluetoothpdminterface.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothserviceclasses.h"
#include "bluetoothmanageradapter.h"
#include "ls2utils.h"
#include "logging.h"
#include "servicewatch.h"
#include "utils.h"
#include "bluetoothprofileservice.h"
#include <map>

#define PDM_SERVICE "com.webos.service.pdm"
#define GET_ATTACHED_NOSTORAGE_DEVICES_URI "luna://com.webos.service.pdm/getAttachedNonStorageDeviceList"
#define GET_ATTACHED_NOSTORAGE_DEVICES_PAYLOAD "{\"subscribe\":true}"
#define CONFIG "/var/lib/bluetooth/adaptersAssignment.json"

std::map <std::string, std::string> displayAssignment =
{
       {"RSE-L", "sa8155 Bluetooth hci0"},
       {"RSE-R", "sa8155 Bluetooth hci1"},
       {"AVN", "sa8155 Bluetooth hci2"}
};

BluetoothPdmInterface::BluetoothPdmInterface(BluetoothManagerService *mngr):
mBluetootManager(mngr),
mWatch(nullptr)
{
	BT_DEBUG("BluetoothPdmInterface created");

	mWatch = new LSUtils::ServiceWatch(mngr->get(), PDM_SERVICE, [this] (bool connected) {
		BT_DEBUG("Service status %d", connected);
		if (connected)
		{
			onServiceConnected();
		}
		else
		{
			onServiceDisconnected();
		}
	});
}

BluetoothPdmInterface::~BluetoothPdmInterface()
{
	BT_DEBUG("BluetoothPdmInterface destroyed");
	if (mWatch)
		delete mWatch;
}

void BluetoothPdmInterface::onServiceConnected()
{
	BT_DEBUG("onServiceConnected");
	LSError error;
	LSErrorInit(&error);
	if (!LSCall(mBluetootManager->get(), GET_ATTACHED_NOSTORAGE_DEVICES_URI, GET_ATTACHED_NOSTORAGE_DEVICES_PAYLOAD, BluetoothPdmInterface::getAttachedNonStorageDeviceListCb, this, NULL, &error))
	{
		BT_ERROR("MSGID_ERROR_CALL", 0, error.message);
		LSErrorFree(&error);
		return;
	}
}

void BluetoothPdmInterface::onServiceDisconnected()
{
	BT_DEBUG("onServiceConnected");
}

bool BluetoothPdmInterface::getAttachedNonStorageDeviceListCb(LSHandle *sh, LSMessage *reply, void *context)
{
	BT_DEBUG("getAttachedNonStorageDeviceListCb");

	BluetoothPdmInterface *pThis = static_cast<BluetoothPdmInterface*>(context);
	if (nullptr == pThis)
		return true;

	std::string response = LSMessageGetPayload(reply);

	BT_DEBUG("Response from PDM %s", response.c_str());

	pbnjson::JValue replyObj = pbnjson::Object();

	if (!LSUtils::parsePayload(LSMessageGetPayload(reply), replyObj))
	{
		BT_ERROR("BT_PDM_INTERFACE", 0, "PDM ls response pasing error");
	}

	bool returnValue = replyObj["returnValue"].asBool();

	if (!returnValue)
	{
		BT_ERROR("BT_PDM_INTERFACE", 0, "PDM ls call returned subcription fail");
		return true;
	}

	pThis->assignAdaptersToDisplays(replyObj);
	return true;
}

void BluetoothPdmInterface::assignAdaptersToDisplays(pbnjson::JValue &replyObj)
{
	auto deviceListInfo = replyObj["deviceListInfo"];

	if (!deviceListInfo.isArray())
			BT_ERROR("BT_PDM_INTERFACE", 0, "deviceListInfo is not an array");

	BT_DEBUG("assignAdaptersToDisplays Size of deviceListInfo %zu", deviceListInfo.arraySize());

	auto adapters = mBluetootManager->getAvailableBluetoothAdapters();

	for(int i = 0; i < deviceListInfo.arraySize(); i++)
	{
		auto nonStorageDeviceList = deviceListInfo[i]["nonStorageDeviceList"];

		if (!nonStorageDeviceList.isArray())
			BT_ERROR("BT_PDM_INTERFACE", 0, "nonStorageDeviceList is not an array");

		BT_DEBUG("Size %zu nonStorageDeviceList", nonStorageDeviceList.arraySize());

		for (int j = 0; j < nonStorageDeviceList.arraySize(); j++)
		{
			pbnjson::JValue nonStorageDeviceObject = nonStorageDeviceList[j];

			std::string devType = nonStorageDeviceObject["deviceType"].asString();
			if (devType == "BLUETOOTH")
			{
				std::string deviceSetId = nonStorageDeviceObject["deviceSetId"].asString();
				std::string deviceName = nonStorageDeviceObject["deviceName"].asString();

				BT_DEBUG("deviceSetId %s deviceName %s", deviceSetId.c_str(), deviceName.c_str());
				mAdapterMap[deviceName] = deviceSetId;
			}
		}
	}

	if (!mAdapterMap.empty())
	{
                GError *error = NULL;
                gint exit_status;
                std::string touchConfig = "touch " + std::string(CONFIG);
                gchar *touchconfig_copy = g_strdup(touchConfig.c_str());
                gchar *cmd1[] = {"sh", "-c", touchconfig_copy, NULL};
                if(!g_spawn_sync(NULL, cmd1, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,NULL, NULL,&exit_status,&error))
                {
                    g_printerr("Error executing command: %s\n", error->message);
                    g_error_free(error);
                }
                g_free(touchconfig_copy);
        }

	for (auto adapterAssigned = mAdapterMap.begin(); adapterAssigned != mAdapterMap.end(); adapterAssigned++)
	{
		for (auto it = adapters.begin(); it != adapters.end(); it++)
		{
			BluetoothManagerAdapter* adapter = it->second;
			auto interfaceName = adapter->getInterface();

			auto adapterAddress = adapter->getAddress();//convertToUpper(adapter->getAddress());

			if (interfaceName == adapterAssigned->first)
			{
				auto adapterName = displayAssignment[adapterAssigned->second];
				auto adapterNameChanged = [](BluetoothError error) {
					if (error == BLUETOOTH_ERROR_NONE)
					{
						BT_DEBUG("pdmInterface adapter name changed");
					}
				};

				if (adapterName != adapter->getName())
				{
                                        //clear adapter cache and devices
                                        GError *error = NULL;
                                        gint exit_status;
                                        auto removeAdapterPath = "rm -rf /var/lib/bluetooth/" + convertToUpper(adapter->getAddress());
                                        gchar *removeAdapterPath_copy = g_strdup(removeAdapterPath.c_str());
                                        gchar *cmd1[] = {"sh", "-c", removeAdapterPath_copy, NULL};
                                        if(!g_spawn_sync(NULL, cmd1, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,NULL, NULL,&exit_status,&error))
                                        {
                                                g_printerr("Error executing command: %s\n", error->message);
                                                g_error_free(error);
                                        }
                                        g_free(removeAdapterPath_copy);
					mBluetootManager->findAdapterInfo(adapterAddress)->getAdapter()->setAdapterProperty(BluetoothProperty(BluetoothProperty::Type::ALIAS, adapterName), adapterNameChanged);

					auto enableCallback = [this](BluetoothError error) {
						if (error == BLUETOOTH_ERROR_NONE)
						{
							BT_DEBUG("pdmInterface adapter name changed");
							return;
						}
					};

					auto adapter = mBluetootManager->getAdapter(adapterAddress);
					if (!adapter)
						return;

					std::string a2dpRoleUuid;
					if (adapterName == "sa8155 Bluetooth hci0" || adapterName == "sa8155 Bluetooth hci1")
					{
						a2dpRoleUuid = "0000110a-0000-1000-8000-00805f9b34fb";
					}
					else if (adapterName == "sa8155 Bluetooth hci2")
					{
						a2dpRoleUuid = "0000110b-0000-1000-8000-00805f9b34fb";
					}

					BluetoothProfile *a2dpImpl = adapter->getProfile("A2DP");
					if (a2dpImpl)
					{
						a2dpImpl->enable(a2dpRoleUuid, enableCallback);
					}
				}
			}
		}
	}
}

#endif
