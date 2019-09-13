// Copyright (c) 2019 LG Electronics, Inc.
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

#include <functional>

#include "logging.h"
#include "bluetoothmanageradapter.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothdevice.h"
#include "utils.h"
#include "config.h"
#include "bluetoothserviceclasses.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "bluetoothprofileservice.h"

using namespace std::placeholders;

BluetoothManagerAdapter::BluetoothManagerAdapter(BluetoothManagerService *mngr, std::string address):
mPowered(false),
mDiscoverable(false),
mDiscovering(false),
mIsDefault(false),
mDiscoveryTimeout(0),
mDiscoverableTimeout(0),
mClassOfDevice(0),
mAdapter(nullptr),
mAddress(address),
mOutgoingPairingWatch(0),
mIncomingPairingWatch(0),
mBluetoothManagerService(mngr)
{
	BT_INFO("MANAGER_SERVICE", 0, "BluetoothManagerAdapter address[%s] created", mAddress.c_str());
	mGetDevicesSubscriptions.setServiceHandle(mBluetoothManagerService);
	mEnabledServiceClasses = split(std::string(WEBOS_BLUETOOTH_ENABLED_SERVICE_CLASSES), ' ');
}

BluetoothManagerAdapter::~BluetoothManagerAdapter()
{
	BT_INFO("MANAGER_SERVICE", 0,"BluetoothManagerAdapter address[%s] destroyed", mAddress.c_str());
}

void BluetoothManagerAdapter::notifySubscriberLeDevicesChanged()
{
	for (auto watchIter : mStartScanWatches)
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		appendLeDevices(responseObj);

		responseObj.put("returnValue", true);
		LSUtils::postToClient(watchIter.second->getMessage(), responseObj);
	}
}

