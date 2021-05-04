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
	bool unprovisionedScanCancel(LSMessage &message);
	bool createNetwork(LSMessage &message);
	bool provision(LSMessage &message);
	bool supplyProvisioningOob(LSMessage &message);
	bool supplyProvisioningNumeric(LSMessage &message);
	bool createAppKey(LSMessage &message);
	bool get(LSMessage &message);
	bool set(LSMessage &message);
	bool setOnOff(LSMessage &message);
	bool send(LSMessage &message);
	bool receive(LSMessage &message);
	bool getCompositionData(LSMessage &message);
	bool getMeshInfo(LSMessage &message);

	/* Mesh Observer APIs */
	void scanResult(const std::string &adapterAddress, const int16_t rssi, const std::string &uuid, const std::string &name = "");
	void modelConfigResult(const std::string &adapterAddress, BleMeshConfiguration &configuration, BluetoothError error);
	void modelSetOnOffResult(const std::string &adapterAddress, bool onOffState, BluetoothError error);
	void updateNetworkId(const std::string &adapterAddress, const uint64_t networkId);
	void provisionResult(BluetoothError error, const std::string &adapterAddress,
								 const std::string &request = "",
								 const std::string &stringToDisplay = "",
								 const uint32_t numberToDisplay = 0,
								 const std::string &numberDisplayType ="",
								 const std::string &promptType = "",
								 uint16_t unicastAddress = 0,
								 uint8_t count = 0,
								 const std::string &uuid = "");
	void modelDataReceived(const std::string &adapterAddress,
									   uint16_t srcAddress, uint16_t destAddress,
									   uint16_t appKey, uint8_t data[], uint32_t datalen);

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
	pbnjson::JValue appendRelayStatus(BleMeshRelayStatus relayStatus);
	pbnjson::JValue appendCompositionData(BleMeshCompositionData compositionData);
	bool isScanDevicePresent(const std::string &adapterAddress, const std::string &uuid);
	bool isNetworkCreated() const { return mNetworkCreated; }
	bool removeFromDeviceList(const std::string &adapterAddress, const std::string &uuid);
	/* Returns true if app key already active */
	bool isAppKeyExist(uint16_t appKeyIndex);
	/* Returns true if application is authorized to use the particular app key index */
	bool isValidApplication(uint16_t appKeyIndex, LS::Message &request);
	void setModelConfigResult(const std::string &adapterAddress, BleMeshConfiguration &configuration, BluetoothError error);
	pbnjson::JValue appendAppKeyIndexes(std::vector<uint16_t> appKeyList);
	pbnjson::JValue appendMeshInfo();
	pbnjson::JValue appendNetKeys();
	pbnjson::JValue appendAppKeys();
	pbnjson::JValue appendProvisioners();

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
	std::list<BluetoothClientWatch *> mModelOnOffResultWatch;
	std::list<BluetoothClientWatch *> mNetworkIdWatch;
	std::list<BluetoothClientWatch *> mProvResultWatch;
	std::list<BluetoothClientWatch *> mGetModelConfigResultWatch;
	std::list<BluetoothClientWatch *> mSetModelConfigResultWatch;
	std::list<BluetoothClientWatch *> mCompositionDataWatch;
	//std::list<BluetoothClientWatch *> mReceiveWatch;
	/* map<adapterAddress, map<uuid, UnprovisionedDeviceInfo>> */
	std::unordered_map<std::string, std::map<std::string, UnprovisionedDeviceInfo>> mUnprovisionedDevices;
	std::map<uint16_t, LS::SubscriptionPoint*> recvSubscriptions;

	bool mNetworkCreated;
	/* App Key Index created so far */
	uint16_t mAppKeyIndex;

	std::unordered_map<uint16_t, std::string> mAppKeys;
};

#endif //BLUETOOTHMESHPROFILESERVICE_H
