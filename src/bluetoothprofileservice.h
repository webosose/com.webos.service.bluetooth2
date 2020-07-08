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


#ifndef BLUETOOTH_PROFILE_SERVICE_H_
#define BLUETOOTH_PROFILE_SERVICE_H_

#include <string>
#include <map>
#include <vector>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.h>
#include <pbnjson.hpp>

class BluetoothManagerService;
class BluetoothProfile;
class BluetoothDevice;
class BluetoothManagerAdapter;

namespace LS
{
	class Message;
	class SubscriptionPoint;
}

namespace LSUtils
{
	class ClientWatch;
}

class BluetoothProfileService : public BluetoothProfileStatusObserver
{
public:
	BluetoothProfileService(BluetoothManagerService *manager, const std::string &name,
	                        const std::string &uuid);
	BluetoothProfileService(BluetoothManagerService *manager, const std::string &name,
	                        const std::string &uuid1, const std::string &uuid2);
	virtual ~BluetoothProfileService();

	virtual void initialize();
	virtual void initialize(const std::string  &adapterAddress);
	virtual void reset();
	virtual void reset(const std::string &adapterAddress);

	std::string getName() const;
	std::vector<std::string> getUuids() const;

	void propertiesChanged(const std::string &address, BluetoothPropertiesList properties);
	void propertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPropertiesList properties);
	bool isDeviceConnected(const std::string &address);
	bool isDeviceConnected(const std::string &adapterAddress, const std::string &address);
	bool isDeviceConnecting(const std::string &address);
	bool isDeviceConnecting(const std::string &adapterAddress, const std::string &address);

public:
	virtual bool connect(LSMessage &message);
	virtual bool disconnect(LSMessage &message);
	virtual bool enable(LSMessage &message);
	virtual bool disable(LSMessage &message);
	virtual bool getStatus(LSMessage &message);

protected:
	BluetoothManagerService* getManager() const;

	template<typename T>
	inline T* getImpl() { return dynamic_cast<T*>(mImpl); }

	template<typename T>
	inline T* getImpl(const std::string &adapterAddress) { return dynamic_cast<T*>(findImpl(adapterAddress)); }

	BluetoothProfile *mImpl;
	std::map<std::string, LSUtils::ClientWatch*> mConnectWatches;
	std::map<std::string, LS::SubscriptionPoint*> mGetStatusSubscriptions;
	std::map<std::string, std::map<std::string, LSUtils::ClientWatch*>> mConnectWatchesForMultipleAdapters;
	std::map<std::string, std::map<std::string, LS::SubscriptionPoint*>> mGetStatusSubscriptionsForMultipleAdapters;

	std::map<std::string, BluetoothProfile*> mImpls;

	virtual bool isDevicePaired(const std::string &address);
	virtual bool isDevicePaired(const std::string &adapterAddress, const std::string &address);
	virtual void notifyStatusSubscribers(const std::string &adapterAddress, const std::string &address, bool connected);
	virtual bool isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	virtual bool isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	virtual bool isGetStatusSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
	                                               std::string adapterAddress, std::string deviceAddress);

	void appendCommonProfileStatus(pbnjson::JValue responseObj, bool connected, bool connecting, bool subscribed,
	                                   bool returnValue, std::string adapterAddress, std::string deviceAddress);
	void markDeviceAsConnected(const std::string &address);
	void markDeviceAsConnected(const std::string &adapterAddress, const std::string &address);
	void markDeviceAsNotConnected(const std::string &address);
	void markDeviceAsNotConnected(const std::string &adapterAddress, const std::string &address);
	void removeConnectWatchForDevice(const std::string &key, bool disconnected, bool remoteDisconnect = true);
	void removeConnectWatchForDevice(const std::string &adapterAddress, const std::string &key, bool disconnected, bool remoteDisconnect = true);
	void markDeviceAsConnecting(const std::string &address);
	void markDeviceAsConnecting(const std::string &adapterAddress, const std::string &address);
	void markDeviceAsNotConnecting(const std::string &address);
	void markDeviceAsNotConnecting(const std::string &adapterAddress, const std::string &address);
	void handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &address);

	BluetoothProfile* findImpl (const std::string &adapterAddress);

private:
	std::vector<std::string> strToProfileRole(const std::string & input);

	BluetoothManagerService *mManager;
	std::string mName;
	std::vector<std::string> mUuids;
	std::vector<std::string> mConnectingDevices;
	std::vector<std::string> mConnectedDevices;
	std::vector<std::string> mEnabledRoles;
	BluetoothResultCallback mCallback;

	std::map<std::string, std::vector<std::string>> mConnectedDevicesForMultipleAdapters;
	std::map<std::string, std::vector<std::string>> mConnectingDevicesForMultipleAdapters;
};

#endif
