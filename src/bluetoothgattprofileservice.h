// Copyright (c) 2015-2020 LG Electronics, Inc.
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


#ifndef BLUETOOTHGATTPROFILESERVICE_H
#define BLUETOOTHGATTPROFILESERVICE_H

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>
#include <unordered_map>

#include "bluetoothprofileservice.h"
class BluetoothGattAncsProfile;

#include "clientwatch.h"

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
	std::string deviceAddress;
	std::string adapterAddress;
	BluetoothUuid serviceUuid;
	uint16_t handle;
	BluetoothUuid characteristicUuid;
	BluetoothUuidList characteristicUuids;
} MonitorCharacteristicSubscriptionInfo;

typedef struct
{
	std::string adapaterAddress;
	std::string deviceAddress;
	bool disconnectByRemote;
	bool used;
	void init()
	{
		adapaterAddress.clear();
		deviceAddress.clear();
		disconnectByRemote = false;
		used = false;
	}
	bool isChanged(const std::string& adapter, const std::string& address, bool remote)
	{
		if ((!used) || (adapaterAddress != adapter) ||
			(deviceAddress != address) ||
			(disconnectByRemote != remote))
		{
			used = true;
			adapaterAddress = adapter;
			deviceAddress = address;
			disconnectByRemote = remote;
			return true;
		}
		return false;
	}
} GattConnSubsInfo;

typedef struct
{
	std::string adapaterAddress;
	std::string deviceAddress;
	bool connecting;
	bool connected;
	bool discoveringServices;
	bool used;
	void init()
	{
		adapaterAddress.clear();
		deviceAddress.clear();
		connecting = false;
		connected = false;
		discoveringServices = false;
		used = false;
	}
	bool isChanged(const std::string& adapter,
		       const std::string& address,
		       bool concting,
		       bool concted,
		       bool discover)
	{
		if ((!used) || (adapaterAddress != adapter) ||
			(deviceAddress != address) ||
			(connecting != concting) ||
			(connected != concted) ||
			(discoveringServices != discover))
		{
			adapaterAddress = adapter;
			deviceAddress = address;
			connecting = concting;
			connected = concted;
			discoveringServices = discover;
			 used = true;
			return true;
		}
		return false;
	}
} GattStatusSubsInfo;


class CharacteristicWatch
{
public:
	CharacteristicWatch() :
	    mRefCount(0),
		mRegistered(false),
		handle(0)
	{
	}

	void ref() { mRefCount++; }
	void unref() { if (mRefCount == 0) return; mRefCount--; }

	bool isUsed() { return mRefCount > 0; }

	void markRegistered() { mRegistered = true; }
	bool isRegistered ()  { return mRegistered; }
	std::string deviceAddress;
	BluetoothUuid serviceId;
	BluetoothUuid characteristicId;
	uint16_t handle;

private:
	unsigned int mRefCount;
	bool mRegistered;
};

class connectedDeviceInfo
{
public:
	connectedDeviceInfo() :
		connectId(0)
	{
	}

	connectedDeviceInfo(std::string address, uint16_t connId) :
		deviceAddress(address),
		connectId(connId)
	{
	}

	std::string getAddress() { return deviceAddress; }
	uint16_t getConnectId() { return connectId; }
	void setAddress(std::string address) { deviceAddress = address; }
	void setConnectId(uint16_t connId) { connectId = connId; }
private:
	std::string deviceAddress;
	uint16_t connectId;
};

