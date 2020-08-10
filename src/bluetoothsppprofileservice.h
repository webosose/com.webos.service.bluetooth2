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


#ifndef BLUETOOTHSPPPROFILESERVICE_H
#define BLUETOOTHSPPPROFILESERVICE_H

#include <string>
#include <unordered_map>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>

#include "bluetoothprofileservice.h"
#include "bluetoothbinarysocket.h"
#include "channelmanager.h"

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

class BluetoothSppProfileService : public BluetoothProfileService, BluetoothSppStatusObserver
{
public:
	BluetoothSppProfileService(BluetoothManagerService *manager);
	~BluetoothSppProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);
	void reset(const std::string &adapterAddress);
	bool createChannel(LSMessage &message);
	bool writeData(LSMessage &message);
	bool readData(LSMessage &message);

	virtual void notifyStatusSubscribers(const std::string &adapterAddress, const std::string &address, const std::string &uuid,
	        bool connected);
	virtual bool isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	virtual bool isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	virtual void disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	virtual pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
	        std::string adapterAddress, std::string deviceAddress);

	void channelStateChanged(const std::string &adapterAddress, const std::string &address, const std::string &uuid, BluetoothSppChannelId channelId, bool state);
	void dataReceived(const BluetoothSppChannelId channelId, const std::string &adapterAddress, const uint8_t *data, const uint32_t size);

protected:
	std::map<std::string, ChannelManager*> mChannelImpls;

	ChannelManager* findChannelImpl (const std::string &adapterAddress);
	void createChannelManager(const std::string &adapterAddress);

private:
	std::unordered_map<std::string, BluetoothBinarySocket*> mBinarySockets;

private:
	void handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &address,
	        const BluetoothSppChannelId channelId);
	void notifyCreateChannelSubscribers(const std::string &adapterAddress, const std::string &address, const std::string &uuid,
	        const std::string &channelId, const bool connected);
	void addReadDataSubscription(LS::Message &request, ChannelManager *channelManager, const std::string channelId, const int timeout);
	void removeChannel(const std::string &uuid, const std::string &adapterAddress);
	BluetoothBinarySocket* findBinarySocket(const std::string &channelId) const;
	void enableBinarySocket(const std::string &adapterAddress, const std::string &channelId);
	void disableBinarySocket(const std::string &channelId);
	bool isCallerUsingBinarySocket(ChannelManager *channelManager, const std::string &channelId);
	void handleBinarySocketRecieveRequest(const std::string &channelId, const std::string &adapterAddress, guchar *readBuf, gsize readLen);
	void sendDataToStack(const std::string &channelId, const std::string &adapterAddress, guchar *data, gsize outLen);
};

#endif // BLUETOOTHSPPPROFILESERVICE_H