void BluetoothManagerAdapter::notifySubscriberLeDevicesChangedbyScanId(uint32_t scanId)
{
	BT_DEBUG("[%s][%d] -- notifySubscriberLeDevicesChangedbyScanId \n\r ", __FUNCTION__, __LINE__ );

	auto watchIter = mStartScanWatches.find(scanId);
	if (watchIter == mStartScanWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;
	pbnjson::JValue responseObj = pbnjson::Object();

	appendLeDevicesByScanId(responseObj, scanId);

	responseObj.put("returnValue", true);

	BT_DEBUG("[%s][%d] -- notifySubscriberLeDevicesChangedbyScanId \n\r ", __FUNCTION__, __LINE__ );


	LSUtils::postToClient(watch->getMessage(), responseObj);
}

void BluetoothManagerAdapter::notifySubscribersFilteredDevicesChanged()
{
	pbnjson::JValue responseObj = pbnjson::Object();

	for (auto watchIter : mGetDevicesWatches)
	{
		std::string senderName = watchIter.first;
		appendFilteringDevices(senderName, responseObj);
		responseObj.put("returnValue", true);
		LSUtils::postToClient(watchIter.second->getMessage(), responseObj);
	}
}

void BluetoothManagerAdapter::notifySubscribersDevicesChanged()
{
	pbnjson::JValue responseObj = pbnjson::Object();

	appendDevices(responseObj);

	responseObj.put("returnValue", true);

	LSUtils::postToSubscriptionPoint(&mGetDevicesSubscriptions, responseObj);
}

BluetoothDevice* BluetoothManagerAdapter::findDevice(const std::string &address) const
{
	std::string convertedAddress = convertToLower(address);
	auto deviceIter = mDevices.find(convertedAddress);
	if (deviceIter == mDevices.end())
	{
		convertedAddress = convertToUpper(address);
		auto deviceIter = mDevices.find(convertedAddress);
		if (deviceIter == mDevices.end())
			return 0;
	}

	return deviceIter->second;
}

BluetoothDevice* BluetoothManagerAdapter::findLeDevice(const std::string &address) const
{
	std::string convertedAddress = convertToLower(address);
	auto deviceIter = mLeDevices.find(convertedAddress);
	if (deviceIter == mLeDevices.end())
	{
		convertedAddress = convertToUpper(address);
		auto deviceIter = mLeDevices.find(convertedAddress);
		if (deviceIter == mLeDevices.end())
			return 0;
	}

	return deviceIter->second;
}

BluetoothLinkKey BluetoothManagerAdapter::findLinkKey(const std::string &address) const
{
	std::string convertedAddress = convertToLower(address);
	auto linkKeyIter = mLinkKeys.find(convertedAddress);
	if (linkKeyIter == mLinkKeys.end())
	{
		convertedAddress = convertToUpper(address);
		auto linkKeyIter = mLinkKeys.find(convertedAddress);
		if (linkKeyIter == mLinkKeys.end())
			return std::vector<int32_t>();
	}

	return linkKeyIter->second;
}

void BluetoothManagerAdapter::adapterStateChanged(bool powered)
{
	BT_INFO("MANAGER_SERVICE", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	if (powered == mPowered)
		return;

	BT_INFO("Manager", 0, "Bluetooth adapter(%s) state has changed to %s",mAddress.c_str(), powered ? "powered" : "not powered");

	mPowered = powered;

	if ( powered == true )
	{
		bt_ready_msg2kernel();
		write_kernel_log("[bt_time] mPowered is true ");
	}

	mBluetoothManagerService->notifySubscribersAboutStateChange();
}

void BluetoothManagerAdapter::adapterHciTimeoutOccurred()
{
	BT_INFO("MANAGER_SERVICE", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);
	BT_CRITICAL( "Module Error", 0, "Failed to adapterHciTimeoutOccurred" );
}

void BluetoothManagerAdapter::discoveryStateChanged(bool active)
{
	BT_INFO("MANAGER_SERVICE", 0, "Observer is called : [%s : %d] active : %d", __FUNCTION__, __LINE__, active);

	if (mDiscovering == active)
		return;

	BT_DEBUG("Bluetooth adapter discovery state has changed to %s", active ? "active" : "not active");

	mDiscovering = active;

	mBluetoothManagerService->notifySubscribersAboutStateChange();
}

void BluetoothManagerAdapter::adapterPropertiesChanged(BluetoothPropertiesList properties)
{
	BT_DEBUG("Bluetooth adapter properties have changed");
	updateFromAdapterProperties(properties);
}

void BluetoothManagerAdapter::updateFromAdapterProperties(const BluetoothPropertiesList &properties)
{
	bool changed = false;
	bool pairableValue = false;
	bool adaptersChanged = false;

	for(auto prop : properties)
	{
		switch (prop.getType())
		{
		case BluetoothProperty::Type::NAME:
			mName = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("Bluetooth adapter name has changed to %s", mName.c_str());
			break;
		case BluetoothProperty::Type::ALIAS:
			mName = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("Bluetooth adapter alias name has changed to %s", mName.c_str());
			break;
		case BluetoothProperty::Type::STACK_NAME:
			mStackName = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("Bluetooth stack name has changed to %s", mStackName.c_str());
			break;
		case BluetoothProperty::Type::STACK_VERSION:
			mStackVersion = prop.getValue<std::string>();
			changed = true;
			BT_DEBUG("Bluetooth stack version has changed to %s", mStackVersion.c_str());
			break;
		case BluetoothProperty::Type::FIRMWARE_VERSION:
			mFirmwareVersion = prop.getValue<std::string>();
			changed = true;

			// Add firmware legnth limitation due to Instart menu size.
			BT_DEBUG("Bluetooth module firmware full version has changed to %s", mFirmwareVersion.c_str());
			if ( mFirmwareVersion.size() > 11 )
				mFirmwareVersion = mFirmwareVersion.substr(0, 11);
			BT_DEBUG("Bluetooth module firmware crop version has changed to %s", mFirmwareVersion.c_str());

            if ( mFirmwareVersion == "")  // to Instart menu mFirmwareVersion : WEBDQMS-47082
                mFirmwareVersion = "NULL";

			break;
		case BluetoothProperty::Type::BDADDR:
			mAddress = convertToLower(prop.getValue<std::string>());
			changed = true;
			adaptersChanged = true;
			BT_DEBUG("Bluetooth adapter address has changed to %s", mAddress.c_str());
			break;
		case BluetoothProperty::Type::DISCOVERY_TIMEOUT:
			mDiscoveryTimeout = prop.getValue<uint32_t>();
			changed = true;
			BT_DEBUG("Bluetooth adapter discovery timeout has changed to %d", mDiscoveryTimeout);
			break;
		case BluetoothProperty::Type::DISCOVERABLE:
			mDiscoverable = prop.getValue<bool>();
			changed = true;
			BT_DEBUG("Bluetooth adapter discoverable state has changed to %s", mDiscoverable ? "discoverable" : "not discoverable");
			break;
		case BluetoothProperty::Type::DISCOVERABLE_TIMEOUT:
			mDiscoverableTimeout = prop.getValue<uint32_t>();
			changed = true;
			BT_DEBUG("Bluetooth adapter discoverable timeout has changed to %d", mDiscoverableTimeout);
			break;
		case BluetoothProperty::Type::UUIDS:
			updateSupportedServiceClasses(prop.getValue<std::vector<std::string>>());
			adaptersChanged = true;
			break;
		case BluetoothProperty::Type::CLASS_OF_DEVICE:
			mClassOfDevice = prop.getValue<uint32_t>();
			adaptersChanged = true;
			BT_DEBUG("Bluetooth adapter class of device updated to %d", mClassOfDevice);
			break;
		case BluetoothProperty::Type::PAIRABLE:
			pairableValue = prop.getValue<bool>();
			BT_DEBUG("Bluetooth adapter pairable state has changed to %s", pairableValue ? "pairable" : "not pairable");
			// If pairable has changed from true to false, it means PairableTimeout has
			// reached, so cancel the incoming subscription on awaitPairingRequests
			if (mPairState.isPairable() && pairableValue == false)
				cancelIncomingPairingSubscription();
			else if (BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT != mBluetoothManagerService->getIOPairingCapability())
				mPairState.setPairable(pairableValue);
			break;
		case BluetoothProperty::Type::PAIRABLE_TIMEOUT:
			mPairState.setPairableTimeout(prop.getValue<uint32_t>());
			changed = true;
			BT_DEBUG("Bluetooth adapter pairable timeout has changed to %d", mPairState.getPairableTimeout());
			break;
		default:
			break;
		}
	}

	if (changed)
		mBluetoothManagerService->notifySubscribersAboutStateChange();
	if (adaptersChanged)
		mBluetoothManagerService->notifySubscribersAdaptersChanged();
}

void BluetoothManagerAdapter::updateSupportedServiceClasses(const std::vector<std::string> uuids)
{
	mSupportedServiceClasses.clear();

	for (auto uuid : uuids)
	{
		std::string luuid = convertToLower(uuid);
		auto serviceClassInfo = allServiceClasses.find(luuid);
		if (serviceClassInfo == allServiceClasses.end())
		{
			// We don't have an entry in our list so we don't support the profile at all
			continue;
		}

		bool enabled = false;
		for (auto enabledServiceClass : mEnabledServiceClasses)
		{
			if (serviceClassInfo->second.getMnemonic().find(enabledServiceClass) != std::string::npos)
			{
				enabled = true;
				break;
			}
		}

		if (!enabled)
		{
			BT_DEBUG("SIL supports profile %s but support for it isn't enabled", serviceClassInfo->second.getMnemonic().c_str());
			continue;
		}

		mSupportedServiceClasses.push_back(serviceClassInfo->second);
	}

	// Sanity check if all enabled profiles are supported by the SIL
	for (auto serviceClass : mEnabledServiceClasses)
	{
		bool found = false;

		for (auto adapterSupportedServiceClass : mSupportedServiceClasses)
		{
			if (adapterSupportedServiceClass.getMnemonic().find(serviceClass) != std::string::npos)
			{
				found = true;
				break;
			}
		}

		if (!found)
		{
			BT_WARNING(MSGID_ENABLED_PROFILE_NOT_SUPPORTED_BY_SIL, 0,
					   "Profile %s should be supported but isn't by the loaded SIL module",
					   serviceClass.c_str());

			// We will let the service continue to work here but all profile
			// specific actions will fail cause not supported by the SIL and
			// will produce further warnings in the logs.
		}
	}
}

void BluetoothManagerAdapter::adapterKeepAliveStateChanged(bool enabled)
{
	mBluetoothManagerService->adapterKeepAliveStateChanged(enabled);
}

void BluetoothManagerAdapter::deviceFound(BluetoothPropertiesList properties)
{
	BluetoothDevice *device = new BluetoothDevice(properties);
	BT_DEBUG("Found a new device");
	mDevices.insert(std::pair<std::string, BluetoothDevice*>(device->getAddress(), device));

	notifySubscribersFilteredDevicesChanged();
	notifySubscribersDevicesChanged();
}

void BluetoothManagerAdapter::deviceFound(const std::string &address, BluetoothPropertiesList properties)
{
    auto device = findDevice(address);
    if (!device) {
        BluetoothDevice *device = new BluetoothDevice(properties);
		BT_DEBUG("Found a new device");
		mDevices.insert(std::pair<std::string, BluetoothDevice*>(device->getAddress(), device));
    }
    else {
        device->update(properties);
    }

	notifySubscribersFilteredDevicesChanged();
	notifySubscribersDevicesChanged();
}

void BluetoothManagerAdapter::devicePropertiesChanged(const std::string &address, BluetoothPropertiesList properties)
{
	BT_DEBUG("Properties of device %s have changed", address.c_str());

	auto device = findDevice(address);
	if (device && device->update(properties))
	{
		notifySubscribersFilteredDevicesChanged();
		notifySubscribersDevicesChanged();
	}
}

void BluetoothManagerAdapter::deviceRemoved(const std::string &address)
{
	BT_DEBUG("Device %s has disappeared", address.c_str());

	auto deviceIter = mDevices.find(address);
	if (deviceIter == mDevices.end())
		return;

	BluetoothDevice *device = deviceIter->second;
	mDevices.erase(deviceIter);
	delete device;
	notifySubscribersFilteredDevicesChanged();
	notifySubscribersDevicesChanged();
}

void BluetoothManagerAdapter::leDeviceFound(const std::string &address, BluetoothPropertiesList properties)
{
	auto device = findLeDevice(address);
	if (!device)
	{
		BluetoothDevice *device = new BluetoothDevice(properties);
		BT_DEBUG("Found a new LE device");
		mLeDevices.insert(std::pair<std::string, BluetoothDevice*>(device->getAddress(), device));
	}
	else
	{
		device->update(properties);
	}

	notifySubscriberLeDevicesChanged();
}

void BluetoothManagerAdapter::leDevicePropertiesChanged(const std::string &address, BluetoothPropertiesList properties)
{
	BT_DEBUG("Properties of device %s have changed", address.c_str());

	auto device = findLeDevice(address);
	if (device && device->update(properties))
		notifySubscriberLeDevicesChanged();
}

void BluetoothManagerAdapter::leDeviceRemoved(const std::string &address)
{
	BT_DEBUG("Device %s has disappeared", address.c_str());

	auto deviceIter = mLeDevices.find(address);
	if (deviceIter == mLeDevices.end())
		return;

	BluetoothDevice *device = deviceIter->second;
	mLeDevices.erase(deviceIter);
	delete device;

	notifySubscriberLeDevicesChanged();
}

void BluetoothManagerAdapter::leDeviceFoundByScanId(uint32_t scanId, BluetoothPropertiesList properties)
{
	BluetoothDevice *device = new BluetoothDevice(properties);
	BT_DEBUG("Found a new LE device by %d", scanId);

	auto devicesIter = mLeDevicesByScanId.find(scanId);
	if (devicesIter == mLeDevicesByScanId.end())
	{
		std::unordered_map<std::string, BluetoothDevice*> devices;
		devices.insert(std::pair<std::string, BluetoothDevice*>(device->getAddress(), device));

		mLeDevicesByScanId.insert(std::pair<uint32_t, std::unordered_map<std::string, BluetoothDevice*>>(scanId, devices));
	}
	else
		(devicesIter->second).insert(std::pair<std::string, BluetoothDevice*>(device->getAddress(), device));

	notifySubscriberLeDevicesChangedbyScanId(scanId);
}

void BluetoothManagerAdapter::leDevicePropertiesChangedByScanId(uint32_t scanId, const std::string &address, BluetoothPropertiesList properties)
{
	BT_DEBUG("Properties of device %s have changed by %d", address.c_str(), scanId);

	auto devicesIter = mLeDevicesByScanId.find(scanId);
	if (devicesIter == mLeDevicesByScanId.end())
		return;

	auto deviceIter = (devicesIter->second).find(address);
	if (deviceIter == (devicesIter->second).end())
		return;

	BluetoothDevice *device = deviceIter->second;
	if (device && device->update(properties))
	{
		notifySubscriberLeDevicesChangedbyScanId(scanId);
	}
}

void BluetoothManagerAdapter::leDeviceRemovedByScanId(uint32_t scanId, const std::string &address)
{
	BT_DEBUG("Device %s has disappeared in %d", address.c_str(), scanId);

	auto devicesIter = mLeDevicesByScanId.find(scanId);
	if (devicesIter == mLeDevicesByScanId.end())
		return;

	auto deviceIter = (devicesIter->second).find(address);
	if (deviceIter == (devicesIter->second).end())
		return;

	BluetoothDevice *device = deviceIter->second;
	(devicesIter->second).erase(deviceIter);
	delete device;
	notifySubscriberLeDevicesChangedbyScanId(scanId);
}

void BluetoothManagerAdapter::deviceLinkKeyCreated(const std::string &address, BluetoothLinkKey LinkKey)
{
	BT_DEBUG("Link Key of device(%s) is created", address.c_str());

	mLinkKeys.insert(std::pair<std::string, BluetoothLinkKey>(address, LinkKey));
}

void BluetoothManagerAdapter::deviceLinkKeyDestroyed(const std::string &address, BluetoothLinkKey LinkKey)
{
	BT_DEBUG("Link Key of device(%s) is created", address.c_str());

	auto linkKeyIter = mLinkKeys.find(address);
	if (linkKeyIter == mLinkKeys.end())
		return;

	mLinkKeys.erase(linkKeyIter);
}

bool BluetoothManagerAdapter::setState(LS::Message &request, pbnjson::JValue &requestObj)
{
	BluetoothPropertiesList propertiesToChange;

	if (requestObj.hasKey("discoveryTimeout"))
	{
		int32_t discoveryTO = requestObj["discoveryTimeout"].asNumber<int32_t>();

		if (discoveryTO < 0)
		{
				LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_DISCOVERY_TO_NEG_VALUE) + std::to_string(discoveryTO), BT_ERR_DISCOVERY_TO_NEG_VALUE);
				return true;
		}
		else
		{
			uint32_t discoveryTimeout = (uint32_t) discoveryTO;
			if (discoveryTimeout != getDisoveryTimeout())
			{
				propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::DISCOVERY_TIMEOUT, discoveryTimeout));
			}
		}
	}

	if (requestObj.hasKey("discoverableTimeout"))
	{
		int32_t discoverableTO = requestObj["discoverableTimeout"].asNumber<int32_t>();

		if (discoverableTO < 0)
		{
			LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_DISCOVERABLE_TO_NEG_VALUE) + std::to_string(discoverableTO), BT_ERR_DISCOVERABLE_TO_NEG_VALUE);
			return true;
		}
		else
		{
			uint32_t discoverableTimeout = (uint32_t) discoverableTO;
			if (discoverableTimeout != mDiscoverableTimeout)
			{
				propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::DISCOVERABLE_TIMEOUT, (uint32_t) discoverableTimeout));
			}
		}
	}

	if (requestObj.hasKey("pairableTimeout"))
	{
		int32_t pairableTO = requestObj["pairableTimeout"].asNumber<int32_t>();

		if (pairableTO < 0)
		{
			LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_PAIRABLE_TO_NEG_VALUE) + std::to_string(pairableTO), BT_ERR_PAIRABLE_TO_NEG_VALUE);
			return true;
		}
		else
		{
			uint32_t pairableTimeout = (uint32_t) pairableTO;
			if (pairableTimeout != mPairState.getPairableTimeout())
			{
				propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::PAIRABLE_TIMEOUT, (uint32_t) pairableTimeout));
			}
		}
	}

	if (requestObj.hasKey("powered"))
	{
		bool powered = requestObj["powered"].asBool();

		if(powered != mPowered)
		{
			BluetoothError error;

            BT_INFO("Manager", 0, "%s = powered :%d", mAddress.c_str(), powered );

			if (powered)
				error = mAdapter->enable();
			else
				error = mAdapter->disable();

			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(request, BT_ERR_POWER_STATE_CHANGE_FAIL);
				return true;
			}
		}
	}

	if (requestObj.hasKey("name"))
	{
		std::string name = requestObj["name"].asString();

		if (name.compare(mName) != 0)
		{
			propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::ALIAS, name));
		}
	}

	if (requestObj.hasKey("discoverable"))
	{
		bool discoverable = requestObj["discoverable"].asBool();

		if (discoverable != mDiscoverable)
		{
			propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::DISCOVERABLE, discoverable));
		}
	}

	if (requestObj.hasKey("pairable"))
	{
		bool pairable = requestObj["pairable"].asBool();

		if (pairable != mPairState.isPairable())
		{
			propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::PAIRABLE, pairable));
		}
	}

	// if we don't have any properties to set we can just respond to the caller
	if (propertiesToChange.size() == 0)
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", mAddress);

		LSUtils::postToClient(request, responseObj);
	}
	else
	{
		BT_INFO("MANAGER_SERVICE", 0, "Service calls SIL API : setAdapterProperties");
		mAdapter->setAdapterProperties(propertiesToChange, std::bind(&BluetoothManagerAdapter::handleStatePropertiesSet, this, propertiesToChange, request, _1));
	}

	return true;
}

