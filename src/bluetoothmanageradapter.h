// Copyright (c) 2019-2020 LG Electronics, Inc.
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

#ifndef BLUETOOTH_MANAGER_ADAPTER_H
#define BLUETOOTH_MANAGER_ADAPTER_H

#include <string>
#include <unordered_map>
#include <vector>

#include <bluetooth-sil-api.h>

#include "bluetoothpairstate.h"

namespace LSUtils
{
	class ClientWatch;
}

namespace LS
{
	class Message;
	class ServerStatus;
}

class BluetoothManagerService;
class BluetoothDevice;
class BluetoothServiceClassInfo;

class BluetoothManagerAdapter: public BluetoothAdapterStatusObserver
{
public:
	BluetoothManagerAdapter(BluetoothManagerService *mngr, std::string address);
	~BluetoothManagerAdapter();

	void adapterStateChanged(bool powered);
	void adapterHciTimeoutOccurred();
	void discoveryStateChanged(bool active);
	void adapterPropertiesChanged(BluetoothPropertiesList properties);
	void adapterKeepAliveStateChanged(bool enabled);
	void deviceFound(BluetoothPropertiesList properties);
	void deviceFound(const std::string &address, BluetoothPropertiesList properties);
	void deviceRemoved(const std::string &address);
	void devicePropertiesChanged(const std::string &address, BluetoothPropertiesList properties);
	/*
	 * These will be deprecated if stack is ready to support scanId.
	 */
	void leDeviceFound(const std::string &address, BluetoothPropertiesList properties);
	void leDeviceRemoved(const std::string &address);
	void leDevicePropertiesChanged(const std::string &address, BluetoothPropertiesList properties);

	void leDeviceFoundByScanId(uint32_t scanId, BluetoothPropertiesList properties);
	void leDeviceRemovedByScanId(uint32_t scanId, const std::string &address);
	void deviceLinkKeyCreated(const std::string &address, BluetoothLinkKey LinkKey);
	void deviceLinkKeyDestroyed(const std::string &address, BluetoothLinkKey LinkKey);
	void leDevicePropertiesChangedByScanId(uint32_t scanId, const std::string &address, BluetoothPropertiesList properties);

	void requestPairingSecret(const std::string &address, BluetoothPairingSecretType type);
	void displayPairingSecret(const std::string &address, const std::string &pin);
	void displayPairingSecret(const std::string &address, BluetoothPasskey passkey);
	void displayPairingConfirmation(const std::string &address, BluetoothPasskey passkey);
	void pairingCanceled();
	void abortPairing(bool incoming);
	void leConnectionRequest(const std::string &address, bool state);

	bool startScan(LS::Message &request, pbnjson::JValue &requestObj);
	bool setState(LS::Message &request, pbnjson::JValue &requestObj);
	bool startDiscovery(LS::Message &request, pbnjson::JValue &requestObj);
	bool getLinkKey(LS::Message &request, pbnjson::JValue &requestObj);
	bool cancelDiscovery(LS::Message &request);
	bool pair(LS::Message &request, pbnjson::JValue &requestObj);
	bool supplyPasskey(LS::Message &request, pbnjson::JValue &requestObj);
	bool supplyPinCode(LS::Message &request, pbnjson::JValue &requestObj);
	bool supplyPasskeyConfirmation(LS::Message &request, pbnjson::JValue &requestObj);
	bool cancelPairing(LS::Message &request, pbnjson::JValue &requestObj);
	bool unpair(LS::Message &request, pbnjson::JValue &requestObj);
	bool awaitPairingRequests(LS::Message &request, pbnjson::JValue &requestObj);
	bool getFilteringDeviceStatus(LS::Message &request, pbnjson::JValue &requestObj);
	bool getConnectedDevices(LS::Message &request, pbnjson::JValue &requestObj);
	bool getDeviceStatus(LS::Message &request, pbnjson::JValue &requestObj);
	bool setDeviceState(LS::Message &request, pbnjson::JValue &requestObj);

	void handleStatePropertiesSet(BluetoothPropertiesList properties, LS::Message &request, BluetoothError error);
	void handleDeviceStatePropertiesSet(BluetoothPropertiesList properties, BluetoothDevice *device, LS::Message &request, BluetoothError error);

	void cancelIncomingPairingSubscription();

	void updateFromAdapterProperties(const BluetoothPropertiesList &properties);

	void setAdapter(BluetoothAdapter *adapter) { mAdapter = adapter; }
	BluetoothAdapter *getAdapter() const { return mAdapter; }

