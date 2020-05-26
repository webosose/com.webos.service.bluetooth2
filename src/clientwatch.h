// Copyright (c) 2014-2020 LG Electronics, Inc.
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


#ifndef CLIENTWATCH_H
#define CLIENTWATCH_H

#include <string>
#include <luna-service2/lunaservice.hpp>

namespace LSUtils
{

typedef std::function<void(void)> ClientWatchStatusCallback;

class ClientWatch
{
public:
	ClientWatch(LSHandle *handle, LSMessage *message, ClientWatchStatusCallback callback);
	ClientWatch(LSHandle* handle, LSMessage* message, ClientWatchStatusCallback callback,
		std::string adapterAddress, std::string deviceAddress);
	ClientWatch(const ClientWatch &other) = delete;
	~ClientWatch();

	LSMessage *getMessage() const { return mMessage; }
	void setCallback(ClientWatchStatusCallback callback) { mCallback = callback; }
	std::string getAdapterAddress() { return mAdapterAddress; }
	std::string getDeviceAddress() { return mDeviceAddress;  }

private:
	LSHandle *mHandle;
	LSMessage *mMessage;
	void *mCookie;
	ClientWatchStatusCallback mCallback;
	guint mNotificationTimeout;
	std::string mAdapterAddress;
	std::string mDeviceAddress;

	void startWatching();
	void cleanup();
	void triggerClientDroppedNotification();

	void notifyClientDisconnected();
	void notifyClientCanceled(const char *clientToken);

	static gboolean sendClientDroppedNotification(gpointer user_data);
	static bool serverStatusCallback(LSHandle *, const char *, bool connected, void *context);
	static bool clientCanceledCallback(LSHandle *, const char *uniqueToken, void *context);
};

} // namespace LS

#endif // CLIENTWATCH_H
