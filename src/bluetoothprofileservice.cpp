// Copyright (c) 2014-2019 LG Electronics, Inc.
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


#include <bluetooth-sil-api.h>

#include "bluetoothprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothdevice.h"
#include "bluetoothserviceclasses.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"

BluetoothProfileService::BluetoothProfileService(BluetoothManagerService *manager, const std::string &name,
                                                 const std::string &uuid) :
	mImpl(0),
	mManager(manager),
	mName(name)
{
	mUuids.push_back(uuid);
}

BluetoothProfileService::BluetoothProfileService(BluetoothManagerService *manager, const std::string &name,
                                                 const std::string &uuid1, const std::string &uuid2) :
	mImpl(0),
	mManager(manager),
	mName(name)
{
	mUuids.push_back(uuid1);
	mUuids.push_back(uuid2);
}

BluetoothProfileService::~BluetoothProfileService()
{
}

void BluetoothProfileService::initialize()
{
	auto defaultAdapter = mManager->getDefaultAdapter();
	if (!defaultAdapter)
		return;

	mImpl = defaultAdapter->getProfile(mName);

	if (mImpl)
		mImpl->registerObserver(this);
}

void BluetoothProfileService::reset()
{
	// Our backend is gone so reset everything
	mImpl = 0;
}

BluetoothManagerService* BluetoothProfileService::getManager() const
{
	return mManager;
}

std::string BluetoothProfileService::getName() const
{
	return mName;
}

std::vector<std::string> BluetoothProfileService::getUuids() const
{
	return mUuids;
}

void BluetoothProfileService::notifyStatusSubscribers(const std::string &adapterAddress, const std::string &address, bool connected)
{
	auto subscriptionIter = mGetStatusSubscriptions.find(address);
	if (subscriptionIter == mGetStatusSubscriptions.end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;

	pbnjson::JValue responseObj = buildGetStatusResp(connected, isDeviceConnecting(address), true,
	                                                 true, adapterAddress, address);

	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

bool BluetoothProfileService::isDeviceConnecting(const std::string &address)
{
	return (std::find(mConnectingDevices.begin(), mConnectingDevices.end(), address) != mConnectingDevices.end());
}

void BluetoothProfileService::markDeviceAsConnecting(const std::string &address)
{
	if (isDeviceConnecting(address))
		return;

	mConnectingDevices.push_back(address);
}

void BluetoothProfileService::markDeviceAsNotConnecting(const std::string &address)
{
	auto deviceIter = std::find(mConnectingDevices.begin(), mConnectingDevices.end(), address);

	if (deviceIter == mConnectingDevices.end())
		return;

	mConnectingDevices.erase(deviceIter);
}

bool BluetoothProfileService::isDeviceConnected(const std::string &address)
{
	return (std::find(mConnectedDevices.begin(), mConnectedDevices.end(), address) != mConnectedDevices.end());
}

void BluetoothProfileService::markDeviceAsConnected(const std::string &address)
{
	if (isDeviceConnected(address))
		return;

	mConnectedDevices.push_back(address);
}

void BluetoothProfileService::markDeviceAsNotConnected(const std::string &address)
{
	auto deviceIter = std::find(mConnectedDevices.begin(), mConnectedDevices.end(), address);

	if (deviceIter == mConnectedDevices.end())
		return;

	mConnectedDevices.erase(deviceIter);
}

void BluetoothProfileService::propertiesChanged(const std::string &address, BluetoothPropertiesList properties)
{
	bool connected = false;

	for (auto prop : properties)
	{
		switch (prop.getType())
		{
		case BluetoothProperty::Type::CONNECTED:
			connected = prop.getValue<bool>();

			if (!connected)
				markDeviceAsNotConnected(address);
			else
			{
				markDeviceAsNotConnecting(address);
				markDeviceAsConnected(address);
			}

			notifyStatusSubscribers(getManager()->getAddress(), address, connected);

			// When we switch from connected to disconnected we always just try to
			// remove all subscription. Called method will just return when subscriptions
			// are not available for the specified device.
			if (!connected)
				removeConnectWatchForDevice(convertToLower(address), !connected);

			break;
		default:
			break;
		}
	}
}

bool BluetoothProfileService::isDevicePaired(const std::string &address)
{
	BluetoothDevice *device = getManager()->findDevice(address);
	if (!device)
		return false;

	return device->getPaired();
}

bool BluetoothProfileService::isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string),
			                         PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	return true;
}