void BluetoothManagerAdapter::handleStatePropertiesSet(BluetoothPropertiesList properties, LS::Message &request, BluetoothError error)
{
	BT_INFO("MANAGER_SERVICE", 0, "Return of handleStatePropertiesSet is %d", error);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(request, responseObj);
}

void BluetoothManagerAdapter::handleDeviceStatePropertiesSet(BluetoothPropertiesList properties, BluetoothDevice *device, LS::Message &request, BluetoothError error)
{
	BT_INFO("MANAGER_SERVICE", 0, "Return of handleDeviceStatePropertiesSet is %d", error);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", mAddress);
	if (device && device->update(properties))
		responseObj.put("returnValue", true);
	else
		responseObj.put("returnValue", false);

	LSUtils::postToClient(request, responseObj);
}

void BluetoothManagerAdapter::appendFilteringDevices(std::string senderName, pbnjson::JValue &object)
{
	pbnjson::JValue devicesObj = pbnjson::Array();

	for (auto deviceIter : mDevices)
	{
		auto device = deviceIter.second;
		pbnjson::JValue deviceObj = pbnjson::Object();

		auto filterClassOfDevices = mFilterClassOfDevices.find(senderName);
		if (filterClassOfDevices != mFilterClassOfDevices.end())
			if((((int32_t)(filterClassOfDevices->second) & (int32_t)(device->getClassOfDevice())) != (int32_t)(filterClassOfDevices->second)))
				continue;

		if(device->getTypeAsString() == "bredr")
		{
			if(mFilterUuids.size() > 0)
			{
				auto filterUuid = mFilterUuids.find(senderName);
				if(filterUuid->second.c_str() != NULL)
				{
					auto uuidIter = std::find(device->getUuids().begin(), device->getUuids().end(), filterUuid->second);
					if (filterUuid != mFilterUuids.end() && uuidIter != device->getUuids().end())
						continue;
				}
			}
		}

        if ( device->getName().find("LGE MR") != std::string::npos ) {
            BT_INFO("Manager", 0, "name: %s, address: %s, paired: %d, rssi: %d, blocked: %d\n", device->getName().c_str(), device->getAddress().c_str(), device->getPaired(), device->getRssi(), device->getBlocked());
        }

		deviceObj.put("name", device->getName());
		deviceObj.put("address", device->getAddress());
		deviceObj.put("typeOfDevice", device->getTypeAsString());
		deviceObj.put("classOfDevice", (int32_t) device->getClassOfDevice());
		deviceObj.put("paired", device->getPaired());
		deviceObj.put("pairing", device->getPairing());
		deviceObj.put("trusted", device->getTrusted());
		deviceObj.put("blocked", device->getBlocked());
		deviceObj.put("rssi", device->getRssi());

		if(device->getPaired())
			deviceObj.put("adapterAddress", mAddress);
		else
			deviceObj.put("adapterAddress", "");

		appendManufacturerData(deviceObj, device->getManufacturerData());
		appendSupportedServiceClasses(deviceObj, device->getSupportedServiceClasses());
		appendConnectedProfiles(deviceObj, device->getAddress());
		devicesObj.append(deviceObj);
	}

	object.put("devices", devicesObj);
}