class BluetoothGattProfileService : public BluetoothProfileService,
                                    public BluetoothGattProfileStatusObserver
{
	// TODO: Consider separate it from BluetoothGattProfileService
	class LocalService
	{
	public:
		LocalService() :
			id(0),
			started(false),
			itemsLeftToRegister(0)
		{
		}

		bool hasCharacteristic(const BluetoothUuid &characteristic)
		{
			auto availableCharacteristics = desc.getCharacteristics();

			// Make sure we have this characteristic registered. If not refuse read call
			// and fail with an error.
			auto characteristicIter = std::find_if(availableCharacteristics.begin(),
					availableCharacteristics.end(),
					[characteristic](const BluetoothGattCharacteristic &currentCharacteristic) {
				return characteristic == currentCharacteristic.getUuid();
			});

			return characteristicIter != availableCharacteristics.end();
		}

		bool hasCharacteristic(const uint16_t &handle)
		{
			auto availableCharacteristics = desc.getCharacteristics();
			for (auto characteristic : availableCharacteristics)
			{
				if(characteristic.getHandle() == handle)
					return true;
			}

			return false;
		}

		BluetoothGattCharacteristic getParentCharacteristic(const uint16_t &handle)
		{
			auto availableCharacteristics = desc.getCharacteristics();
			for (auto characteristic : availableCharacteristics)
			{
				auto availableDescriptors = characteristic.getDescriptors();
				for (auto descriptor : availableDescriptors)
				{
					if(descriptor.getHandle() == handle)
						return characteristic;
				}
			}

			return {};
		}

		bool hasDescriptor(const uint16_t &handle)
		{
			auto availableCharacteristics = desc.getCharacteristics();
			for (auto characteristic : availableCharacteristics)
			{
				auto availableDescriptors = characteristic.getDescriptors();
				for (auto descriptor : availableDescriptors)
				{
					if(descriptor.getHandle() == handle)
						return true;
				}
			}

			return false;
		}
		// TODO: Check static
		static std::string buildDescriptorKey(const BluetoothUuid &characteristic,
				const BluetoothUuid &descriptor)
		{
			return characteristic.toString() + ":" + descriptor.toString();
		}

		BluetoothGattService desc;
		// BSA server id of the registered service
		uint16_t id;
		bool started;
		// Callback provided from the SIL API user to addService
		BluetoothResultCallback addServiceCallback;

		BluetoothGattCharacteristic lastRegisteredCharacteristic;
		BluetoothGattDescriptor lastRegisteredDescriptor;
		unsigned int itemsLeftToRegister;

		// TODO: Change not to copy
		BluetoothGattCharacteristicList characteristics;
		BluetoothGattDescriptorList descriptors;

		BluetoothGattCharacteristicList::iterator charIt;
		BluetoothGattDescriptorList::iterator descIt;
	};

	class LocalServer
	{
	public:
		LocalServer() :
			id(0)
		{
		}

		void addLocalService(LocalService* newService)
		{
			mLocalServices.insert({newService->desc.getUuid(), newService});
		}

		bool isLocalServiceRegistered(const BluetoothUuid &uuid)
		{
			return mLocalServices.find(uuid) != mLocalServices.end();
		}

		bool removeLocalService(const BluetoothUuid &uuid)
		{
			auto serviceIter = mLocalServices.find(uuid);
			if (serviceIter == mLocalServices.end())
				return false;

			delete serviceIter->second;
			mLocalServices.erase(serviceIter);

			return true;
		}

        void removeAllLocalService()
        {
            for(auto serviceIter : mLocalServices)
			{
                delete serviceIter.second;
                mLocalServices.erase(serviceIter.first);
            }
        }

		LocalService* findLocalService(uint16_t serviceId)
		{
			for (auto serviceIter : mLocalServices)
			{
				if (serviceIter.second->id == serviceId)
					return serviceIter.second;
			}

			return nullptr;
		}

		LocalService* findLocalService(const BluetoothUuid &uuid)
		{
			auto serviceIter = mLocalServices.find(uuid);
			if (serviceIter == mLocalServices.end())
				return nullptr;

			return serviceIter->second;
		}

		bool getLocalCharacteristic(const uint16_t &handle, BluetoothGattCharacteristic &characteristic)
		{
			for (auto service : mLocalServices)
			{
				auto localService = service.second;
				for (auto characteristicElem : localService->desc.getCharacteristics())
				{
					if(characteristicElem.getHandle() == handle)
					{
						characteristic = characteristicElem;
						return true;
					}
				}
			}
			return false;
		}

		bool getLocalDescriptor(const uint16_t &handle, BluetoothGattDescriptor &descriptor)
		{
			for (auto service : mLocalServices)
			{
				auto localService = service.second;
				auto availableCharacteristics = localService->desc.getCharacteristics();
				for (auto characteristicElem : availableCharacteristics)
				{
					auto availableDescriptors = characteristicElem.getDescriptors();
					for (auto descriptorElem : availableDescriptors)
					{
						if(descriptorElem.getHandle() == handle)
						{
							descriptor = descriptorElem;
							return true;
						}
					}
				}
			}
			return false;
		}

		uint16_t id;
		std::unordered_map<BluetoothUuid, LocalService*> mLocalServices;
	};

	// TODO: move to LocalService
	bool addLocalServer(const BluetoothUuid applicationUuid, LocalServer* newServer, const std::string &adapterAddress);
	void addLocalService(const BluetoothUuid applicationUuid, const BluetoothGattService &service, BluetoothResultCallback callback, const std::string &adapterAddress);
	void initCharacteristic(LocalService* newService); // TODO: Change name
	bool hasNext(LocalService* newService);
	void addLocalCharacteristic(LocalServer* server, LocalService* newService, const std::string &adapterAddress);
	void addCharacteristicCallback(LocalServer* server, LocalService* newService, BluetoothError error, uint16_t charId, const std::string &adapterAddress);
	bool removeLocalServer(uint16_t appId, const std::string &adapterAddress);
	bool removeLocalService(uint16_t serverId, const BluetoothUuid &uuid, const std::string &adapterAddress);
	bool removeLocalService(const BluetoothUuid &uuid, const std::string &adapterAddress);

	bool isLocalServiceRegistered(const BluetoothUuid &uuid)
	{
		for (auto server : mLocalServer)
		{
			if(server.second->isLocalServiceRegistered(uuid))
				return true;
		}
		return false;
	}

	LocalServer* findLocalServer(const BluetoothUuid &uuid);
	LocalServer* findLocalServer(uint16_t server_if);
	LocalService* findLocalService(const BluetoothUuid &uuid);
	LocalService* findLocalService(uint16_t server_if);
	LocalService* findLocalService(const BluetoothUuid &uuid, const std::string &adapterAddress);
	LocalService* findLocalService(uint16_t server_if, const std::string &adapterAddress);
	LocalServer* findLocalServerByServiceId(uint16_t serviceId, const std::string &adapterAddress);
	LocalService* findLocalServiceByCharId(uint16_t charId,  const std::string &adapterAddress);
	LocalServer* getLocalServer(const std::string &serverUuid);
	BluetoothGattService getLocalService(const std::string &serviceUuid, const std::string &adapterAddress);
	BluetoothGattServiceList getLocalServices(const std::string &adapterAddress);

	bool getLocalCharacteristic(const uint16_t &handle, BluetoothGattCharacteristic &characteristic, const std::string &adapterAddress);
	bool getLocalDescriptor(const uint16_t &handle, BluetoothGattDescriptor &descriptor, const std::string &adapterAddress);

	void writeLocalCharacteristic(
			const BluetoothGattCharacteristic &characteristic,
			BluetoothResultCallback callback,
			const std::string &adapterAddress);
	void writeLocalDescriptor(
			const BluetoothGattDescriptor &descriptor,
			BluetoothResultCallback callback,
			const std::string &adapterAddress);

	void readLocalCharacteristics(
			const std::string &adapterAddress,
			const BluetoothUuid &service,
			const BluetoothUuidList &characteristics,
			BluetoothGattReadCharacteristicsCallback callback);
	void readLocalDescriptors(const std::string &adapterAddress,
		const BluetoothUuid& service,
		const BluetoothUuid &characteristic,
		const BluetoothUuidList &descriptors,
		BluetoothGattReadDescriptorsCallback callback);

	void writeLocalCharacteristic(const BluetoothUuid &service,
			const BluetoothGattCharacteristic &characteristic,
			BluetoothResultCallback callback,
			const std::string &adapterAddress);
	void writeLocalDescriptor(const BluetoothUuid& service,
			const BluetoothUuid &characteristic,
			const BluetoothGattDescriptor &descriptor,
			BluetoothResultCallback callback,
			const std::string &adapterAddress);

	// observer

	std::unordered_map<BluetoothUuid, LocalServer*> mLocalServer;
	std::unordered_map<uint16_t, connectedDeviceInfo*> mConnectedDevices;
	std::unordered_map<uint16_t, std::string> mServerAdapterMap;
public:
	BluetoothGattProfileService(BluetoothManagerService *manager);
	BluetoothGattProfileService(BluetoothManagerService *manager, const std::string &name, const std::string &uuid);
	~BluetoothGattProfileService();

	static uint16_t nextClientId();
	virtual void initialize();
	virtual void initialize(BluetoothProfile *impl) {};
	void initialize(const std::string &adapterAddress);
	bool connect(LSMessage &message);
	bool disconnect(LSMessage &message);
	bool getStatus(LSMessage &message);
	bool openServer(LSMessage &message);
	bool closeServer(LSMessage &message);
	bool discoverServices(LSMessage &message);
	bool addService(LSMessage &message);
	bool removeService(LSMessage &message);
	bool getServices(LSMessage &message);
	bool writeCharacteristicValue(LSMessage &message);
	bool readCharacteristicValue(LSMessage &message);
	bool readCharacteristicValues(LSMessage &message);
	bool monitorCharacteristic(LSMessage &message);
	bool monitorCharacteristics(LSMessage &message);
	bool readDescriptorValue(LSMessage &message);
	bool readDescriptorValues(LSMessage &message);
	bool writeDescriptorValue(LSMessage &message);

	bool readRemoteCharacteristic(const std::string adapterAddress, const std::string deviceAddress,
			const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid,
			const uint16_t characteristicHandle,
			BluetoothGattReadCharacteristicCallback callback);
	bool readRemoteCharacteristics(const std::string adapterAddress, const std::string deviceAddress,
			const BluetoothUuid &serviceUuid, const BluetoothUuidList &characteristicUuids,
			BluetoothGattReadCharacteristicsCallback callback);
	bool readRemoteDescriptor(const std::string adapterAddress, const std::string deviceAddress, const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid, const BluetoothUuid &descriptorUuid, const uint16_t descriptorHandle,
			BluetoothGattReadDescriptorCallback callback);
	bool readRemoteDescriptors(const std::string adapterAddress, const std::string deviceAddress, const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid, const BluetoothUuidList &descriptorUuids,
				BluetoothGattReadDescriptorsCallback callback);

	virtual bool isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	virtual bool isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);

	void serviceFound(const std::string &address, const BluetoothGattService &service);
	void serviceLost(const std::string &address, const BluetoothGattService &service);
	void characteristicValueChanged(const std::string &address, const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic, const std::string &adapterAddress);
	void characteristicValueChanged(const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic, const std::string &adapterAddress);
	void descriptorValueChanged(const BluetoothUuid &service, const BluetoothUuid &characteristic, BluetoothGattDescriptor &descriptor);
	void incomingLeConnectionRequest(const std::string &address, bool state);

	//Register the Service implementations with GattProfileService
	void registerGattStatusObserver(BluetoothGattProfileService *statusObserver);
