// Copyright (c) 2014-2020 LG Electronics, Inc.
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


#include "bluetoothserviceclasses.h"
#include "bluetoothdevice.h"
#include "utils.h"
#include "logging.h"

BluetoothDevice::BluetoothDevice() :
	mType(BLUETOOTH_DEVICE_TYPE_UNKNOWN),
	mClassOfDevice(0),
	mPaired(false),
	mPairing(false),
	mTrusted(false),
	mBlocked(false),
	mConnected(false),
	mRole(BLUETOOTH_DEVICE_ROLE),
	mRssi(0),
	mAccessCode(InquiryAccessCode::BT_ACCESS_CODE_NONE)
{
}

BluetoothDevice::BluetoothDevice(BluetoothPropertiesList &properties) :
	mType(BLUETOOTH_DEVICE_TYPE_UNKNOWN),
	mClassOfDevice(0),
	mPaired(false),
	mPairing(false),
	mTrusted(false),
	mBlocked(false),
	mConnected(false),
	mRssi(0),
	mRole(BLUETOOTH_DEVICE_ROLE)
{
	update(properties);
}

BluetoothDevice::~BluetoothDevice()
{
}


/**
 * @brief Update device with a set of changed properties
 * @param properties List of properties which have changed
 * @return True if device properties have changed. False otherwise.
 */
bool BluetoothDevice::update(BluetoothPropertiesList &properties)
{
	bool changed = false;

	for (auto prop : properties)
	{
		switch (prop.getType())
		{
		case BluetoothProperty::Type::NAME:
			mName = prop.getValue<std::string>();
			changed = true;
			break;
		case BluetoothProperty::Type::BDADDR:
			mAddress = convertToLower(prop.getValue<std::string>());
			changed = true;
			break;
		case BluetoothProperty::Type::UUIDS:
			mUuids = prop.getValue<std::vector<std::string>>();
			updateSupportedServiceClasses();
			changed = true;
			break;
		case BluetoothProperty::Type::MAP_INSTANCES_NAME:
			mMapInstancesName = prop.getValue<std::vector<std::string>>();
			changed = true;
			break;
		case BluetoothProperty::Type::MAP_SUPPORTED_MESSAGE_TYPE:
			mMapSupportedMessageTypes = prop.getValue<std::map<std::string, std::vector<std::string>>>();
			changed = true;
			break;
		case BluetoothProperty::Type::CLASS_OF_DEVICE:
			mClassOfDevice = prop.getValue<uint32_t>();
			changed = true;
			break;
		case BluetoothProperty::Type::TYPE_OF_DEVICE:
			mType = (BluetoothDeviceType) prop.getValue<uint32_t>();
			changed = true;
			break;
		case BluetoothProperty::Type::PAIRED:
			mPaired = prop.getValue<bool>();
			changed = true;
			break;
		case BluetoothProperty::Type::CONNECTED:
			mConnected = prop.getValue<bool>();
			changed = true;
			break;
		case BluetoothProperty::Type::TRUSTED:
			mTrusted = prop.getValue<bool>();
			BT_DEBUG("Trusted is updated to %d for address %s", mTrusted, mAddress.c_str());
			changed = true;
			break;
		case BluetoothProperty::Type::BLOCKED:
			mBlocked = prop.getValue<bool>();
			BT_DEBUG("Blocked is updated to %d for address %s", mBlocked, mAddress.c_str());
			changed = true;
			break;
		case BluetoothProperty::Type::RSSI:
			mRssi = prop.getValue<int>();
			changed = true;
			break;
		case BluetoothProperty::Type::ROLE:
			mRole = prop.getValue<uint32_t>();
			changed = true;
			break;
		case BluetoothProperty::Type::MANUFACTURER_DATA:
			mManufacturerData = prop.getValue<std::vector<uint8_t>>();
			changed = true;
			break;
		case BluetoothProperty::Type::INQUIRY_ACCESS_CODE:
			mAccessCode = (InquiryAccessCode) prop.getValue<uint32_t>();
			changed = true;
			break;
		case BluetoothProperty::Type::SCAN_RECORD:
			mScanRecord = prop.getValue<std::vector<uint8_t>>();
			changed = true;
			break;
		default:
			break;
		}
	}

	return changed;
}

void BluetoothDevice::updateSupportedServiceClasses()
{
	mSupportedServiceClasses.clear();

	// We only care about us knowing the profile here and not if we support it. If
	// we don't support the profile then the service user will not have any ability
	// to connect with it at all.

	for (auto uuid : mUuids)
	{
		std::string luuid = convertToLower(uuid);
		auto iter = allServiceClasses.find(luuid);
		if (iter != allServiceClasses.end())
		{
			mSupportedServiceClasses.push_back(iter->second);
		}
	}
}

std::string BluetoothDevice::getTypeAsString() const
{
	switch (mType)
	{
	case BLUETOOTH_DEVICE_TYPE_BREDR:
		return "bredr";
	case BLUETOOTH_DEVICE_TYPE_BLE:
		return "ble";
	case BLUETOOTH_DEVICE_TYPE_DUAL:
		return "dual";
	default:
		return "unknown";
	}
}