void BluetoothManagerAdapter::appendLeDevices(pbnjson::JValue &object)
{
	pbnjson::JValue devicesObj = pbnjson::Array();


	for (auto deviceIter : mLeDevices)
	{
		auto device = deviceIter.second;
		pbnjson::JValue deviceObj = pbnjson::Object();

		deviceObj.put("address", device->getAddress());
		deviceObj.put("rssi", device->getRssi());
		deviceObj.put("adapterAddress", mAddress);

		appendScanRecord(deviceObj, device->getScanRecord());
		devicesObj.append(deviceObj);
	}

	object.put("devices", devicesObj);
}

void BluetoothManagerAdapter::appendLeDevicesByScanId(pbnjson::JValue &object, uint32_t scanId)
{
	auto devicesIter = mLeDevicesByScanId.find(scanId);
	if (devicesIter == mLeDevicesByScanId.end())
		return;

	std::unordered_map<std::string, BluetoothDevice*> devices = devicesIter->second;
	pbnjson::JValue devicesObj = pbnjson::Array();

	for (auto deviceIter : devices)
	{
		auto device = deviceIter.second;
		pbnjson::JValue deviceObj = pbnjson::Object();

        if(!device->getName().compare("LGE MR18")) {
            BT_INFO("Manager", 0, "name: %s, address: %s, paired: %d, rssi: %d, blocked: %d\n", device->getName().c_str(), device->getAddress().c_str(), device->getPaired(), device->getRssi(), device->getBlocked());
        }

		deviceObj.put("name", device->getName());
		deviceObj.put("address", device->getAddress());
		deviceObj.put("typeOfDevice", device->getTypeAsString());
		deviceObj.put("classOfDevice", (int32_t) device->getClassOfDevice());
		deviceObj.put("paired", device->getPaired());
		deviceObj.put("pairing", device->getPairing());
		deviceObj.put("trusted", device->getTrusted());
		deviceObj.put("blocked", device->getBlocked());
		deviceObj.put("rssi", device->getRssi());

		if(device->getPaired())
			deviceObj.put("adapterAddress", mAddress);
		else
			deviceObj.put("adapterAddress", "");

		appendManufacturerData(deviceObj, device->getManufacturerData());
		appendScanRecord(deviceObj, device->getScanRecord());
		appendSupportedServiceClasses(deviceObj, device->getSupportedServiceClasses());
		appendConnectedProfiles(deviceObj, device->getAddress());
		devicesObj.append(deviceObj);
	}

	object.put("devices", devicesObj);
}

void BluetoothManagerAdapter::appendDevices(pbnjson::JValue &object)
{
	pbnjson::JValue devicesObj = pbnjson::Array();

	for (auto deviceIter : mDevices)
	{
		auto device = deviceIter.second;
		pbnjson::JValue deviceObj = pbnjson::Object();

		if(!device->getName().compare("LGE MR18")) {
			BT_INFO("Manager", 0, "name: %s, address: %s, paired: %d, rssi: %d, blocked: %d\n", device->getName().c_str(), device->getAddress().c_str(), device->getPaired(), device->getRssi(),
					device->getBlocked());
		}

		deviceObj.put("name", device->getName());
		deviceObj.put("address", device->getAddress());
		deviceObj.put("typeOfDevice", device->getTypeAsString());
		deviceObj.put("classOfDevice", (int32_t) device->getClassOfDevice());
		deviceObj.put("paired", device->getPaired());
		deviceObj.put("pairing", device->getPairing());
		deviceObj.put("trusted", device->getTrusted());
		deviceObj.put("blocked", device->getBlocked());
		deviceObj.put("rssi", device->getRssi());


		deviceObj.put("adapterAddress", getAddress());

		appendManufacturerData(deviceObj, device->getManufacturerData());
		appendSupportedServiceClasses(deviceObj, device->getSupportedServiceClasses());
		appendConnectedProfiles(deviceObj, device->getAddress());
		appendScanRecord(deviceObj, device->getScanRecord());
		devicesObj.append(deviceObj);
	}

	object.put("devices", devicesObj);
}

void BluetoothManagerAdapter::appendScanRecord(pbnjson::JValue &object, const std::vector<uint8_t> scanRecord)
{
	pbnjson::JValue scanRecordArray = pbnjson::Array();

	for (unsigned int i = 0; i < scanRecord.size(); i++)
		scanRecordArray.append(scanRecord[i]);

	object.put("scanRecord", scanRecordArray);
}

void BluetoothManagerAdapter::appendManufacturerData(pbnjson::JValue &object, const std::vector<uint8_t> manufacturerData)
{
	pbnjson::JValue manufacturerDataObj = pbnjson::Object();
	unsigned int i = 0;

	if(manufacturerData.size() > 2)
	{
		pbnjson::JValue idArray = pbnjson::Array();
		for (i = 0; i < 2; i++)
			idArray.append(manufacturerData[i]);

		pbnjson::JValue dataArray = pbnjson::Array();
		for (i = 2; i < manufacturerData.size(); i++)
			dataArray.append(manufacturerData[i]);

		manufacturerDataObj.put("companyId", idArray);
		manufacturerDataObj.put("data", dataArray);
	}

	object.put("manufacturerData", manufacturerDataObj);
}

