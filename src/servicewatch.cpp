// Copyright (c) 2020 LG Electronics, Inc.
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


#include "servicewatch.h"
#include <string>
#include <PmLogLib.h>

namespace LSUtils
{

ServiceWatch::ServiceWatch(LSHandle *handle, std::string serviceName, ServiceWatchStatusCallback callback):
    mHandle(handle),
    mServiceName(serviceName),
    mCookie(0),
    mCallback(callback)
{
	startWatching();
}

ServiceWatch::~ServiceWatch()
{
	if (mCookie)
	{
		LS::Error error;

		if (!LSCancelServerStatus(mHandle, mCookie, error.get()))
			error.log(PmLogGetLibContext(), "LS_FAILED_TO_UNREG_SRV_STAT");
	}
}

bool ServiceWatch::serverStatusCallback(LSHandle *, const char *, bool connected, void *context)
{
	ServiceWatch *watch = static_cast<ServiceWatch*>(context);
	if (nullptr == watch)
		return false;

	watch->mCallback(connected);
	return true;
}

void ServiceWatch::startWatching()
{
	LS::Error error;
	if (!LSRegisterServerStatusEx(mHandle, mServiceName.c_str(), &ServiceWatch::serverStatusCallback,
	                              this, &mCookie, error.get()))
		throw error;
}

} // namespace LS
