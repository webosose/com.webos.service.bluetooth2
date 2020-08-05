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


#include "clientwatch.h"
#include "logging.h"

namespace LSUtils
{

ClientWatch::ClientWatch(LSHandle *handle, LSMessage *message, ClientWatchStatusCallback callback) :
    mHandle(handle),
    mMessage(message),
    mCookie(0),
    mCallback(callback),
    mNotificationTimeout(0)
{
	if (!mMessage)
		return;

	LSMessageRef(mMessage);
	startWatching();
}

ClientWatch::~ClientWatch()
{
	// Don't send any pending notifications as that will fail
	if (mNotificationTimeout)
		g_source_remove(mNotificationTimeout);

	if (mCookie)
	{
		LS::Error error;

		if (!LSCancelServerStatus(mHandle, mCookie, error.get()))
			error.log(PmLogGetLibContext(), "LS_FAILED_TO_UNREG_SRV_STAT");
	}

	if (mMessage)
		LSMessageUnref(mMessage);

	LSCallCancelNotificationRemove(mHandle, &ClientWatch::clientCanceledCallback, this, NULL);
}

bool ClientWatch::serverStatusCallback(LSHandle *, const char *, bool connected, void *context)
{
	ClientWatch *watch = static_cast<ClientWatch*>(context);
	if (nullptr == watch)
		return false;

	if (connected)
		return true;

	watch->notifyClientDisconnected();

	return true;
}

bool ClientWatch::clientCanceledCallback(LSHandle *, const char *uniqueToken, void *context)
{
	ClientWatch *watch = static_cast<ClientWatch*>(context);
	if (nullptr == watch)
		return false;

	watch->notifyClientCanceled(uniqueToken);

	return true;
}

void ClientWatch::startWatching()
{
	if (!mMessage)
		return;

	const char *serviceName = LSMessageGetSender(mMessage);

	LS::Error error;
#ifdef MULTI_SESSION_SUPPORT
	std::string session = LSMessageGetSessionId(mMessage);
	BT_DEBUG("startWatching serviceName %s session id %s", serviceName, session.c_str());
	const char *sessionId = (session == "host") ? NULL : session.c_str();

	if (!LSRegisterSessionServerStatusEx(mHandle, serviceName, sessionId, &ClientWatch::serverStatusCallback,
	                                     this, &mCookie, error.get()))
#else
	if (!LSRegisterServerStatusEx(mHandle, serviceName, &ClientWatch::serverStatusCallback,
	                              this, &mCookie, error.get()))
#endif
		throw error;

	if (!LSCallCancelNotificationAdd(mHandle, &ClientWatch::clientCanceledCallback, this, error.get()))
		throw error;
}

gboolean ClientWatch::sendClientDroppedNotification(gpointer user_data)
{
	ClientWatch *watch = static_cast<ClientWatch*>(user_data);
	if (nullptr == watch)
		return FALSE;

	watch->mNotificationTimeout = 0;

	if (watch->mCallback)
		watch->mCallback();

	return FALSE;
}

void ClientWatch::triggerClientDroppedNotification()
{
	if (mNotificationTimeout)
		return;

	// We have to offload the actual callback here as otherwise we risk
	// a deadlock when someone tries to destroy us while stilling being
	// in the callback from ls2
	mNotificationTimeout = g_timeout_add(0, &ClientWatch::sendClientDroppedNotification, this);
}

void ClientWatch::notifyClientDisconnected()
{
	triggerClientDroppedNotification();
}

void ClientWatch::notifyClientCanceled(const char *clientToken)
{
	if (!mMessage)
		return;

	const char *messageToken = LSMessageGetUniqueToken(mMessage);

	if(!g_strcmp0(messageToken, clientToken))
		triggerClientDroppedNotification();
}

} // namespace LS