void BluetoothProfileService::connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	std::string address = convertToLower(requestObj["address"].asString());
	if (isDeviceConnecting(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_CONNECTING);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto isConnectedCallback = [this, requestMessage, adapterAddress, address](BluetoothError error, const BluetoothProperty &property) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		bool connected = property.getValue<bool>();

		if (connected)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECTED);
			LSMessageUnref(request.get());
			return;
		}

		markDeviceAsConnecting(address);
		notifyStatusSubscribers(adapterAddress, address, false);

		auto connectCallback = [this, requestMessage, adapterAddress, address](BluetoothError error) {
			BT_INFO("PROFILE", 0, "Return of connect is %d", error);

			LS::Message request(requestMessage);
			bool subscribed = false;

			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
				LSMessageUnref(request.get());

				markDeviceAsNotConnecting(address);
				notifyStatusSubscribers(adapterAddress, address, false);
				return;
			}

			// NOTE: At this point we're successfully connected but we will notify
			// possible subscribers once we get the update from the SIL through the
			// observer that the device is connected.

			// If we have a subscription we need to register a watch with the device
			// and update the client once the connection with the remote device is
			// dropped.
			if (request.isSubscription())
			{
				auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
				                    std::bind(&BluetoothProfileService::handleConnectClientDisappeared, this, adapterAddress, address));

				mConnectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(address, watch));
				subscribed = true;
			}
			markDeviceAsConnected(address);

			pbnjson::JValue responseObj = pbnjson::Object();

			responseObj.put("subscribed", subscribed);
			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("address", address);

			LSUtils::postToClient(request, responseObj);

			// We're done with sending out the first response to the client so
			// no use anymore for the message object
			LSMessageUnref(request.get());
		};

		BT_INFO("PROFILE", 0, "Service calls SIL API : connect to %s", address.c_str());
		mImpl->connect(address, connectCallback);
	};

	// Before we start to connect with the device we have to make sure
	// we're not already connected with it.
	mImpl->getProperty(address, BluetoothProperty::Type::CONNECTED, isConnectedCallback);
}

bool BluetoothProfileService::connect(LSMessage &message)
{
	BT_INFO("PROFILE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isConnectSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();
	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (!isDevicePaired(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEV_NOT_PAIRED, true);
		return true;
	}

	connectToStack(request, requestObj, adapterAddress);

	return true;
}

void BluetoothProfileService::handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &address)
{
	auto watchIter = mConnectWatches.find(address);
	if (watchIter == mConnectWatches.end())
		return;

	if (!mImpl)
		return;

	auto disconnectCallback = [this, address, adapterAddress](BluetoothError error) {
		markDeviceAsNotConnected(address);
		markDeviceAsNotConnecting(address);
	};

	mImpl->disconnect(address, disconnectCallback);
}

void BluetoothProfileService::removeConnectWatchForDevice(const std::string &key, bool disconnected, bool remoteDisconnect)
{
	auto watchIter = mConnectWatches.find(key);
	if (watchIter == mConnectWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", true);
	if (disconnected)
	{
		if (remoteDisconnect)
			responseObj.put("disconnectByRemote", true);
		else
			responseObj.put("disconnectByRemote", false);
	}
	responseObj.put("adapterAddress", getManager()->getAddress());

	LSUtils::postToClient(watch->getMessage(), responseObj);

	// There will be only one client per device so we can safely remove it here
	mConnectWatches.erase(watchIter);
	delete watch;
}

bool BluetoothProfileService::isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string))  REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	return true;
}

void BluetoothProfileService::disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	std::string address = convertToLower(requestObj["address"].asString());
	if (!mManager->isDeviceAvailable(address))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return;
	}

	if (!isDeviceConnected(address))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto disconnectCallback = [this, requestMessage, adapterAddress, address](BluetoothError error) {
		BT_INFO("PROFILE", 0, "Return of disconnect is %d", error);

		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_DISCONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);
		LSUtils::postToClient(request, responseObj);

		removeConnectWatchForDevice(address, true, false);
		markDeviceAsNotConnected(address);
		markDeviceAsNotConnecting(address);
		LSMessageUnref(request.get());
	};

	BT_INFO("PROFILE", 0, "Service calls SIL API : disconnect to %s", address.c_str());
	mImpl->disconnect(address, disconnectCallback);
}

