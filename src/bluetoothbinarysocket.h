// Copyright (c) 2015-2018 LG Electronics, Inc.
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


#ifndef BLUETOOTHBINARYSOCKET_H
#define BLUETOOTHBINARYSOCKET_H

#include <cstdint>
#include <string>
#include <functional>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <gio/gio.h>
#include <glib-object.h>

#define BINARY_SOCKET_DIRECTORY         "/dev/bluetooth"
#define BINARY_SOCKET_FILE_NAME_PREFIX  "binarySocketPath"
#define BINARY_SOCKET_FILE_NAME_SIZE    64
#define DEFAULT_LISTEN_BACKLOG          5
#define READ_BUFFER_SIZE                1024
#define DATA_BUFFER_SIZE                1024*5
#define MAX_WRITE_RETRY_COUNT           1000
#define WRITE_RETRY_SLEEP_TIME          10000

typedef std::function<void(guchar *readBuf, gsize readLen)> BluetoothBinarySocketReceiveCallback;

class BluetoothBinarySocket
{
public:
	BluetoothBinarySocket();
	~BluetoothBinarySocket();

	bool isWriting() const { return mWriting; }
	void setWriting(bool writing) { mWriting = writing; }
	bool createBinarySocket(const std::string &name);
	void removeBinarySocket(void);
	bool registerReceiveDataWatch(BluetoothBinarySocketReceiveCallback callback);
	bool sendData(const uint8_t *data, const uint32_t size);

private:
	char mSocketFileName[BINARY_SOCKET_FILE_NAME_SIZE];
	char mBufferData[DATA_BUFFER_SIZE];
	uint32_t mBufferSize;
	const uint8_t *mRetryData;
	uint32_t mRetryDataSize;
	uint32_t mRetryTimeout;
	uint32_t mRetryCount;
	int mServerSocketFd;
	int mClientSocketFd;
	bool mWriting;
	GIOChannel *mServerIoChannel;
	GIOChannel *mClientIoChannel;
	BluetoothBinarySocketReceiveCallback mCallback;

private:
	void retrySendData(const uint8_t *data, const uint32_t size);
	void storeSendDataToBuffer(const uint8_t *data, const uint32_t size);
	void sendBufferData();

private:
	static gboolean getAcceptRequest(GIOChannel *io, GIOCondition cond, gpointer userData);
	static gboolean getReceiveRequest(GIOChannel *io, GIOCondition cond, gpointer userData);
};

#endif // BLUETOOTHBINARYSOCKET_H
