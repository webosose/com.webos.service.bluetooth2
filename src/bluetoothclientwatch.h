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


#ifndef BLUETOOTH_CLIENTWATCH_H
#define BLUETOOTH_CLIENTWATCH_H

#include <string>
#include "clientwatch.h"

class BluetoothClientWatch : public LSUtils::ClientWatch
{
public:
	BluetoothClientWatch(LSHandle* handle, LSMessage* message, LSUtils::ClientWatchStatusCallback callback,
		std::string adapterAddress, std::string deviceAddress);
	~BluetoothClientWatch();
	std::string getAdapterAddress() const { return mAdapterAddress; }
	std::string getDeviceAddress() const { return mDeviceAddress; }

private:
	std::string mAdapterAddress;
	std::string mDeviceAddress;


};

#endif //BLUETOOTH_CLIENTWATCH_H
