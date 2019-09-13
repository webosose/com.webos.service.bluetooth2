// Copyright (c) 2014-2019 LG Electronics, Inc.
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


#ifndef BLUETOOTH_MANAGER_H_
#define BLUETOOTH_MANAGER_H_

#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <glib.h>
#include <luna-service2/lunaservice.hpp>
#include <bluetooth-sil-api.h>
#include "bluetoothpairstate.h"

class BluetoothProfileService;
class BluetoothDevice;
class BluetoothServiceClassInfo;
class BluetoothGattAncsProfile;
class BluetoothManagerAdapter;

namespace pbnjson
{
	class JValue;
}

namespace LS
{
	class Message;
	class ServerStatus;
}

namespace LSUtils
{
	class ClientWatch;
}

typedef struct
{
	AdvertiseData advertiseData;
	AdvertiseData scanResponse;
	AdvertiseSettings settings;
} AdvertiserInfo;

class AdapterInfo
{
public:
	BluetoothAdapter* adapter;
	std::string name;
	std::string stackName;
	std::string stackVersion;
	std::string firmwareVersion;
	std::string address;
	uint32_t discoveryTimeout;
	bool powered;
	bool discoverable;
	bool discovering;
	uint32_t discoverableTimeout;
	uint32_t classOfDevice;
	BluetoothPairState pairState;
};

