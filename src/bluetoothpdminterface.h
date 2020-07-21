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


#ifndef BLUETOOTHPDMINTERFACE_H
#define BLUETOOTHPDMINTERFACE_H

#include <luna-service2/lunaservice.hpp>
#include <unordered_map>

class BluetoothManagerService;

namespace pbnjson
{
	class JValue;
}

namespace LSUtils
{
	class ServiceWatch;
}

class BluetoothPdmInterface
{
public:
	BluetoothPdmInterface(BluetoothManagerService *mngr);
	~BluetoothPdmInterface();

	void onServiceConnected();
	void onServiceDisconnected();

	static bool getAttachedNonStorageDeviceListCb(LSHandle *sh, LSMessage *reply, void *ctx);
	void assignAdaptersToDisplays(pbnjson::JValue &replyObj);

private:
	BluetoothManagerService *mBluetootManager;
	LSUtils::ServiceWatch *mWatch;
	std::unordered_map <std::string, std::string> mAdapterMap;
};

#endif