bool BluetoothProfileService::disconnect(LSMessage &message)
{
	BT_INFO("PROFILE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isDisconnectSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	disconnectToStack(request, requestObj, adapterAddress);

	return true;
}

std::vector<std::string> BluetoothProfileService::strToProfileRole(const std::string & input)
{
	std::vector<std::string> ret;
	for (auto uuid : mUuids)
	{
		std::string luuid = convertToLower(uuid);
		auto iter = allServiceClasses.find(luuid);
		if (iter != allServiceClasses.end())
		{
			if(input.length() > 0)
			{
				if(convertToLower(iter->second.getMnemonic()) == convertToLower(input))
				{
					ret.push_back(iter->first);
				}
			}
			else
			{
				ret.push_back(iter->first);
			}
		}
	}
	return ret;
}

bool BluetoothProfileService::enable(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	std::string adapterAddress;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(role, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
	{
		return true;
	}

	if(!getManager()->getPowered(adapterAddress))
	{
		return true;
	}

	std::string role = "";
	if(requestObj.hasKey("role"))
	{
		role = convertToLower(requestObj["role"].asString());
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	if(role.length() > 0 && getManager()->isRoleEnable(adapterAddress, role))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_ENABLED);
		return true;
	}

	auto enableCallback = [this, requestMessage, adapterAddress](BluetoothError error) {
		LS::Message request(requestMessage);
		mEnabledRoles.pop_back();

		if (error != BLUETOOTH_ERROR_NONE)
		{
			mEnabledRoles.clear();
			LSUtils::respondWithError(request, error);
			LSMessageUnref(request.get());
			return;
		}
		if(mEnabledRoles.size() > 0)
		{
			mImpl->enable(mEnabledRoles.back(), mCallback);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(request, responseObj);

		LSMessageUnref(request.get());
	};

	initialize();

	if(mImpl)
	{
		mEnabledRoles = strToProfileRole(role);
		if(mEnabledRoles.size() == 0)
		{
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
			return true;
		}
		mCallback = enableCallback;
		mImpl->enable(mEnabledRoles.back(), mCallback);
	}
	return true;
}

bool BluetoothProfileService::disable(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(role, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
	{
		return true;
	}

	std::string role = "";
	if(requestObj.hasKey("role"))
	{
		role = convertToLower(requestObj["role"].asString());
	}
	if(role.length() > 0 && !getManager()->isRoleEnable(adapterAddress, role))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_ENABLED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto disableCallback = [this, requestMessage, adapterAddress](BluetoothError error) {
		LS::Message request(requestMessage);
		mEnabledRoles.pop_back();

		if (error != BLUETOOTH_ERROR_NONE)
		{
			mEnabledRoles.clear();
			LSUtils::respondWithError(request, error);
			LSMessageUnref(request.get());
			return;
		}
		if(mEnabledRoles.size() > 0)
		{
			mImpl->disable(mEnabledRoles.back(), mCallback);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(request.get());
	};

	mEnabledRoles = strToProfileRole(role);
	if(mEnabledRoles.size() == 0)
	{
		LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		return true;
	}
	mCallback = disableCallback;
	mImpl->disable(mEnabledRoles.back(), mCallback);

	return true;
}

bool BluetoothProfileService::isGetStatusSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string),
			                                  PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		return false;
	}

	return true;
}

bool BluetoothProfileService::getStatus(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	bool subscribed = false;

	if (!mImpl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isGetStatusSchemaAvailable(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = convertToLower(requestObj["address"].asString());
	if (!mManager->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	if (request.isSubscription())
	{
		LS::SubscriptionPoint *subscriptionPoint = 0;

		auto subscriptionIter = mGetStatusSubscriptions.find(deviceAddress);
		if (subscriptionIter == mGetStatusSubscriptions.end())
		{
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			mGetStatusSubscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(deviceAddress, subscriptionPoint));
		}
		else
		{
			subscriptionPoint = subscriptionIter->second;
		}

		subscriptionPoint->subscribe(request);
		subscribed = true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto getPropCallback = [this, requestMessage, deviceAddress, adapterAddress, subscribed](BluetoothError error, const BluetoothProperty &property) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_STATE_ERR);
			return;
		}

		pbnjson::JValue responseObj = buildGetStatusResp(property.getValue<bool>(), isDeviceConnecting(deviceAddress), subscribed,
			true, adapterAddress, deviceAddress);

		LSUtils::postToClient(request, responseObj);
	};

	mImpl->getProperty(deviceAddress, BluetoothProperty::Type::CONNECTED, getPropCallback);

	return true;
}

void BluetoothProfileService::appendCommonProfileStatus(pbnjson::JValue responseObj, bool connected, bool connecting,
        bool subscribed, bool returnValue, std::string adapterAddress, std::string deviceAddress)
{
	responseObj.put("connected", connected);
	responseObj.put("connecting", connecting);
	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", returnValue);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
}

pbnjson::JValue BluetoothProfileService::buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
        std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	appendCommonProfileStatus(responseObj, connected, connecting, subscribed,
	                          returnValue, adapterAddress, deviceAddress);

	return responseObj;
}