void BluetoothManagerAdapter::appendSupportedServiceClasses(pbnjson::JValue &object, const std::vector<BluetoothServiceClassInfo> &supportedServiceClasses)
{
	pbnjson::JValue supportedProfilesObj = pbnjson::Array();

	for (auto profile : supportedServiceClasses)
	{
		pbnjson::JValue profileObj = pbnjson::Object();

		profileObj.put("mnemonic", profile.getMnemonic());

		// Only set the category if we have one. If we don't have one then the
		// profile doesn't have any support in here and we don't need to expose
		// a non existing category name
		std::string category = profile.getMethodCategory();
		if (!category.empty())
			profileObj.put("category", profile.getMethodCategory());

		supportedProfilesObj.append(profileObj);
	}

	object.put("serviceClasses", supportedProfilesObj);
}

void BluetoothManagerAdapter::appendConnectedProfiles(pbnjson::JValue &object, const std::string deviceAddress)
{
	pbnjson::JValue connectedProfilesObj = pbnjson::Array();

	for (auto profile : mBluetoothManagerService->getProfiles())
	{
		if (profile->isDeviceConnected(deviceAddress))
			connectedProfilesObj.append(convertToLower(profile->getName()));
	}

	object.put("connectedProfiles", connectedProfilesObj);
}

bool BluetoothManagerAdapter::startDiscovery(LS::Message &request, pbnjson::JValue &requestObj)
{
	if (!mPowered)
	{
		LSUtils::respondWithError(request, BT_ERR_START_DISC_ADAPTER_OFF_ERR);
		return true;
	}

	BluetoothError error = BLUETOOTH_ERROR_NONE;
	// Outgoing pairing performs in two steps, cancelDiscovery() and pair().
	// startDiscovery request in the middle of pairing must be ignored.
	if (!mPairState.isPairing())
		error = mAdapter->startDiscovery();

	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerAdapter::cancelDiscovery(LS::Message &request)
{
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	mAdapter->cancelDiscovery([requestMessage, this](BluetoothError error) {

	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(requestMessage, BT_ERR_STOP_DISC_FAIL);
	}
	else
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", mAddress);
		LSUtils::postToClient(requestMessage, responseObj);
	}

	const char* senderName = LSMessageGetApplicationID(requestMessage);
	if( senderName == NULL )
	{
		senderName = LSMessageGetSenderServiceName(requestMessage);
	}

	if(senderName != NULL)
	{
		auto watchIter = mGetDevicesWatches.find(senderName);
		if (watchIter == mGetDevicesWatches.end())
			return;

		LSUtils::ClientWatch *watch = watchIter->second;
		mGetDevicesWatches.erase(watchIter);
		delete watch;
	}
	});

	return true;
}

bool BluetoothManagerAdapter::getLinkKey(LS::Message &request, pbnjson::JValue &requestObj)
{
	std::string address = requestObj["address"].asString();
	auto device = findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	auto linkKey = findLinkKey(address);

	pbnjson::JValue linkKeyArray = pbnjson::Array();
	for (size_t i=0; i < linkKey.size(); i++)
		linkKeyArray.append((int32_t) linkKey[i]);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("address", address);
	responseObj.put("linkKey", linkKeyArray);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerAdapter::getFilteringDeviceStatus(LS::Message &request, pbnjson::JValue &requestObj)
{
	bool subscribed = false;

	std::string appName = mBluetoothManagerService->getMessageOwner(request.get());

	if (appName.compare("") == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING, true);
		return true;
    }

	const char* senderName = LSMessageGetApplicationID(request.get());
	if(senderName == NULL)
	{
		senderName = LSMessageGetSenderServiceName(request.get());
		if(senderName == NULL)
		{
			LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("classOfDevice"))
	{
		if(mFilterClassOfDevices.find(appName) != mFilterClassOfDevices.end())
			mFilterClassOfDevices[appName] = requestObj["classOfDevice"].asNumber<int32_t>();
		else
			mFilterClassOfDevices.insert(std::pair<std::string, int32_t>(appName, requestObj["classOfDevice"].asNumber<int32_t>()));
	}
	else
	{
		if(mFilterClassOfDevices.find(appName) != mFilterClassOfDevices.end())
			mFilterClassOfDevices[appName] = 0;
		else
			mFilterClassOfDevices.insert(std::pair<std::string, int32_t>(appName, 0));
	}

	if (requestObj.hasKey("uuid"))
	{
		if(mFilterUuids.find(appName) != mFilterUuids.end())
			mFilterUuids[appName] = requestObj["uuid"].asString();
		else
			mFilterUuids.insert(std::pair<std::string, std::string>(appName, requestObj["uuid"].asString()));
	}
	else
	{
		if(mFilterUuids.find(appName) != mFilterUuids.end())
			mFilterUuids[appName] = std::string();
		else
			mFilterUuids.insert(std::pair<std::string, std::string>(appName, std::string()));
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	if (request.isSubscription())
	{
		LSUtils::ClientWatch *watch = new LSUtils::ClientWatch(mBluetoothManagerService->get(), request.get(), nullptr);
		if(mGetDevicesWatches.find(senderName) != mGetDevicesWatches.end())
			mGetDevicesWatches[senderName] = watch;
		else
			mGetDevicesWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(senderName, watch));

		subscribed = true;
	}

	appendFilteringDevices(senderName, responseObj);

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerAdapter::getDeviceStatus(LS::Message &request, pbnjson::JValue &requestObj)
{
	bool subscribed = false;

	if (request.isSubscription())
	{
		mGetDevicesSubscriptions.subscribe(request);
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	appendDevices(responseObj);

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerAdapter::setDeviceState(LS::Message &request, pbnjson::JValue &requestObj)
{
	BluetoothPropertiesList propertiesToChange;
	std::string address = requestObj["address"].asString();

	auto device = findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (requestObj.hasKey("trusted"))
	{
		bool trusted = requestObj["trusted"].asBool();
		trusted = requestObj["trusted"].asBool();

		if (trusted != device->getTrusted())
			propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::TRUSTED, trusted));
	}

	if (requestObj.hasKey("blocked"))
	{
		bool blocked = requestObj["blocked"].asBool();

		if (blocked != device->getBlocked())
			propertiesToChange.push_back(BluetoothProperty(BluetoothProperty::Type::BLOCKED, blocked));
	}

	if (propertiesToChange.size() == 0)
		LSUtils::respondWithError(request, BT_ERR_NO_PROP_CHANGE);
	else
		mAdapter->setDeviceProperties(address, propertiesToChange, std::bind(&BluetoothManagerAdapter::handleDeviceStatePropertiesSet, this, propertiesToChange, device, request, _1));

	return true;
}

bool BluetoothManagerAdapter::pair(LS::Message &request, pbnjson::JValue &requestObj)
{
	if(mOutgoingPairingWatch)
	{
		LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
		return true;
	}

	if (mPairState.isPairing())
	{
		LSUtils::respondWithError(request, BT_ERR_PAIRING_IN_PROG);
		return true;
	}

	std::string address = requestObj["address"].asString();

	BluetoothDevice *device = findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (device->getPaired())
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_DEVICE_ALREADY_PAIRED);
		return true;
	}

	mOutgoingPairingWatch =  new LSUtils::ClientWatch(mBluetoothManagerService->get(), request.get(), [this]() {
		notifyPairingListenerDropped(false);
	});

	mPairState.markAsOutgoing();

	// We have to send a response to the client immediately
	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	startPairing(device);

	return true;
}