protected:
	bool getConnectId(uint16_t appId, uint16_t &connectId, std::string &deviceAddress, const std::string &adapterAddress);
	bool isDevicePaired(const std::string &address);
	bool isCharacteristicValid(const std::string &adapterAddress, const std::string &address,
			const uint16_t &handle, BluetoothGattCharacteristic *characteristic);
	bool isCharacteristicValid(const std::string &adapterAddress, const std::string &address, const std::string &serviceUuid,
			const std::string &characteristicUuid, BluetoothGattCharacteristic *characteristic);

	virtual void notifyStatusSubscribers(const std::string &adapterAddress, const std::string &address, bool connected);
	virtual pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
		                                               std::string adapterAddress, std::string deviceAddress);
	void removeConnectWatchForDevice(const std::string &adapterAddress, const std::string &key,
							bool disconnected, bool remoteDisconnect = true);


	void handleConnectClientDisappeared(const uint16_t &appId, const uint16_t &connectId, const std::string &adapterAddress, const std::string &address);
private:
	void appendServiceResponse(bool localAdapterServices, pbnjson::JValue responseObj, BluetoothGattServiceList serviceList);
	pbnjson::JValue buildDescriptor(const BluetoothGattDescriptor &descriptor, bool localAdapterServices = false);
	pbnjson::JValue buildDescriptors(const BluetoothGattDescriptorList &descriptorsList, bool localAdapterServices = false);
	pbnjson::JValue buildCharacteristic(bool localAdapterServices, const BluetoothGattCharacteristic &characteristic);
	pbnjson::JValue buildCharacteristics(bool localAdapterServices, const BluetoothGattCharacteristicList &characteristicsList);
	void notifyGetServicesSubscribers(bool localAdapterChanged, const std::string &adapterAddress, const std::string &deviceAddress, BluetoothGattServiceList serviceList);
	bool parseValue(pbnjson::JValue valueObj, BluetoothGattValue *value);
	void handleMonitorCharacteristicClientDropped(MonitorCharacteristicSubscriptionInfo &subscriptionInfo, LSUtils::ClientWatch *monitorCharacteristicsWatch);
	void handleMonitorCharacteristicsClientDropped(MonitorCharacteristicSubscriptionInfo &subscriptionInfo, LSUtils::ClientWatch *monitorCharacteristicsWatch);
	bool isDescriptorValid(const std::string &address, const uint16_t &handle, BluetoothGattDescriptor &descriptor, const std::string &adapterAddress);
	bool isDescriptorValid(const std::string &address, const std::string &serviceUuid, const std::string &descriptorUuuid,
	                       const std::string &characteristicUuid, BluetoothGattDescriptor &descriptor, const std::string &adapterAddress);
	void removeSubscriptionPoint(const std::string &adapterAddress, const std::string &address);

	std::unordered_map<std::string, LS::SubscriptionPoint*> mGetServicesSubscriptions;
	std::vector<std::pair<LSUtils::ClientWatch*, MonitorCharacteristicSubscriptionInfo>>  mMonitorCharacteristicSubscriptions;
	std::unordered_map<std::string, bool> mDiscoveringServices;
	std::vector<CharacteristicWatch*> mCharacteristicWatchList;
	std::vector<BluetoothGattProfileService *> mGattObservers;
	std::unordered_map<std::string, std::unordered_map<std::string, uint16_t>> mConnectedDevicesMap;
	std::map<std::string, std::map<std::string, std::pair<LS::SubscriptionPoint*, GattConnSubsInfo*>>> mConnectSubsMap;
	std::map<std::string, std::map<std::string, std::pair<LS::SubscriptionPoint*, GattStatusSubsInfo*>>> mGetStatusSubsMap;
};


#endif // BLUETOOTHGATTPROFILESERVICE_H