class BluetoothManagerService :
		public LS::Handle,
		public BluetoothSILStatusObserver
{
public:
	BluetoothManagerService();
	~BluetoothManagerService();

	//Observer callbacks
	void adaptersChanged();
	void adapterKeepAliveStateChanged(bool enabled);

	void leDeviceRemovedByScanId(uint32_t scanId, const std::string &address);
	void leDevicePropertiesChangedByScanId(uint32_t scanId, const std::string &address, BluetoothPropertiesList properties);

	void leConnectionRequest(const std::string &address, bool state);
	void requestReset();

	bool isDefaultAdapterAvailable() const;
	BluetoothAdapter* getDefaultAdapter() const;
	bool isDeviceAvailable(const std::string &address) const;
	std::string getAddress() const;

	void initializeProfiles();
	void resetProfiles();

	BluetoothManagerAdapter* findAdapterInfo(const std::string &address) const;
	BluetoothDevice* findDevice(const std::string &address) const;
	bool getPowered(const std::string &address);
	bool isAdapterAvailable(const std::string &address);
	bool isRequestedAdapterAvailable(LS::Message &request, const pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool getAdvertisingState();
	void setAdvertisingState(bool advertising);
	bool isRoleEnable(const std::string &address, const std::string &role);
	std::string getMessageOwner(LSMessage *message);
	int getAdvSize(AdvertiseData advData, bool flagRequired);


private:
	bool setState(LSMessage &message);
	bool getStatus(LSMessage &message);
	bool queryAvailable(LSMessage &message);
	bool startFilteringDiscovery(LSMessage &message);
	bool getFilteringDeviceStatus(LSMessage &message);
	bool startDiscovery(LSMessage &message);
	bool cancelDiscovery(LSMessage &message);
	bool getDeviceStatus(LSMessage &message);
	bool setDeviceState(LSMessage &msg);
	bool pair(LSMessage &message);
	bool unpair(LSMessage &message);
	bool supplyPasskey(LSMessage &message);
	bool supplyPinCode(LSMessage &message);
	bool supplyPasskeyConfirmation(LSMessage &message);
	bool cancelPairing(LSMessage &message);
	bool awaitPairingRequests(LSMessage &message);
	bool setWoBle(LSMessage &message);
	bool setWoBleTriggerDevices(LSMessage &message);
	bool getWoBleStatus(LSMessage &message);
	bool sendHciCommand(LSMessage &message);
	bool setAdvertiseData(LSMessage &message, pbnjson::JValue &value, AdvertiseData &data, bool isScanRsp);
	void updateAdvertiserData(LSMessage *requestMessage, uint8_t advertiserId, AdvertiserInfo advInfo,
			bool isSettingsChanged, bool isAdvDataChanged, bool isScanRspChanged);
	bool setTrace(LSMessage &message);
	bool getTraceStatus(LSMessage &message);
	bool getLinkKey(LSMessage &message);
	bool setKeepAlive(LSMessage &message);
	bool getKeepAliveStatus(LSMessage &message);
	bool startSniff(LSMessage &message);
	bool stopSniff(LSMessage &message);


#ifdef WBS_UPDATE_FIRMWARE
	bool updateFirmware(LSMessage &message);
#endif

	void appendCurrentStatus(pbnjson::JValue &object);
	void appendCurrentStatusForMultipleAdapters(pbnjson::JValue &object);
	void appendAvailableStatus(pbnjson::JValue &object);

	void notifySubscriberLeDevicesChanged();
	void notifySubscriberLeDevicesChangedbyScanId(uint32_t scanId);
	void notifySubscribersAboutStateChange();
	void notifySubscribersAdvertisingChanged(std::string adapterAddress);
	void notifySubscribersAdaptersChanged();

	void handleDeviceStatePropertiesSet(BluetoothPropertiesList properties, BluetoothDevice *device, LS::Message &request, const std::string &adapterAddress, BluetoothError error);

	void updateFromAdapterAddressForQueryAvailable(BluetoothAdapter *adapter, const BluetoothProperty &property);
	void assignDefaultAdapter();

	BluetoothAdapter* getAdapter(const std::string &address);

	bool isServiceClassEnabled(const std::string& serviceClass);

	void createProfiles();


	bool notifyAdvertisingDropped(uint8_t advertiserId);
	bool notifyAdvertisingDisabled(uint8_t advertiserId);

	void postToClient(LSMessage *message, pbnjson::JValue &object);

	bool setPairableState(const std::string &adapterAddress, bool value);
	void cancelIncomingPairingSubscription(const std::string &adapterAddress);

	bool pairCallback (BluetoothError error);
	void cancelDiscoveryCallback(const std::string &adapterAddress, BluetoothDevice *device, BluetoothError error);

	//BLE
	bool configureAdvertisement(LSMessage &message);
	bool startAdvertising(LSMessage &message);
	bool updateAdvertising(LSMessage &message);
    bool disableAdvertising(LSMessage &message);
	bool stopAdvertising(LSMessage &message);
	bool getAdvStatus(LSMessage &message);
	bool startScan(LSMessage &message);
	std::vector<BluetoothProfileService*>& getProfiles() { return mProfiles; }
	BluetoothPairingIOCapability getIOPairingCapability() { return mPairingIOCapability; }

private:
	std::vector<BluetoothProfileService*> mProfiles;
	std::string mAddress;
	bool mAdvertising;
	bool mWoBleEnabled;
	bool mKeepAliveEnabled;
	uint32_t mKeepAliveInterval;
	BluetoothSIL *mSil;
	BluetoothAdapter *mDefaultAdapter;
	std::vector<BluetoothAdapter*> mAdapters;
	std::unordered_map<std::string, BluetoothManagerAdapter*> mAdaptersInfo;
	std::vector<std::string> mEnabledServiceClasses;
	BluetoothWoBleTriggerDeviceList mWoBleTriggerDevices;
	BluetoothPairingIOCapability mPairingIOCapability;

	LSUtils::ClientWatch *mAdvertisingWatch;

	std::unordered_map<uint8_t, AdvertiserInfo*> mAdvertisers;

	LS::SubscriptionPoint mGetStatusSubscriptions;
	LS::SubscriptionPoint mGetAdvStatusSubscriptions;
	LS::SubscriptionPoint mQueryAvailableSubscriptions;
	LS::SubscriptionPoint mGetKeepAliveStatusSubscriptions;

	BluetoothGattAncsProfile *mGattAnsc;
	friend class BluetoothManagerAdapter;
};

#endif

// vim: noai:ts=4:sw=4:ss=4:expandtab