bool BluetoothManagerAdapter::supplyPasskey(LS::Message &request, pbnjson::JValue &requestObj)
{
	std::string address = requestObj["address"].asString();
	uint32_t passkey = requestObj["passkey"].asNumber<int32_t>();

	BluetoothError error = mAdapter->supplyPairingSecret(address, passkey);
	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("returnValue", true);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	if (mPairState.isIncoming())
		stopPairing();

	return true;
}

bool BluetoothManagerAdapter::supplyPinCode(LS::Message &request, pbnjson::JValue &requestObj)
{
	std::string address = requestObj["address"].asString();
	std::string pin = requestObj["pin"].asString();

	BluetoothError error = mAdapter->supplyPairingSecret(address, pin);
	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("returnValue", true);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	if (mPairState.isIncoming())
		stopPairing();

	return true;
}

bool BluetoothManagerAdapter::supplyPasskeyConfirmation(LS::Message &request, pbnjson::JValue &requestObj)
{
	if (!mPairState.isPairing())
	{
		LSUtils::respondWithError(request, BT_ERR_NO_PAIRING);
		return true;
	}

	std::string address = requestObj["address"].asString();
	bool accept = requestObj["accept"].asBool();

	BluetoothError error = mAdapter->supplyPairingConfirmation(address, accept);

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("returnValue", true);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	// For an incoming pairing request we're done at this point. Either
	// the user accepted the pairing request or not but we don't have to
	// track that anymore. Service users will get notified about a newly
	// paired device once its state switched to paired.
	if (mPairState.isIncoming())
		stopPairing();

	return true;
}

bool BluetoothManagerAdapter::cancelPairing(LS::Message &request, pbnjson::JValue &requestObj)
{
	if (!mPairState.isPairing())
	{
		LSUtils::respondWithError(request, BT_ERR_NO_PAIRING);
		return true;
	}

	std::string address = requestObj["address"].asString();
	auto device = findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (mPairState.getDevice()->getAddress() != address)
	{
		LSUtils::respondWithError(request, BT_ERR_NO_PAIRING_FOR_REQUESTED_ADDRESS);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto cancelPairingCallback = [this, requestMessage, device](BluetoothError error) {
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("returnValue", true);

		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);

		pbnjson::JValue subscriptionResponseObj = pbnjson::Object();

		if (BLUETOOTH_ERROR_NONE == error)
		{
			BT_DEBUG("Cancel pairing success");
			// When an incoming pairing request is canceled we don't drop the
			// subscription
			subscriptionResponseObj.put("adapterAddress", mAddress);
			subscriptionResponseObj.put("subscribed", mPairState.isIncoming());
			subscriptionResponseObj.put("returnValue", false);
			subscriptionResponseObj.put("request", "endPairing");
			subscriptionResponseObj.put("errorCode", (int32_t)BT_ERR_PAIRING_CANCELED);
			subscriptionResponseObj.put("errorText", retrieveErrorText(BT_ERR_PAIRING_CANCELED));
		}
		else
		{
			BT_DEBUG("Cancel pairing failed");
			subscriptionResponseObj.put("adapterAddress", mAddress);
			subscriptionResponseObj.put("subscribed", true);
			subscriptionResponseObj.put("returnValue", true);
			subscriptionResponseObj.put("request", "continuePairing");
		}

		if (mPairState.isOutgoing())
		{
			BT_DEBUG("Canceling outgoing pairing");
			if (mOutgoingPairingWatch)
				LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), subscriptionResponseObj);
		}
		else if (mPairState.isIncoming())
		{
			BT_DEBUG("Canceling incoming pairing");
			if(mIncomingPairingWatch)
				LSUtils::postToClient(mIncomingPairingWatch->getMessage(), subscriptionResponseObj);
		}

		if (BLUETOOTH_ERROR_NONE == error)
			stopPairing();
	};

	BT_DEBUG("Initiating cancel pair call to the SIL for address %s", address.c_str());
	mAdapter->cancelPairing(address, cancelPairingCallback);

	return true;
}

bool BluetoothManagerAdapter::unpair(LS::Message &request, pbnjson::JValue &requestObj)
{
	std::string address = requestObj["address"].asString();
	auto device = findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto unpairCallback = [requestMessage, this](BluetoothError error) {
		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_UNPAIR_FAIL);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", mAdapter);

		LSUtils::postToClient(requestMessage, responseObj);

		LSMessageUnref(requestMessage);
	};

	getAdapter()->unpair(address, unpairCallback);

	return true;
}

bool BluetoothManagerAdapter::awaitPairingRequests(LS::Message &request, pbnjson::JValue &requestObj)
{
	if(mIncomingPairingWatch)
	{
		LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
		return true;
	}

	mIncomingPairingWatch = new LSUtils::ClientWatch(mBluetoothManagerService->get(), request.get(), [this]() {
		notifyPairingListenerDropped(true);
	});

	pbnjson::JValue responseObj = pbnjson::Object();

	if (mBluetoothManagerService->setPairableState(mAddress, true))
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
	else
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("subscribed", false);
		responseObj.put("returnValue", false);
		responseObj.put("errorCode", (int32_t)BT_ERR_PAIRABLE_FAIL);
		responseObj.put("errorText", retrieveErrorText(BT_ERR_PAIRABLE_FAIL));
		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}

	return true;
}

void BluetoothManagerAdapter::requestPairingSecret(const std::string &address, BluetoothPairingSecretType type)
{
		pbnjson::JValue responseObj = pbnjson::Object();

	// If we're not pairing yet then this is a pairing request from a remote device
	if (!mPairState.isPairing())
	{
		beginIncomingPair(address);
	}

	if (type == BLUETOOTH_PAIRING_SECRET_TYPE_PASSKEY)
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		responseObj.put("address", address);
		responseObj.put("request", "enterPasskey");
	}
	else if (type == BLUETOOTH_PAIRING_SECRET_TYPE_PIN)
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		responseObj.put("address", address);
		responseObj.put("request", "enterPinCode");
	}

	if (mPairState.isIncoming() && mIncomingPairingWatch)
	{
		auto device = findDevice(address);
		if (device)
			responseObj.put("name", device->getName());

		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
	else if (mPairState.isOutgoing() && mOutgoingPairingWatch)
	{
		LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
	}
	else
	{
		stopPairing();
	}
}

void BluetoothManagerAdapter::displayPairingConfirmation(const std::string &address, BluetoothPasskey passkey)
{
	BT_DEBUG("Received display pairing confirmation request from SIL for address %s, passkey %d", address.c_str(), passkey);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("request", "confirmPasskey");
	responseObj.put("passkey", (int) passkey);

	// If we're not pairing yet then this is a pairing request from a remote device
	if (!getPairState().isPairing())
	{
		beginIncomingPair(address);
		responseObj.put("address", address);
	}

	responseObj.put("adapterAddress", mAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);

	if (getPairState().isIncoming() && mIncomingPairingWatch)
	{
		auto device = findDevice(address);
		if (device)
			responseObj.put("name", device->getName());

		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
	else if (getPairState().isOutgoing() && mOutgoingPairingWatch)
	{
		LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
	}
	else
	{
		stopPairing();
	}
}

void BluetoothManagerAdapter::pairingCanceled()
{
	BT_DEBUG ("Pairing has been canceled from remote user");
	if (!(mPairState.isPairing()))
		return;

	pbnjson::JValue subscriptionResponseObj = pbnjson::Object();
	subscriptionResponseObj.put("adapterAddress", mAddress);
	subscriptionResponseObj.put("subscribed", true);
	subscriptionResponseObj.put("returnValue", false);
	subscriptionResponseObj.put("request", "endPairing");
	subscriptionResponseObj.put("errorCode", (int32_t)BT_ERR_PAIRING_CANCEL_TO);
	subscriptionResponseObj.put("errorText", retrieveErrorText(BT_ERR_PAIRING_CANCEL_TO));

	if (mPairState.isIncoming() && mIncomingPairingWatch)
		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), subscriptionResponseObj);

	if (mPairState.isOutgoing() && mOutgoingPairingWatch)
		LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), subscriptionResponseObj);

	stopPairing();
}

