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


#ifndef BLUETOOTHOPPPROFILESERVICE_H
#define BLUETOOTHOPPPROFILESERVICE_H

#include <string>
#include <map>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>

#include "bluetoothprofileservice.h"

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

class BluetoothClientWatch;

class BluetoothOppProfileService : public BluetoothProfileService,
                                   public BluetoothOppStatusObserver
{
public:
	BluetoothOppProfileService(BluetoothManagerService *manager);
	~BluetoothOppProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);
	bool isDevicePaired(const std::string &address);
	bool pushFile(LSMessage &message);
	bool awaitTransferRequest(LSMessage &message);
	bool acceptTransferRequest(LSMessage &message);
	bool rejectTransferRequest(LSMessage &message);
	bool cancelTransfer(LSMessage &message);
	void transferConfirmationRequested(BluetoothOppTransferId transferId, const std::string &adapterAddress, const std::string &address, const std::string &deviceName, const std::string &fileName, uint64_t fileSize);
	void transferStateChanged(BluetoothOppTransferId transferId, uint64_t transferred, bool finished);
	bool monitorTransfer(LSMessage &message);
	bool addClientWatch(LS::Message& request, std::list<BluetoothClientWatch*>* clientWatch,
		std::string adapterAddress, std::string deviceAddress);

private:
	class Transfer
	{
	public:
		Transfer() : watch(0), canceled(false), clientDisappeared(false) { }
		~Transfer();

		std::string deviceAddress;
		std::string adapterAddress;
		LSUtils::ClientWatch *watch;
		bool canceled;
		bool clientDisappeared;
	};

	class PushRequest
	{
	public:
		PushRequest() : fileSize(0), transferred(0) { }
		~PushRequest();

		std::string requestId;
		std::string adapterAddress;
		std::string address;
		std::string name;
		std::string fileName;
		uint64_t fileSize;
		uint64_t transferred;
	};

	std::map<BluetoothOppTransferId, Transfer*> mTransfers;
	std::map<uint64_t, PushRequest*> mPushRequests;
	std::map<uint64_t, BluetoothOppTransferId> mTransferIds;
	std::map<std::string, PushRequest*> mDeletedPushRequested;

	bool prepareFileTransfer(LS::Message &request, pbnjson::JValue &requestObj);
	void handleFileTransferUpdate(LSMessage *message, const std::string &adapterAddress, BluetoothError, uint64_t bytesTransferred, uint64_t totalSize, bool finished);

	void notifyClientTransferStarts(LS::Message &request, const std::string &adapterAddress);
	void notifyClientTransferCanceled(LS::Message &request, const std::string &adapterAddress);
	void notifyTransferStatus(const std::string &adapterAddress);

	void appendTransferStatus(pbnjson::JValue &object);

	Transfer* findTransfer(LSMessage *message);

	void createTransfer(BluetoothOppTransferId id, const std::string &address, const std::string &adapterAddress, LSMessage *message);
	void removeTransfer(BluetoothOppTransferId id);
	void removeTransfer(LSMessage *message);
	void removeTransfer(const std::string &deviceAddress);
	void removeTransfer(std::function<bool(Transfer*)> condition);
	void removeTransfer(std::map<BluetoothOppTransferId,Transfer*>::iterator transferIter);
	void cancelTransfer(BluetoothOppTransferId id, bool clientDisappeared = false);
	std::string buildStorageDirPath(const std::string &path);

	PushRequest* findRequest(const std::string &requestIdStr);
	uint64_t getPushRequestId(const std::string &requestIdStr);
	void deleteTransferId(uint64_t requestId);
	void deleteTransferId(const std::string &requestIdStr);
	BluetoothOppTransferId findTransferId(const std::string &requestIdStr);
	bool prepareConfirmationRequest(LS::Message &request, pbnjson::JValue &requestObj, bool accept);
	void createPushRequest(BluetoothOppTransferId transferId, const std::string &adapterAddress, const std::string &address, const std::string &deviceName, const std::string &fileName, uint64_t fileSize);
	std::string generateRequestId();
	void deletePushRequest(const std::string &requestId);
	void assignPushRequestId(PushRequest *pushRequest);
	void assignPushRequestFromUnused(PushRequest *pushRequest);
	void setTransferRequestsAllowed(const std::string &adapterAddress, bool state);
	void notifyTransferConfirmation(uint64_t requestId);
	void notifyConfirmationRequest(LS::Message &request, const std::string &adapterAddress, bool success);
	void removeClientWatch(std::list<BluetoothClientWatch*> *clientWatch, const std::string& senderName);
	void handleClientDisappeared(std::list<BluetoothClientWatch*>* clientWatch, const std::string senderName);

private:
	uint64_t mRequestIndex;
	uint32_t mNextRequestId;
	std::list<BluetoothClientWatch*> mIncomingTransferWatchesForMultipleAdapters;
	std::map<std::string, bool> mTransferRequestsAllowed;
	std::map<std::string, LS::SubscriptionPoint*> mMonitorTransferSubscriptions;
};

#endif // BLUETOOTHOPPPROFILESERVICE_H
