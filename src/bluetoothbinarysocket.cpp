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


#include "bluetoothbinarysocket.h"
#include "logging.h"

BluetoothBinarySocket::BluetoothBinarySocket() :
	mBufferSize(0),
	mRetryData(NULL),
	mRetryDataSize(0),
	mRetryTimeout(0),
	mRetryCount(0),
	mServerSocketFd(-1),
	mClientSocketFd(-1),
	mWriting(false),
	mServerIoChannel(NULL),
	mClientIoChannel(NULL)
{
}

BluetoothBinarySocket::~BluetoothBinarySocket()
{
}

bool BluetoothBinarySocket::createBinarySocket(const std::string &name)
{
	if (name.empty())
		return false;

	if (mkdir(BINARY_SOCKET_DIRECTORY, S_IRUSR | S_IWUSR | S_IXUSR |
										S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) < 0)
	{
		if (errno != EEXIST)
			BT_DEBUG("Failed to create binary socket directory");
	}

	int serverSockFd;
	struct sockaddr_un serverAddr;

	snprintf(mSocketFileName, BINARY_SOCKET_FILE_NAME_SIZE, "%s/%s%s",
			BINARY_SOCKET_DIRECTORY, BINARY_SOCKET_FILE_NAME_PREFIX, name.c_str());

	if (access(mSocketFileName, F_OK) == 0)
		unlink(mSocketFileName);

	if ((serverSockFd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return false;

	bzero(&serverAddr, sizeof(serverAddr));
	serverAddr.sun_family = AF_UNIX;
	strncpy(serverAddr.sun_path, mSocketFileName, sizeof(serverAddr.sun_path)-1);

	if (bind(serverSockFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
	{
		if (!close(serverSockFd))
			BT_DEBUG("Failed to close server socket");
		return false;
	}
	mServerSocketFd = serverSockFd;

	if (chmod(mSocketFileName, ACCESSPERMS) < 0)
	{
		if (errno != EEXIST)
		{
			BT_DEBUG("Failed to chmod binary socket file");
			return false;
		}
	}

	return true;
}

void BluetoothBinarySocket::removeBinarySocket(void)
{
	if (NULL != mServerIoChannel)
	{
		g_io_channel_shutdown(mServerIoChannel, TRUE, NULL);
		g_io_channel_unref(mServerIoChannel);
		mServerIoChannel = NULL;
	}

	if (NULL != mClientIoChannel)
	{
		g_io_channel_shutdown(mClientIoChannel, TRUE, NULL);
		g_io_channel_unref(mClientIoChannel);
		mClientIoChannel = NULL;
	}

	if (mServerSocketFd > 0)
	{
		close(mServerSocketFd);
		mServerSocketFd = -1;
	}

	if (mClientSocketFd > 0)
	{
		close(mClientSocketFd);
		mClientSocketFd = -1;
	}

	if (access(mSocketFileName, F_OK) == 0)
		unlink(mSocketFileName);

	if (access(mSocketFileName, F_OK) == 0)
		unlink(mSocketFileName);

	if (mRetryTimeout)
		g_source_remove(mRetryTimeout);
}

bool BluetoothBinarySocket::registerReceiveDataWatch(BluetoothBinarySocketReceiveCallback callback)
{
	if (listen(mServerSocketFd, DEFAULT_LISTEN_BACKLOG))
		return false;

	mCallback = callback;

	mServerIoChannel = g_io_channel_unix_new(mServerSocketFd);
	g_io_channel_set_flags(mServerIoChannel, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(mServerIoChannel, TRUE);

	g_io_add_watch(mServerIoChannel, (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
					&getAcceptRequest, this);

	return true;
}

bool BluetoothBinarySocket::sendData(const uint8_t *data, const uint32_t size)
{
	if (mClientSocketFd < 0)
	{
		storeSendDataToBuffer(data, size);
		return true;
	}

	if (write(mClientSocketFd, data, size) <= 0)
		retrySendData(data, size);

	return true;
}

void BluetoothBinarySocket::retrySendData(const uint8_t *data, const uint32_t size)
{
	/*
	 * This logic is the migration from bluetooth-manager.
	 * It avoids to drop the data which is sent from the stack to the socket.
	 * MAX_WRITE_RETRY_COUNT and WRITE_RETRY_SLEEP_TIME are configured with the
	 * socket client side. (i.e. watchmanager and mashupmanager)
	 */

	auto retrySendDataCallback = [] (gpointer userData) -> gboolean {
		if (NULL == userData)
			return FALSE;

		BluetoothBinarySocket *binarySocket = static_cast<BluetoothBinarySocket *>(userData);
		if (NULL == binarySocket || binarySocket->mRetryCount > MAX_WRITE_RETRY_COUNT
									|| binarySocket->mClientSocketFd < 0)
		{
			if ( binarySocket == NULL ) // 	Either the condition 'NULL==binarySocket' is redundant or there is possible null pointer dereference: binarySocket.
				BT_INFO("BINSOCKET", 0, "binarySocket is NULL ");
			else
				binarySocket->mRetryTimeout = 0;
			return FALSE;
		}

		if (write(binarySocket->mClientSocketFd, binarySocket->mRetryData, binarySocket->mRetryDataSize) <= 0)
		{
			binarySocket->mRetryCount++;
			return TRUE;
		}

		binarySocket->mRetryTimeout = 0;
		return FALSE;
	};

	mRetryData = data;
	mRetryDataSize = size;
	mRetryCount = 0;

	g_timeout_add(WRITE_RETRY_SLEEP_TIME, retrySendDataCallback, this);
}

void BluetoothBinarySocket::storeSendDataToBuffer(const uint8_t *data, const uint32_t size)
{
	/*
	 * This logic is the migration from bluetooth-manager.
	 * The buffer will be sent first when the socket is connected.
	 */
	if (size < (DATA_BUFFER_SIZE - mBufferSize))
	{
		memcpy((uint8_t *)(mBufferData + mBufferSize), data, size);
		mBufferSize += size;
	}
}

void BluetoothBinarySocket::sendBufferData()
{
	if (mBufferSize > 0)
	{
		if (write(mClientSocketFd, mBufferData, mBufferSize) > 0)
			mBufferSize = 0;
	}
}

gboolean BluetoothBinarySocket::getAcceptRequest(GIOChannel *io, GIOCondition cond, gpointer userData)
{
	if (NULL == userData)
		return FALSE;

	BluetoothBinarySocket *binarySocket = static_cast<BluetoothBinarySocket *>(userData);

	if (cond & (G_IO_NVAL | G_IO_HUP | G_IO_ERR))
		return FALSE;

	if (NULL == binarySocket->mServerIoChannel || binarySocket->mServerSocketFd <= 0)
		return FALSE;

	struct sockaddr_un clientAddr;
	int clientLen = sizeof(clientAddr);

	binarySocket->mClientSocketFd = accept(binarySocket->mServerSocketFd,
										(struct sockaddr *)&(clientAddr),
										(socklen_t *)&clientLen);
	if ((binarySocket->mClientSocketFd) < 0)
		return FALSE;

	binarySocket->sendBufferData();

	binarySocket->mClientIoChannel = g_io_channel_unix_new(binarySocket->mClientSocketFd);
	g_io_channel_set_flags(binarySocket->mClientIoChannel, G_IO_FLAG_NONBLOCK, NULL);
	g_io_channel_set_close_on_unref(binarySocket->mClientIoChannel, TRUE);
	g_io_add_watch(binarySocket->mClientIoChannel, (GIOCondition)(G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL),
					&getReceiveRequest, userData);

	return TRUE;
}

gboolean BluetoothBinarySocket::getReceiveRequest(GIOChannel *io, GIOCondition cond, gpointer userData)
{
	if (NULL == userData)
		return FALSE;

	BluetoothBinarySocket *binarySocket = static_cast<BluetoothBinarySocket *>(userData);

	if (cond & (G_IO_NVAL | G_IO_ERR))
		return FALSE;

	if (NULL == binarySocket->mClientIoChannel || binarySocket->mClientSocketFd <= 0)
		return FALSE;

	if (binarySocket->isWriting())
		return TRUE;

	guchar buf[READ_BUFFER_SIZE];
	ssize_t readBytes = read(binarySocket->mClientSocketFd, buf, sizeof(buf));

	if (readBytes > 0) {
		if (NULL != binarySocket->mCallback)
			binarySocket->mCallback(buf, readBytes);
	} else if (cond & G_IO_HUP)
		return FALSE;

	return TRUE;
}