void BluetoothManagerAdapter::displayPairingSecret(const std::string &address, const std::string &pin)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	// If we're not pairing yet then this is a pairing request from a remote device
	if (!mPairState.isPairing())
	{
		beginIncomingPair(address);
	}
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("request", "displayPinCode");
	responseObj.put("address", address);
	responseObj.put("pin", pin);

	if (mPairState.isIncoming() && mIncomingPairingWatch)
	{
		auto device = findDevice(address);
		if (device)
			responseObj.put("name", device->getName());

		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
	else if (mPairState.isOutgoing() && mOutgoingPairingWatch)
	{
		LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
	}
	else
	{
		stopPairing();
	}
}

void BluetoothManagerAdapter::displayPairingSecret(const std::string &address, BluetoothPasskey passkey)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	// If we're not pairing yet then this is a pairing request from a remote device
	if (!mPairState.isPairing())
	{
		beginIncomingPair(address);
	}
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("request", "displayPasskey");
	responseObj.put("address", address);
	responseObj.put("passkey", (int)passkey);

	if (mPairState.isIncoming() && mIncomingPairingWatch)
	{
		auto device = findDevice(address);
		if (device)
			responseObj.put("name", device->getName());

		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
	else if (mPairState.isOutgoing() && mOutgoingPairingWatch)
	{
		LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
	}
	else
	{
		stopPairing();
	}
}

void BluetoothManagerAdapter::beginIncomingPair(const std::string &address)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	BT_DEBUG("%s: address %s", __func__, address.c_str());

	if (mPairState.isPairing())
	{
		BT_WARNING(MSGID_INCOMING_PAIR_REQ_FAIL, 0, "Incoming pairing request received but cannot process since we are pairing with another device");
		return;
	}

	if (!mIncomingPairingWatch)
		return;

	auto device = findDevice(address);
	if (device)
	{
		mPairState.markAsIncoming();

		responseObj.put("adapterAddress", mAddress);
		responseObj.put("request", "incomingPairRequest");
		responseObj.put("address", address);
		responseObj.put("name", device->getName());
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);

		startPairing(device);
	}
	else
	{
		responseObj.put("adapterAddress", mAddress);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", false);
		responseObj.put("errorText", retrieveErrorText(BT_ERR_INCOMING_PAIR_DEV_UNAVAIL));
		responseObj.put("errorCode", (int32_t)BT_ERR_INCOMING_PAIR_DEV_UNAVAIL);
		LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
	}
}

void BluetoothManagerAdapter::abortPairing(bool incoming)
{
	bool cancelPairing = false;

	BT_DEBUG("Abort pairing");

	if (incoming)
	{
		// Pairable should always be true for a device with no input and output
		// simple pairs in that case

		/*
		 * Based on the problem described in PLAT-9396, we comment this part to
		 * maintain the pairing status even when user quit subscribing awaitPairingRequest
		 * Once EMS (Event Monitoring Service) is introduced in the build later, we can
		 * uncomment this part.
		 * For now, to maintin the functionality of incoming pairing using com.webos.service.bms,
		 * this routine will be commented. Check PLAT-9396 for more detail.
		 * PLAT-9808 is created to recover this later.
		if (mPairingIOCapability != BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT)
			setPairableState(false);

		if (mPairState.isPairing() && mPairState.isIncoming())
			cancelPairing = true;
		*/

		if (mIncomingPairingWatch)
		{
			delete mIncomingPairingWatch;
			mIncomingPairingWatch = 0;
		}
	}
	else
	{
		if (mPairState.isPairing() && mPairState.isOutgoing())
			cancelPairing = true;

		if (mOutgoingPairingWatch)
		{
			delete mOutgoingPairingWatch;
			mOutgoingPairingWatch = 0;
		}
	}

	if (cancelPairing)
	{
		// No need to call handleCancelResponse as callback, since we lost the subscriber and
		// we need not respond to the subscriber anymore.

		auto abortPairingCb = [this](BluetoothError error) {
			if (BLUETOOTH_ERROR_NONE == error)
			{
				BT_DEBUG("Pairing has been aborted");
			}
		};

		BluetoothDevice *device = mPairState.getDevice();
		if (device && mAdapter)
			mAdapter->cancelPairing(device->getAddress(), abortPairingCb);

		stopPairing();
	}
}

bool BluetoothManagerAdapter::notifyPairingListenerDropped(bool incoming)
{
	BT_DEBUG("Pairing listener dropped (incoming %d)", incoming);

	if ((incoming && mIncomingPairingWatch) || (!incoming && mOutgoingPairingWatch))
		abortPairing(incoming);

	return true;
}

void BluetoothManagerAdapter::notifyStartScanListenerDropped(uint32_t scanId)
{
	BT_DEBUG("StartScan listener dropped");

	auto watchIter = mStartScanWatches.find(scanId);
	if (watchIter == mStartScanWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", false);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(watch->getMessage(), responseObj);

	mStartScanWatches.erase(watchIter);
	delete watch;

	mAdapter->removeLeDiscoveryFilter(scanId);

	if (mStartScanWatches.size() == 0)
		mAdapter->cancelLeDiscovery();
}

void BluetoothManagerAdapter::cancelDiscoveryCallback(BluetoothDevice *device, BluetoothError error)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	if (error != BLUETOOTH_ERROR_NONE)
	{
		BT_DEBUG("%s: Error is %d", __func__, error);
		if (mPairState.isOutgoing() && mOutgoingPairingWatch)
		{
			responseObj.put("adapterAddress", mAddress);
			responseObj.put("subscribed", false);
			responseObj.put("returnValue", false);
			responseObj.put("errorText", retrieveErrorText(BT_ERR_STOP_DISC_FAIL));
			responseObj.put("errorCode", (int32_t)BT_ERR_STOP_DISC_FAIL);
			LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);

			stopPairing();

			delete mOutgoingPairingWatch;
			mOutgoingPairingWatch = 0;
		}

		if (mPairState.isIncoming() && mIncomingPairingWatch)
		{
			responseObj.put("adapterAddress", mAddress);
			responseObj.put("subscribed", true);
			responseObj.put("returnValue", false);
			responseObj.put("errorText", retrieveErrorText(BT_ERR_STOP_DISC_FAIL));
			responseObj.put("errorCode", (int32_t)BT_ERR_STOP_DISC_FAIL);
			LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);
		}
	}
	else
	{
		BT_DEBUG("%s: No error", __func__);
		if (mPairState.isOutgoing() && mOutgoingPairingWatch)
		{
			// Make sure discovery is canceled
			if (!getDiscoveringState())
			{
				BT_DEBUG("%s: Discovery state is disabled", __func__);
				std::string address = device->getAddress();
				auto pairCallback = [this](BluetoothError error)
				{
					pbnjson::JValue responseObj = pbnjson::Object();
					BT_DEBUG("Outgoing pairing process finished");

					if (!mPairState.isPairing())
						return;

					if (BLUETOOTH_ERROR_NONE == error)
					{
						responseObj.put("adapterAddress", mAddress);
						responseObj.put("subscribed", false);
						responseObj.put("returnValue", true);
						responseObj.put("request", "endPairing");
					}
					else
					{
						responseObj.put("adapterAddress", mAddress);
						responseObj.put("subscribed", false);
						responseObj.put("request", "endPairing");
						appendErrorResponse(responseObj, error);
					}
					stopPairing();

					if (mOutgoingPairingWatch)
					{
						LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
						delete mOutgoingPairingWatch;
						mOutgoingPairingWatch = 0;
					}
				};

				mAdapter->pair(address, pairCallback);
			}
			else
			{
				BT_DEBUG("%s: No error, but discovery state is still enabled", __func__);
				responseObj.put("adapterAddress", mAddress);
				responseObj.put("subscribed", false);
				responseObj.put("returnValue", false);
				responseObj.put("errorText", retrieveErrorText(BT_ERR_STOP_DISC_FAIL));
				responseObj.put("errorCode", (int32_t)BT_ERR_STOP_DISC_FAIL);

				stopPairing();

				LSUtils::postToClient(mOutgoingPairingWatch->getMessage(), responseObj);
				delete mOutgoingPairingWatch;
				mOutgoingPairingWatch = 0;
			}
		}
	}
}

