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

#ifndef BLUETOOTHMESHPROFILESERVICE_H
#define BLUETOOTHMESHPROFILESERVICE_H

#include <string>
#include <map>
#include <list>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>

#include "bluetoothprofileservice.h"

#define DELETE_OBJ(del_obj) \
	if (del_obj)            \
	{                       \
		delete del_obj;     \
		del_obj = 0;        \
	}

namespace pbnjson
{
	class JValue;
}

namespace LS
{
	class Message;
	class ServerStatus;
} // namespace LS

namespace LSUtils
{
	class ClientWatch;
}

class BluetoothClientWatch;

class BluetoothMeshProfileService : public BluetoothProfileService, BluetoothMeshObserver
{
public:
	BluetoothMeshProfileService(BluetoothManagerService *manager);
	~BluetoothMeshProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);

	/* Mesh APIs */
	bool scanUnprovisionedDevices(LSMessage &message);

	/* Mesh Observer APIs */
	void scanResult(const std::string &adapterAddress, const int16_t rssi, const std::string &uuid, const std::string &name = "");

private:
	/* Private helper methods */
	bool addClientWatch(LS::Message &request,
						std::list<BluetoothClientWatch *> *clientWatch,
						std::string adapterAddress, std::string deviceAddress);
	void handleClientDisappeared(std::list<BluetoothClientWatch *> *clientWatch,
								 const std::string senderName);
	void removeClientWatch(std::list<BluetoothClientWatch *> *clientWatch,
						   const std::string &senderName);
	bool updateDeviceList(const std::string &adapterAddress, const int16_t rssi, const std::string &uuid, const std::string &name);
	pbnjson::JValue appendDevice(const int16_t rssi, const std::string &uuid, const std::string &name);
	pbnjson::JValue appendDevices(const std::string &adapterAddress);
	bool isScanDevicePresent(const std::string &adapterAddress, const std::string &uuid);

private:
	typedef struct device
	{
		device(int16_t rssiValue, std::string uuidValue, std::string nameValue)
		:rssi(rssiValue), uuid(uuidValue), name(nameValue) {}
		int16_t rssi;
		std::string uuid;
		std::string name;
	} UnprovisionedDeviceInfo;
	std::list<BluetoothClientWatch *> mScanResultWatch;
	/* map<adapterAddress, map<uuid, UnprovisionedDeviceInfo>> */
	std::unordered_map<std::string, std::map<std::string, UnprovisionedDeviceInfo>> mUnprovisionedDevices;
};

#endif //BLUETOOTHMESHPROFILESERVICE_H