	std::string getName() { return mName; }
	std::string getStackName() const { return mStackName; }
	std::string getStackVersion() const { return mStackVersion; }
	std::string getFirmwareVersion() const { return mFirmwareVersion; }
	std::string getAddress() const { return mAddress; }
	uint32_t getDisoveryTimeout() const { return mDiscoveryTimeout; }
	bool getPowerState() const { return mPowered; }
	bool getDiscoverable() const { return mDiscoverable; }
	bool getDiscoveringState() const { return mDiscovering; }
	void setDefaultAdapter(bool isDefault) { mIsDefault = isDefault; }
	bool isDefaultAdapter() const { return mIsDefault; }
	uint32_t getDiscoverableTimeout() const { return mDiscoverableTimeout; }
	uint32_t getClassOfDevice() const { return mClassOfDevice; }
	BluetoothPairState& getPairState() { return mPairState; }

#ifdef MULTI_SESSION_SUPPORT
	int32_t getHciIndex() const { return mHciIndex; }
#endif

	std::unordered_map<std::string, BluetoothDevice*> getDevices() const { return mDevices; }

	std::vector<BluetoothServiceClassInfo> getSupportedServiceClasses(){ return mSupportedServiceClasses; }

	BluetoothDevice* findDevice(const std::string &address) const;
	BluetoothDevice* findLeDevice(const std::string &address) const;
	BluetoothLinkKey findLinkKey(const std::string &address) const;
	void updateSupportedServiceClasses(const std::vector<std::string> uuids);

	void appendCurrentStatus(pbnjson::JValue &object);
	void appendFilteringDevices(std::string senderName, pbnjson::JValue &object);
	void appendConnectedDevices(pbnjson::JValue &object);
	void appendDevices(pbnjson::JValue &object);
	void appendLeDevices(pbnjson::JValue &object);
	void appendLeRecentDevice(pbnjson::JValue &object, BluetoothDevice *device);
	void appendLeDevicesByScanId(pbnjson::JValue &object, uint32_t scanId);
	void appendSupportedServiceClasses(pbnjson::JValue &object, const std::vector<BluetoothServiceClassInfo> &supportedProfiles);
	void appendConnectedProfiles(pbnjson::JValue &object, const std::string deviceAddress);
	void appendAvailableStatus(pbnjson::JValue &object);
	void appendManufacturerData(pbnjson::JValue &object, const std::vector<uint8_t> manufacturerData);
	void appendScanRecord(pbnjson::JValue &object, const std::vector<uint8_t> scanRecord);
	void appendConnectedRoles(pbnjson::JValue &object, BluetoothDevice *device);

	void notifySubscriberLeDevicesChanged();
	void notifySubscribersConnectedDevicesChanged();
	void notifySubscribersDevicesChanged();
	void notifySubscribersFilteredDevicesChanged();
	void notifySubscriberLeDevicesChangedbyScanId(uint32_t scanId, BluetoothDevice *device = NULL);

	void notifyStartScanListenerDropped(uint32_t scanId);
	bool notifyPairingListenerDropped(bool incoming);

	void startPairing(BluetoothDevice *device);
	void stopPairing();
	void cancelDiscoveryCallback(BluetoothDevice *device, BluetoothError error);
	void beginIncomingPair(const std::string &address);

private:
	bool mPowered;
	bool mDiscoverable;
	bool mDiscovering;
	bool mIsDefault;

	uint32_t mDiscoveryTimeout;
	uint32_t mDiscoverableTimeout;
	uint32_t mClassOfDevice;

#ifdef MULTI_SESSION_SUPPORT
	int32_t mHciIndex;
#endif

	BluetoothAdapter* mAdapter;
	std::string mName;
	std::string mStackName;
	std::string mStackVersion;
	std::string mFirmwareVersion;
	std::string mAddress;

	BluetoothPairState mPairState;
	std::unordered_map<std::string, BluetoothDevice*> mDevices;
	std::unordered_map<std::string, BluetoothDevice*> mLeDevices;
	std::unordered_map<std::string, BluetoothLinkKey> mLinkKeys;
	std::unordered_map<std::string, int32_t> mFilterClassOfDevices;
	std::unordered_map<std::string, std::string> mFilterUuids;
	std::unordered_map<uint32_t, std::unordered_map<std::string, BluetoothDevice*>> mLeDevicesByScanId;

	LSUtils::ClientWatch *mOutgoingPairingWatch;
	LSUtils::ClientWatch *mIncomingPairingWatch;

	std::unordered_map <std::string, LSUtils::ClientWatch*> mGetDevicesWatches;
	std::unordered_map <uint32_t, LSUtils::ClientWatch*> mStartScanWatches;
	LS::SubscriptionPoint mGetDevicesSubscriptions;
	LS::SubscriptionPoint mGetConnectedDevicesSubscriptions;
	std::vector<BluetoothServiceClassInfo> mSupportedServiceClasses;
	std::vector<std::string> mEnabledServiceClasses;
	BluetoothManagerService *mBluetoothManagerService;
};
#endif

