// Copyright (c) 2014-2018 LG Electronics, Inc.
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


#ifndef BLUETOOTHDEVICE_H
#define BLUETOOTHDEVICE_H

#include <string>
#include <map>

#include <bluetooth-sil-api.h>

typedef std::function<bool()> BluetoothDeviceWatchCallback;

#include "bluetoothserviceclasses.h"

class BluetoothDevice
{
public:
	BluetoothDevice();
	BluetoothDevice(BluetoothPropertiesList &properties);
	~BluetoothDevice();

	BluetoothDevice(const BluetoothDevice &other) = delete;

	bool update(BluetoothPropertiesList &properties);

	std::string getName() const { return mName; }
	std::string getAddress() const { return mAddress; }
	BluetoothDeviceType getType() const { return mType; }
	uint32_t getClassOfDevice() const { return mClassOfDevice; }
	bool getPaired() const { return mPaired; }
	bool getPairing() const { return mPairing; }
	bool getTrusted() const { return mTrusted; }
	bool getBlocked() const { return mBlocked; }
	int getRssi() const { return mRssi; }
	void setPairing(bool pairingStatus) { mPairing = pairingStatus; }
	std::vector<std::string> getUuids() const { return mUuids; }
	std::vector<std::string> getMapInstancesName() const { return mMapInstancesName; }
	std::map<std::string, std::vector<std::string>> getSupportedMessageTypes() const { return mMapSupportedMessageTypes; }
	std::vector<BluetoothServiceClassInfo> getSupportedServiceClasses() const { return mSupportedServiceClasses; }
	bool getConnected() const { return mConnected; }
	uint32_t getRole() const { return mRole; }
	std::vector<uint8_t> getManufacturerData() const { return mManufacturerData; }
	InquiryAccessCode getAccessCode() const { return mAccessCode; }
	std::vector<uint8_t> getScanRecord() const { return mScanRecord; }
	// whether the device is under connection with the input role or not
	bool hasConnectedRole(uint32_t role) const { return (mRole & role); }

	std::string getTypeAsString() const;

private:
	std::string mName;
	std::string mAddress;
	BluetoothDeviceType mType;
	uint32_t mClassOfDevice;	// Specified by https://www.bluetooth.org/en-us/specification/assigned-numbers/baseband
	std::vector<std::string> mUuids;
	std::vector<std::string> mMapInstancesName;
	std::map<std::string, std::vector<std::string>> mMapSupportedMessageTypes;
	bool mPaired;
	bool mPairing;
	bool mTrusted;
	bool mBlocked;
	std::vector<BluetoothServiceClassInfo> mSupportedServiceClasses;
	bool mConnected;
	int mRssi;
	uint32_t mRole;
	std::vector<uint8_t> mManufacturerData;
	InquiryAccessCode mAccessCode;
	std::vector<uint8_t> mScanRecord;

	void updateSupportedServiceClasses();
};

#endif // BLUETOOTHDEVICE_H
