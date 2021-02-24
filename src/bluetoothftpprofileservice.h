// Copyright (c) 2014-2021 LG Electronics, Inc.
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


#ifndef BLUETOOTHFTPPROFILESERVICE_H
#define BLUETOOTHFTPPROFILESERVICE_H

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

class BluetoothFtpProfileService : public BluetoothProfileService
{
public:
	BluetoothFtpProfileService(BluetoothManagerService *manager);
	~BluetoothFtpProfileService();

	bool listDirectory(LSMessage &message);
	bool pullFile(LSMessage &message);
	bool pushFile(LSMessage &message);

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

	std::map<BluetoothFtpTransferId, Transfer*> mTransfers;

	bool prepareFileTransfer(LS::Message &request, pbnjson::JValue &requestObj);
	void handleFileTransferUpdate(LSMessage *message, const std::string &adapterAddress, BluetoothError, uint64_t bytesTransferred, bool finished);

	void notifyClientTransferStarts(LS::Message &request, const std::string &adapterAddress);
	void notifyClientTransferCanceled(LS::Message &request, const std::string &adapterAddress);

	Transfer* findTransfer(LSMessage *message);

	void createTransfer(BluetoothFtpTransferId id, const std::string &address, const std::string &adapterAddress, LSMessage *message);
	void removeTransfer(BluetoothFtpTransferId id);
	void removeTransfer(LSMessage *message);
	void removeTransfer(const std::string &deviceAddress);
	void removeTransfer(std::function<bool(Transfer*)> condition);
	void removeTransfer(std::map<BluetoothFtpTransferId,Transfer*>::iterator transferIter);
	void cancelTransfer(BluetoothFtpTransferId id, bool clientDisappeared = false);

	std::string buildStorageDirPath(const std::string &path);
};

#endif // BLUETOOTHFTPPROFILESERVICE_H
