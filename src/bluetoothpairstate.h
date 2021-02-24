// Copyright (c) 2014-2021 LG Electronics, Inc.
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


#ifndef BLUETOOTH_PAIRSTATE_H_
#define BLUETOOTH_PAIRSTATE_H_

#include <pbnjson.hpp>
#include <luna-service2/lunaservice.hpp>

class BluetoothDevice;

namespace pbnjson {
	class JValue;
}

class BluetoothPairState
{
public:
	BluetoothPairState();
	~BluetoothPairState();

	bool isPairable() const;
	bool isPairing() const;
	uint32_t getPairableTimeout() const;
	BluetoothDevice* getDevice() const { return mDevice; }

	void setPairable(bool pairable) { mPairable = pairable; }
	void setPairableTimeout(uint32_t pairableTimeout) { mPairableTimeout = pairableTimeout; }

	bool isIncoming() const;
	bool isOutgoing() const;

	void stopPairing();
	void startPairing(BluetoothDevice* device);

	void markAsOutgoing() { mIncoming = false; }
	void markAsIncoming() { mIncoming = true; }

private:
	bool mPairing;
	bool mPairable;
	uint32_t mPairableTimeout;
	bool mIncoming;
	std::string mPairingAddress;
	BluetoothDevice *mDevice;
};

#endif
