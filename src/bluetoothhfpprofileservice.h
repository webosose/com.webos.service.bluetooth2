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


#ifndef BLUETOOTHHFPPROFILESERVICE_H
#define BLUETOOTHHFPPROFILESERVICE_H

#include <unordered_map>
#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>
#include <glib.h>

#include "bluetoothprofileservice.h"
#include "clientwatch.h"

namespace pbnjson
{
	class JValue;
}

namespace LS
{
	class SubscriptionPoint;
}

namespace LSUtils
{
	class ClientWatch;
}
typedef std::unordered_map<std::string, LS::SubscriptionPoint*> HfpServiceSubscriptions;

class BluetoothHfpProfileService : public BluetoothProfileService,
                                   public BluetoothHfpStatusObserver
{
public:
	BluetoothHfpProfileService(BluetoothManagerService *manager);
	~BluetoothHfpProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);

	bool openSCO(LSMessage &message);
	bool closeSCO(LSMessage &message);
	bool receiveAT(LSMessage &message);
	bool sendResult(LSMessage &message);
	bool indicateCall(LSMessage &message);
	bool sendAT(LSMessage &message);
	bool receiveResult(LSMessage &message);

	void scoStateChanged(const std::string &address, bool state);
	void atCommandReceived(const std::string &address, const BluetoothHfpAtCommand &atCommand);
	void resultCodeReceived(const std::string &address, const std::string &resultCode);

protected:
	pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
                                       std::string adapterAddress, std::string deviceAddress);

private:
	void handleOpenScoClientDisappeared(const std::string &adapterAddress, const std::string &address);
	void removeOpenScoWatchForDevice(const std::string &address, bool disconnected, bool remoteDisconnect = true);
	std::string typeToString(BluetoothHfpAtCommand::Type type) const;
	BluetoothHfpAtCommand::Type stringToType(const std::string &type) const;
	void startRinging(const std::string &address, const std::string &phoneNumber);
	void stopRinging(const std::string &address);
	void sendRingResultCode(const std::string &address);
	void sendCLIPResultCode(const std::string &address);
	void notifyReceiveATSubscribers(const std::string &key, const std::string &address, const BluetoothHfpAtCommand &atCommand);
	void notifyToSubscribers(const std::string &address, HfpServiceSubscriptions &subscriptions, pbnjson::JValue &responseObj);
	void addSubscription(const std::string &deviceAddress, LS::Message &request, HfpServiceSubscriptions &subscriptions);

private:
	typedef struct {
		BluetoothHfpProfileService *service;
		std::string address;
		std::string phoneNumber;
	} RingCallbackInfo;

	std::unordered_map<std::string, RingCallbackInfo *> mIndicateCallUserData;
	std::unordered_map<std::string, std::pair<std::string, std::pair<guint, LSUtils::ClientWatch*>>> mIndicateCallWatches;
	std::unordered_map<std::string, LSUtils::ClientWatch*> mOpenScoWatches;
	HfpServiceSubscriptions mReceiveResultSubscriptions;
	HfpServiceSubscriptions mReceiveAtSubscriptions;
	std::vector<std::string> mOpenedScoDevices;
};

#endif // BLUETOOTHHFPPROFILESERVICE_H