void BluetoothManagerAdapter::startPairing(BluetoothDevice *device)
{
	getPairState().startPairing(device);
	mBluetoothManagerService->notifySubscribersAboutStateChange();
	notifySubscribersFilteredDevicesChanged();
	notifySubscribersDevicesChanged();

	// Device discovery needs to be stopped for pairing
	getAdapter()->cancelDiscovery(std::bind(&BluetoothManagerAdapter::cancelDiscoveryCallback, this, device, _1));
}

void BluetoothManagerAdapter::stopPairing()
{
	getPairState().stopPairing();

	mBluetoothManagerService->notifySubscribersAboutStateChange();
	notifySubscribersFilteredDevicesChanged();
	notifySubscribersDevicesChanged();
}

void BluetoothManagerAdapter::cancelIncomingPairingSubscription()
{
	BT_DEBUG("Cancel incoming pairing subscription since pairable timeout has reached");

	// Pairable should always be true for a device with no input and output - simple pairs in that case
	if (mPairState.isPairable() && (mBluetoothManagerService->getIOPairingCapability() != BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT))
	{
		if (mIncomingPairingWatch)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("adapterAddress", mAddress);
			responseObj.put("subscribed", false);
			responseObj.put("returnValue", false);
			responseObj.put("errorText", retrieveErrorText(BT_ERR_PAIRABLE_TO));
			responseObj.put("errorCode", (int32_t)BT_ERR_PAIRABLE_TO);
			LSUtils::postToClient(mIncomingPairingWatch->getMessage(), responseObj);

			delete mIncomingPairingWatch;
			mIncomingPairingWatch = nullptr;
		}

		mBluetoothManagerService->setPairableState(mAddress, false);
		if (mPairState.isPairing())
			stopPairing();
	}
}

bool BluetoothManagerAdapter::startScan(LS::Message &request, pbnjson::JValue &requestObj)
{
	int32_t leScanId = -1;
	bool subscribed = false;
	BluetoothLeDiscoveryFilter leFilter;
	BluetoothLeServiceUuid serviceUuid;
	BluetoothLeServiceData serviceData;
	BluetoothManufacturerData manufacturerData;

	if (requestObj.hasKey("address"))
	{
		std::string address = requestObj["address"].asString();
		leFilter.setAddress(address);
	}

	if (requestObj.hasKey("name"))
	{
		std::string name = requestObj["name"].asString();
		leFilter.setName(name);
	}

	if (requestObj.hasKey("serviceUuid"))
	{
		pbnjson::JValue serviceUuidObj = requestObj["serviceUuid"];

		if (serviceUuidObj.hasKey("uuid"))
		{
			std::string uuid = serviceUuidObj["uuid"].asString();
			serviceUuid.setUuid(uuid);
		}

		if (serviceUuidObj.hasKey("mask"))
		{
			std::string mask = serviceUuidObj["mask"].asString();
			serviceUuid.setMask(mask);
		}

		leFilter.setServiceUuid(serviceUuid);
	}

	if (requestObj.hasKey("serviceData"))
	{
		pbnjson::JValue serviceDataObj = requestObj["serviceData"];

		if (serviceDataObj.hasKey("uuid"))
		{
			std::string uuid = serviceDataObj["uuid"].asString();
			serviceData.setUuid(uuid);
		}

		if (serviceDataObj.hasKey("data"))
		{
			BluetoothLowEnergyData data;
			auto dataObjArray = serviceDataObj["data"];
			for (int n = 0; n < dataObjArray.arraySize(); n++)
			{
				pbnjson::JValue element = dataObjArray[n];
				data.push_back((uint8_t)element.asNumber<int32_t>());
			}

			serviceData.setData(data);
		}

		if (serviceDataObj.hasKey("mask"))
		{
			BluetoothLowEnergyMask mask;
			auto maskObjArray = serviceDataObj["mask"];
			for (int n = 0; n < maskObjArray.arraySize(); n++)
			{
				pbnjson::JValue element = maskObjArray[n];
				mask.push_back((uint8_t)element.asNumber<int32_t>());
			}

			serviceData.setMask(mask);
		}

		leFilter.setServiceData(serviceData);

	}

	if (requestObj.hasKey("manufacturerData"))
	{
		pbnjson::JValue manufacturerDataObj = requestObj["manufacturerData"];

		if (manufacturerDataObj.hasKey("id"))
		{
			int32_t id = manufacturerDataObj["id"].asNumber<int32_t>();
			manufacturerData.setId(id);
		}

		if (manufacturerDataObj.hasKey("data"))
		{
			BluetoothLowEnergyData data;
			auto dataObjArray = manufacturerDataObj["data"];
			for (int n = 0; n < dataObjArray.arraySize(); n++)
			{
				pbnjson::JValue element = dataObjArray[n];
				data.push_back((uint8_t)element.asNumber<int32_t>());
			}

			manufacturerData.setData(data);
		}

		if (manufacturerDataObj.hasKey("mask"))
		{
			BluetoothLowEnergyMask mask;
			auto maskObjArray = manufacturerDataObj["mask"];
			for (int n = 0; n < maskObjArray.arraySize(); n++)
			{
				pbnjson::JValue element = maskObjArray[n];
				mask.push_back((uint8_t)element.asNumber<int32_t>());
			}

			manufacturerData.setMask(mask);
		}

		leFilter.setManufacturerData(manufacturerData);
	}

	if (request.isSubscription())
	{
		leScanId = mAdapter->addLeDiscoveryFilter(leFilter);
		if (leScanId < 0)
		{
			LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
			return true;
		}

		LSUtils::ClientWatch *watch = new LSUtils::ClientWatch(mBluetoothManagerService->get(), request.get(),
		                    std::bind(&BluetoothManagerAdapter::notifyStartScanListenerDropped, this, leScanId));

		mStartScanWatches.insert(std::pair<uint32_t, LSUtils::ClientWatch*>(leScanId, watch));
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	BluetoothError error = BLUETOOTH_ERROR_NONE;

	if (mStartScanWatches.size() == 1)
		error = mAdapter->startLeDiscovery();

	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
		return true;
	}

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);
	responseObj.put("adapterAddress", mAddress);

	LSUtils::postToClient(request, responseObj);

	if (leScanId > 0)
		mAdapter->matchLeDiscoveryFilterDevices(leFilter, leScanId);

	return true;
}

void BluetoothManagerAdapter::leConnectionRequest(const std::string &address, bool state)
{
	mBluetoothManagerService->leConnectionRequest(address, state);
}
