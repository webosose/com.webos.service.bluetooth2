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

#ifndef SERVICEWATCH_H
#define SERVICEWATCH_H

#include <string>
#include <luna-service2/lunaservice.hpp>

namespace LSUtils
{

typedef std::function<void(bool)> ServiceWatchStatusCallback;

class ServiceWatch
{
public:
	ServiceWatch(LSHandle* handle, std::string serviceName, ServiceWatchStatusCallback callback);
	ServiceWatch(const ServiceWatch &other) = delete;
	~ServiceWatch();

	void setCallback(ServiceWatchStatusCallback callback) { mCallback = callback; }

private:
	LSHandle *mHandle;
	std::string mServiceName;
	void *mCookie;
	ServiceWatchStatusCallback mCallback;

	void startWatching();
	static bool serverStatusCallback(LSHandle *, const char *, bool connected, void *context);
};

} // namespace LS

#endif // CLIENTWATCH_H
