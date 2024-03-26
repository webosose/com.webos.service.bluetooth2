// Copyright (c) 2014-2024 LG Electronics, Inc.
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


#include <pbnjson.hpp>
#include <assert.h>
#undef NDEBUG

#include <iostream>
#include <fstream>
#include <regex>

#include "bluetoothmanagerservice.h"
#include "bluetoothdevice.h"
#include "bluetoothsilfactory.h"
#include "bluetoothserviceclasses.h"
#include "bluetootherrors.h"
#include "bluetoothftpprofileservice.h"
#include "bluetoothoppprofileservice.h"
#include "bluetootha2dpprofileservice.h"
#include "bluetoothgattprofileservice.h"
#include "bluetoothpbapprofileservice.h"
#include "bluetoothavrcpprofileservice.h"
#include "bluetoothsppprofileservice.h"
#include "bluetoothhfpprofileservice.h"
#include "bluetoothpanprofileservice.h"
#include "bluetoothmapprofileservice.h"
#include "bluetoothhidprofileservice.h"
#include "bluetoothmeshprofileservice.h"
#include "bluetoothgattancsprofile.h"
#include "bluetoothmanageradapter.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "config.h"
#include "utils.h"
#ifdef MULTI_SESSION_SUPPORT
#include "bluetoothpdminterface.h"
#endif

#define BLUETOOTH_LE_START_SCAN_MAX_ID 999
#define MAX_ADVERTISING_DATA_BYTES 31

using namespace std::placeholders;

static const std::map<std::string, BluetoothPairingIOCapability> pairingIOCapability =
{
	{"NoInputNoOutput", BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT},
	{"DisplayOnly", BLUETOOTH_PAIRING_IO_CAPABILITY_DISPLAY_ONLY},
	{"DisplayYesNo", BLUETOOTH_PAIRING_IO_CAPABILITY_DISPLAY_YES_NO},
	{"KeyboardOnly", BLUETOOTH_PAIRING_IO_CAPABILITY_KEYBOARD_ONLY},
	{"KeyboardDisplay", BLUETOOTH_PAIRING_IO_CAPABILITY_KEYBOARD_DISPLAY}
};

BluetoothManagerService::BluetoothManagerService() :
	LS::Handle("com.webos.service.bluetooth2"),
	mAdvertising(false),
	mWoBleEnabled(false),
	mKeepAliveEnabled(false),
	mKeepAliveInterval(1),
	mSil(0),
	mDefaultAdapter(0),
	mAdvertisingWatch(0),
	mGattAnsc(0)
#ifdef MULTI_SESSION_SUPPORT
	,
	mPdmInterface(this)
#endif
{
	std::string bluetoothCapability = WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY;
	const char* capabilityOverride = getenv("WEBOS_BLUETOOTH_PAIRING_IO_CAPABILITY");
	if (capabilityOverride != NULL)
		bluetoothCapability = capabilityOverride;

	auto it = pairingIOCapability.find(bluetoothCapability);
	if (it != pairingIOCapability.cend())
		mPairingIOCapability = it->second;
	else
	{
		BT_WARNING(MSGID_INVALID_PAIRING_CAPABILITY, 0, "Pairing capability not valid, fallback to simple pairing");
		mPairingIOCapability = BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT;
	}

	mEnabledServiceClasses = split(std::string(WEBOS_BLUETOOTH_ENABLED_SERVICE_CLASSES), ' ');

	mWoBleTriggerDevices.clear();
	createProfiles();

	BT_DEBUG("Creating SIL for API version %d, capability %s", BLUETOOTH_SIL_API_VERSION, bluetoothCapability.c_str());
	mSil = BluetoothSILFactory::create(BLUETOOTH_SIL_API_VERSION, mPairingIOCapability);

	if (mSil)
	{
		mSil->registerObserver(this);
		assignDefaultAdapter();
	}

	LS_CREATE_CATEGORY_BEGIN(BluetoothManagerService, adapter)
		LS_CATEGORY_METHOD(setState)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_METHOD(queryAvailable)
		LS_CATEGORY_METHOD(startDiscovery)
		LS_CATEGORY_METHOD(cancelDiscovery)
		LS_CATEGORY_METHOD(pair)
		LS_CATEGORY_METHOD(unpair)
		LS_CATEGORY_METHOD(supplyPasskey)
		LS_CATEGORY_METHOD(supplyPinCode)
		LS_CATEGORY_METHOD(supplyPasskeyConfirmation)
		LS_CATEGORY_METHOD(cancelPairing)
		LS_CATEGORY_METHOD(awaitPairingRequests)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothManagerService, adapter_internal)
		LS_CATEGORY_METHOD(setWoBle)
		LS_CATEGORY_METHOD(setWoBleTriggerDevices)
		LS_CATEGORY_METHOD(getWoBleStatus)
		LS_CATEGORY_METHOD(sendHciCommand)
		LS_CATEGORY_METHOD(setTrace)
		LS_CATEGORY_METHOD(getTraceStatus)
		LS_CATEGORY_METHOD(setKeepAlive)
		LS_CATEGORY_METHOD(getKeepAliveStatus)
		LS_CATEGORY_MAPPED_METHOD(startDiscovery, startFilteringDiscovery)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothManagerService, device)
		LS_CATEGORY_MAPPED_METHOD(getConnectedDevices, getConnectedDevices)
		LS_CATEGORY_MAPPED_METHOD(getPairedDevices, getPairedDevicesStatus)
		LS_CATEGORY_MAPPED_METHOD(getDiscoveredDevice, getDiscoveredDeviceStatus)
		LS_CATEGORY_MAPPED_METHOD(getStatus, getDeviceStatus)
		LS_CATEGORY_MAPPED_METHOD(setState, setDeviceState)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothManagerService, device_internal)
		LS_CATEGORY_METHOD(getLinkKey)
		LS_CATEGORY_METHOD(startSniff)
		LS_CATEGORY_METHOD(stopSniff)
		LS_CATEGORY_MAPPED_METHOD(getStatus, getFilteringDeviceStatus)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothManagerService, le)
		/*LS_CATEGORY_METHOD(configureAdvertisement)*/
		LS_CATEGORY_METHOD(startAdvertising)
		LS_CATEGORY_METHOD(updateAdvertising)
		/*LS_CATEGORY_METHOD(stopAdvertising)*/
		LS_CATEGORY_METHOD(disableAdvertising)
		LS_CATEGORY_MAPPED_METHOD(getStatus, getAdvStatus)
		LS_CATEGORY_METHOD(startScan)
	LS_CREATE_CATEGORY_END

	registerCategory("/adapter", LS_CATEGORY_TABLE_NAME(adapter), NULL, NULL);
	setCategoryData("/adapter", this);

	registerCategory("/adapter/internal", LS_CATEGORY_TABLE_NAME(adapter_internal), NULL, NULL);
	setCategoryData("/adapter/internal", this);

	registerCategory("/device", LS_CATEGORY_TABLE_NAME(device), NULL, NULL);
	setCategoryData("/device", this);

	registerCategory("/device/internal", LS_CATEGORY_TABLE_NAME(device_internal), NULL, NULL);
	setCategoryData("/device/internal", this);

	registerCategory("/le", LS_CATEGORY_TABLE_NAME(le), NULL, NULL);
	setCategoryData("/le", this);

#ifdef MULTI_SESSION_SUPPORT
	for (int32_t idx = 0; idx < MAX_SUBSCRIPTION_SESSIONS; idx++)
	{
		mGetStatusSubscriptions[idx].setServiceHandle(this);
		mQueryAvailableSubscriptions[idx].setServiceHandle(this);
	}
#else
	mGetStatusSubscriptions.setServiceHandle(this);
	mQueryAvailableSubscriptions.setServiceHandle(this);
#endif

	mGetAdvStatusSubscriptions.setServiceHandle(this);
	mGetKeepAliveStatusSubscriptions.setServiceHandle(this);
}

BluetoothManagerService::~BluetoothManagerService()
{
	BT_DEBUG("Shutting down bluetooth manager service ...");

	if (mSil)
		delete mSil;

	if (mGattAnsc)
		delete mGattAnsc;

	BluetoothSILFactory::freeSILHandle();
}

//TODO move to BluetoothManagerAdapter (Based on discussion mProfiles should be part of each adapter?)
bool BluetoothManagerService::isServiceClassEnabled(const std::string &serviceClass)
{
	for (auto currentServiceClass : mEnabledServiceClasses)
	{
		if (currentServiceClass == serviceClass)
			return true;
	}

	return false;
}

bool BluetoothManagerService::isDefaultAdapterAvailable() const
{
	return mDefaultAdapter != 0;
}

bool BluetoothManagerService::isAdapterAvailable(const std::string &address)
{
	for(auto it = mAdaptersInfo.begin(); it != mAdaptersInfo.end(); it++)
	{
		std::string convertedAddress = convertToLower(address);
		if (it->first.compare(convertedAddress) == 0)
		{
			return true;
		}
	}

	return false;
}

bool BluetoothManagerService::isRequestedAdapterAvailable(LS::Message &request, const pbnjson::JValue &requestObj, std::string &adapterAddress)
{
#ifdef MULTI_SESSION_SUPPORT
	auto message = request.get();
	auto displayId = LSUtils::getDisplaySetIdIndex(*message, this);

#if 0
	/*Give high prioriy to adapterAddress*/
	if (requestObj.hasKey("adapterAddress"))
	{
		BT_DEBUG("Request contains adapterAddress so assigning adapterAddress");
		/*TODO Currently priority is given to adapterAddress so returning HOST
		 Later priority should be given to container session & adapterAddress
		 assigned to container session should be returned*/
		displayId = LSUtils::DisplaySetId::HOST;
	}
#endif

	if (displayId != LSUtils::DisplaySetId::HOST)
	{
		for (auto it = mAdaptersInfo.begin(); it != mAdaptersInfo.end(); it++)
		{
			if (it->second->getHciIndex() == displayId)
			{
				BT_DEBUG("Adapter for displayId %d found adapterAddress %s", displayId, it->second->getAddress().c_str());
				adapterAddress = it->second->getAddress();
				return true;
			}
		}

		BT_DEBUG("Adapter for displayId %d is not found", displayId);
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return false;
	}
	/*Request either from host or no session exist*/
	else
	{
		if (requestObj.hasKey("adapterAddress"))
		{
			adapterAddress = convertToLower(requestObj["adapterAddress"].asString());
			if (!isValidAddress(adapterAddress) || !isAdapterAvailable(adapterAddress))
			{
				LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
				return false;
			}
			BT_DEBUG("Host request Adapter address %s", adapterAddress.c_str());
		}
		else
		{
			BT_DEBUG("Host request doesn't contain adapterAddress so using default adapter address %s", mAddress.c_str());
			adapterAddress = mAddress;
			if (!isAdapterAvailable(adapterAddress))
			{
				LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
				return false;
			}
		}
	}

	return true;
#else
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = convertToLower(requestObj["adapterAddress"].asString());
		if (!isValidAddress(adapterAddress) || !isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return false;
		}
	}
	else
	{
		adapterAddress = mAddress;
		if (!isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
			return false;
		}
	}

	return true;
#endif
}

bool BluetoothManagerService::isRoleEnable(const std::string &address, const std::string &role)
{
	for (auto profile : findAdapterInfo(address)->getSupportedServiceClasses())
	{
		if(convertToLower(profile.getMnemonic()) == convertToLower(role))
		{
			return true;
		}
	}
	return false;
}

std::string BluetoothManagerService::getMessageOwner(LSMessage *message)
{
        if (NULL == message)
                return EMPTY_STRING;

        std::string returnName = EMPTY_STRING;
        const char *appName = LSMessageGetApplicationID(message);
        if (NULL == appName)
        {
                appName = LSMessageGetSenderServiceName(message);
                if (appName != NULL)
                        returnName = appName;
        }
        else
        {
                returnName = appName;
                std::size_t found = returnName.find_first_of(" ");
                if (found != std::string::npos)
                        returnName.erase(found, returnName.size()-found);
        }

        return returnName;
}

int BluetoothManagerService::getAdvSize(AdvertiseData advData, bool flagRequired)
{
	//length (1byte) + type (1byte) + flag (1byte)
	int flagsFieldBytes = 3;
	// length (1byte) + type (1 byte)
	int overheadBytesPerField = 2;
	int numUuuid = 0;
	//Currently only 16-bit uuid supported
	int uuidSize = 2;

	int size = flagRequired ? flagsFieldBytes : 0;

	if (!advData.services.empty())
	{
		numUuuid = advData.services.size();
		for (auto it = advData.services.begin(); it != advData.services.end(); it++)
		{
			auto data = it->second;
			if (!(it->second.empty()))
			{
				size = size + overheadBytesPerField + data.size();
				break;
			}
		}
	}

	if (!advData.manufacturerData.empty())
	{
		size = size + overheadBytesPerField + advData.manufacturerData.size();
	}

	if (numUuuid)
	{
		size = size + overheadBytesPerField + (numUuuid * uuidSize);
	}

	for (auto it = advData.proprietaryData.begin(); it != advData.proprietaryData.end(); it++)
	{
		auto data = it->data;
		size = size + data.size() + overheadBytesPerField;
	}

	if (advData.includeTxPower)
	{
		size += overheadBytesPerField + 1; // tx power level value is one byte.
	}

	if (advData.includeName)
	{
		//TODO multi adapter support required
		size += overheadBytesPerField + findAdapterInfo(mAddress)->getName().length();
	}

	return size;
}

bool BluetoothManagerService::isValidAddress(std::string& address)
{
	std::replace(address.begin(), address.end(), '-', ':');
	std::regex addressRegex("^([0-9A-Fa-f]{2}[:]){5}([0-9A-Fa-f]{2})$");
	return std::regex_match(address, addressRegex);
}

bool BluetoothManagerService::getAdvertisingState() {
	return mAdvertising;
}

void BluetoothManagerService::setAdvertisingState(bool advertising) {
	mAdvertising = advertising;
}

BluetoothAdapter* BluetoothManagerService::getDefaultAdapter() const
{
	return mDefaultAdapter;
}

std::string BluetoothManagerService::getAddress() const
{
	return mAddress;
}

bool BluetoothManagerService::isDeviceAvailable(const std::string &adapterAddress, const std::string &address) const
{
	auto devices = findAdapterInfo(adapterAddress)->getDevices();
	std::string convertedAddress = convertToLower(address);
	auto deviceIter = devices.find(convertedAddress);
	if (deviceIter == devices.end())
		return false;
	BluetoothDevice *device = deviceIter->second;
	if(convertToLower(device->getAddress()) == convertedAddress)
		return true;

	return false;
}

bool BluetoothManagerService::isDeviceAvailable(const std::string &address) const
{
	auto devices = findAdapterInfo(mAddress)->getDevices();
	std::string convertedAddress = convertToLower(address);
	auto deviceIter = devices.find(convertedAddress);
	if (deviceIter == devices.end())
		return false;

	BluetoothDevice *device = deviceIter->second;
	if(device->getAddress() == convertedAddress)
		return true;

	return false;
}

void BluetoothManagerService::createProfiles()
{
	if (isServiceClassEnabled("FTP"))
		mProfiles.push_back(new BluetoothFtpProfileService(this));

	if (isServiceClassEnabled("OPP"))
		mProfiles.push_back(new BluetoothOppProfileService(this));

	if (isServiceClassEnabled("A2DP"))
		mProfiles.push_back(new BluetoothA2dpProfileService(this));

	if (isServiceClassEnabled("GATT"))
	{
		BluetoothGattProfileService *gattService = new BluetoothGattProfileService(this);

		if (isServiceClassEnabled("ANCS")) {
			mGattAnsc = new BluetoothGattAncsProfile(this, gattService);
			//BluetoothGattAncsProfile registers with gattService
		}
		mProfiles.push_back(gattService);
	}
	if (isServiceClassEnabled("PBAP"))
		mProfiles.push_back(new BluetoothPbapProfileService(this));

	if (isServiceClassEnabled("AVRCP"))
		mProfiles.push_back(new BluetoothAvrcpProfileService(this));

	if (isServiceClassEnabled("SPP"))
		mProfiles.push_back(new BluetoothSppProfileService(this));

	if (isServiceClassEnabled("HFP"))
		mProfiles.push_back(new BluetoothHfpProfileService(this));

	if (isServiceClassEnabled("PAN"))
		mProfiles.push_back(new BluetoothPanProfileService(this));

	if (isServiceClassEnabled("HID"))
		mProfiles.push_back(new BluetoothHidProfileService(this));

	if (isServiceClassEnabled("MAP"))
		mProfiles.push_back(new BluetoothMapProfileService(this));
	if (isServiceClassEnabled("MESH"))
	{
			BT_INFO("MANAGER_SERVICE", 0, "Mesh profile service created : [%s : %d]", __FUNCTION__, __LINE__);
			mProfiles.push_back(new BluetoothMeshProfileService(this));
	}
}

void BluetoothManagerService::notifySubscribersAboutStateChange()
{
	pbnjson::JValue responseObj = pbnjson::Object();

#ifdef MULTI_SESSION_SUPPORT
	for (int i = 0; i < MAX_SUBSCRIPTION_SESSIONS; i++)
	{
		appendCurrentStatus(responseObj, (LSUtils::DisplaySetId)i);
		responseObj.put("returnValue", true);
		LSUtils::postToSubscriptionPoint(&(mGetStatusSubscriptions[i]), responseObj);
	}
#else
	appendCurrentStatus(responseObj);

	responseObj.put("returnValue", true);

	LSUtils::postToSubscriptionPoint(&mGetStatusSubscriptions, responseObj);
#endif
}

void BluetoothManagerService::notifySubscribersAdvertisingChanged(std::string adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("advertising", mAdvertising);
	responseObj.put("returnValue",true);
	responseObj.put("subscribed",true);

	LSUtils::postToSubscriptionPoint(&mGetAdvStatusSubscriptions, responseObj);
}

void BluetoothManagerService::notifySubscribersAdaptersChanged()
{
	pbnjson::JValue responseObj = pbnjson::Object();
#ifdef MULTI_SESSION_SUPPORT
	for (int32_t i = 0; i < MAX_SUBSCRIPTION_SESSIONS; i++)
	{
		appendAvailableStatus(responseObj, (LSUtils::DisplaySetId)i);

		responseObj.put("returnValue", true);

		LSUtils::postToSubscriptionPoint(&(mQueryAvailableSubscriptions[i]), responseObj);
	}
#else
	appendAvailableStatus(responseObj);

	responseObj.put("returnValue", true);

	LSUtils::postToSubscriptionPoint(&mQueryAvailableSubscriptions, responseObj);
#endif
}

void BluetoothManagerService::adaptersChanged()
{
	BT_INFO("MANAGER_SERVICE", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	assignDefaultAdapter();

	mAdapters = mSil->getAdapters();

	for (auto it = mAdaptersInfo.begin(); it != mAdaptersInfo.end(); )
	{
		bool found = false;
		for (auto silAdapter : mAdapters)
		{
			if (silAdapter == it->second->getAdapter())
			{
				found = true;
				 break;
			}
		}

		if (!found)
		{
			BT_INFO("MANAGER_SERVICE", 0, "adaptersChanged erasing adapter [%s] from list", it->first.c_str());
			delete it->second;
			it = mAdaptersInfo.erase(it);
			notifySubscribersAboutStateChange();
		}
		else
		{
			++it;
		}
	}

	for (auto adapter : mAdapters)
	{
		BT_DEBUG("Updating properties from adapters");

		adapter->getAdapterProperty(BluetoothProperty::Type::BDADDR,[this, adapter](BluetoothError error, const BluetoothProperty &property) {
			if (error != BLUETOOTH_ERROR_NONE)
				return;

			updateFromAdapterAddressForQueryAvailable(adapter, property);
		});
	}

	notifySubscribersAdaptersChanged();
}

void BluetoothManagerService::initializeProfiles()
{
	for (auto profile : mProfiles)
	{
		profile->initialize();
	}
}

void BluetoothManagerService::initializeProfiles(BluetoothManagerAdapter *adapter)
{
	for (auto profile : mProfiles)
	{
		profile->initialize(adapter->getAddress());
	}
}

void BluetoothManagerService::resetProfiles()
{
	for (auto profile : mProfiles)
	{
		profile->reset();
	}
}

void BluetoothManagerService::resetProfiles(const std::string &adapterAddress)
{
	for (auto profile : mProfiles)
	{
		profile->reset(adapterAddress);
	}
}

void BluetoothManagerService::assignDefaultAdapter()
{
	if (!mSil)
		return;

	mDefaultAdapter = mSil->getDefaultAdapter();

	if (!mDefaultAdapter)
	{
		resetProfiles();
		return;
	}


	BT_DEBUG("Updating properties from default adapter");
	mDefaultAdapter->getAdapterProperties([this](BluetoothError error, const BluetoothPropertiesList &properties) {
		if (error != BLUETOOTH_ERROR_NONE)
			return;

		auto adapter = findAdapterInfo(mAddress);
		if (adapter)
			adapter->updateFromAdapterProperties(properties);
	});
}

BluetoothManagerAdapter* BluetoothManagerService::findAdapterInfo(const std::string &address) const
{
	std::string convertedAddress = convertToLower(address);
	auto adapterInfoIter = mAdaptersInfo.find(convertedAddress);
	if (adapterInfoIter == mAdaptersInfo.end())
	{
		convertedAddress = convertToUpper(address);
		adapterInfoIter = mAdaptersInfo.find(convertedAddress);
		if (adapterInfoIter == mAdaptersInfo.end())
			return 0;
	}

	return adapterInfoIter->second;
}

//TODO support multi adapter, remove this method once multi adapter is supported
//Still used by a2dp profile
BluetoothDevice* BluetoothManagerService::findDevice(const std::string &address) const
{
	return findAdapterInfo(mAddress)->findDevice(address);
}

BluetoothDevice* BluetoothManagerService::findDevice(const std::string &adapterAddress, const std::string &address) const
{
	return findAdapterInfo(adapterAddress)->findDevice(address);
}

void BluetoothManagerService::updateFromAdapterAddressForQueryAvailable(BluetoothAdapter *adapter, const BluetoothProperty &property)
{
	if (property.getType() != BluetoothProperty::Type::BDADDR)
		return;

	std::string address = convertToLower(property.getValue<std::string>());
	BT_DEBUG("##### Bluetooth adapter address has changed to %s", address.c_str());

	auto adapterInfoIter = mAdaptersInfo.find(address);
	if (adapterInfoIter != mAdaptersInfo.end())
		return;

	BluetoothManagerAdapter *btmngrAdapter = new BluetoothManagerAdapter(this, address);

	if (adapter == mDefaultAdapter)
	{
		mAddress = address;
		btmngrAdapter->setDefaultAdapter(true);
	}

	btmngrAdapter->setAdapter(adapter);

	adapter->registerObserver(btmngrAdapter);
	mAdaptersInfo.insert(std::pair<std::string, BluetoothManagerAdapter*>(address, btmngrAdapter));

	resetProfiles(address);
	initializeProfiles(btmngrAdapter);

	if (mPairingIOCapability == BLUETOOTH_PAIRING_IO_CAPABILITY_NO_INPUT_NO_OUTPUT)
		setPairableState(address, true);

	adapter->getAdapterProperties([this, address](BluetoothError error, const BluetoothPropertiesList &properties) {
		if (error != BLUETOOTH_ERROR_NONE)
			return;

		auto adapter = findAdapterInfo(address);
		if (adapter)
			adapter->updateFromAdapterProperties(properties);
	});
}

void BluetoothManagerService::adapterKeepAliveStateChanged(bool enabled)
{
	BT_INFO("MANAGER_SERVICE", 0, "Observer is called : [%s : %d] enabled : %d", __FUNCTION__, __LINE__, enabled);

	if (mKeepAliveEnabled == enabled)
		return;
	else
		mKeepAliveEnabled = enabled;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("subscribed", true);
	responseObj.put("keepAliveEnabled", mKeepAliveEnabled);
	responseObj.put("keepAliveInterval", (int32_t)mKeepAliveInterval);

	LSUtils::postToSubscriptionPoint(&mGetKeepAliveStatusSubscriptions, responseObj);
}

bool BluetoothManagerService::setState(LSMessage &msg)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&msg);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_8(
                                    PROP(adapterAddress, string), PROP(name, string), PROP(powered, boolean),
                                    PROP(discoveryTimeout, integer), PROP(discoverable, boolean),
                                    PROP(discoverableTimeout, integer), PROP(pairable, boolean),
                                    PROP(pairableTimeout, integer)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothManagerAdapter* adapter = findAdapterInfo(adapterAddress);

	return adapter->setState(request, requestObj);
}

BluetoothAdapter* BluetoothManagerService::getAdapter(const std::string &address)
{
	for(auto it = mAdaptersInfo.begin(); it != mAdaptersInfo.end(); it++)
	{
		std::string convertedAddress = convertToLower(address);
		if (it->first.compare(convertedAddress) == 0)
		{
			return it->second->getAdapter();
		}
	}
	return nullptr;
}

bool BluetoothManagerService::setPairableState(const std::string &adapterAddress, bool value)
{
	BT_DEBUG("Setting pairable to %d", value);
	bool retVal = false;

	auto pairableCB = [this, &retVal, adapterAddress](BluetoothError error) {
		if (error == BLUETOOTH_ERROR_NONE)
		{
			BT_DEBUG("Pairable value set in SIL with no errors");
			findAdapterInfo(adapterAddress)->getPairState().setPairable(true);
			notifySubscribersAboutStateChange();
			retVal = true;
		}
	};
        if(findAdapterInfo(adapterAddress) && findAdapterInfo(adapterAddress)->getAdapter())
        {
	    findAdapterInfo(adapterAddress)->getAdapter()->setAdapterProperty(BluetoothProperty(BluetoothProperty::Type::PAIRABLE, value), pairableCB);
        }
        else
        {
            BT_DEBUG("Adapter not found for address: %s", adapterAddress.c_str());
        }

	return retVal;
}

#ifdef MULTI_SESSION_SUPPORT
void BluetoothManagerService::appendCurrentStatus(pbnjson::JValue &object, LSUtils::DisplaySetId displayId)
#else
void BluetoothManagerService::appendCurrentStatus(pbnjson::JValue &object)
#endif
{
	pbnjson::JValue adaptersObj = pbnjson::Array();

	for (auto adapterInfoIter : mAdaptersInfo)
	{
		auto adapterInfo = adapterInfoIter.second;

#ifdef MULTI_SESSION_SUPPORT
		BT_INFO("MANAGER_SERVICE", 0, "displayId %d", displayId);
		if (displayId != LSUtils::DisplaySetId::HOST && adapterInfo->getHciIndex() != displayId)
			continue;
#endif

		pbnjson::JValue adapterObj = pbnjson::Object();
		adapterObj.put("powered", adapterInfo->getPowerState());
		adapterObj.put("name", adapterInfo->getName());
		adapterObj.put("interfaceName", adapterInfo->getInterface());
		adapterObj.put("adapterAddress", adapterInfo->getAddress());
		adapterObj.put("discovering", adapterInfo->getDiscoveringState());
		// pbnjson doesn't support unsigned int, so using int32_t for discoveryTimeout
		// and discoverableTimeout
		adapterObj.put("discoveryTimeout", (int32_t) adapterInfo->getDisoveryTimeout());
		adapterObj.put("discoverable", adapterInfo->getDiscoverable());
		adapterObj.put("discoverableTimeout", (int32_t) adapterInfo->getDiscoverableTimeout());
		adapterObj.put("pairable", adapterInfo->getPairState().isPairable());
		adapterObj.put("pairableTimeout", (int32_t) adapterInfo->getPairState().getPairableTimeout());
		adapterObj.put("pairing", adapterInfo->getPairState().isPairing());

		adaptersObj.append(adapterObj);
	}

	object.put("adapters", adaptersObj);
}

#ifdef MULTI_SESSION_SUPPORT
void BluetoothManagerService::appendAvailableStatus(pbnjson::JValue &object, LSUtils::DisplaySetId displayId)
#else
void BluetoothManagerService::appendAvailableStatus(pbnjson::JValue &object)
#endif
{
	pbnjson::JValue adaptersObj = pbnjson::Array();

	for (auto adapterInfoIter : mAdaptersInfo)
	{
		auto adapterInfo = adapterInfoIter.second;

#ifdef MULTI_SESSION_SUPPORT
		BT_INFO("MANAGER_SERVICE", 0, "displayId %d", displayId);
		if (displayId != LSUtils::DisplaySetId::HOST && adapterInfo->getHciIndex() != displayId)
			continue;
#endif

		pbnjson::JValue adapterObj = pbnjson::Object();

		adapterObj.put("adapterAddress", adapterInfo->getAddress());
		if (adapterInfo->getAddress() == mAddress)
			adapterObj.put("default", true);
		else
			adapterObj.put("default", false);
		// pbnjson doesn't support unsigned int, so using int32_t for classOfDevice
		adapterObj.put("classOfDevice", (int32_t)adapterInfo->getClassOfDevice());
		adapterObj.put("stackName", adapterInfo->getStackName());
		adapterObj.put("stackVersion", adapterInfo->getStackVersion());
		adapterObj.put("firmwareVersion", adapterInfo->getFirmwareVersion());
		adapterInfo->appendSupportedServiceClasses(adapterObj, adapterInfo->getSupportedServiceClasses());

		adaptersObj.append(adapterObj);
	}

	object.put("adapters", adaptersObj);
}

bool BluetoothManagerService::getStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	bool subscribed = false;

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, SCHEMA_1(PROP(subscribe, boolean)), &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

#ifdef MULTI_SESSION_SUPPORT
	LSUtils::DisplaySetId displaySetIndex = LSUtils::getDisplaySetIdIndex(message, this);
	if (request.isSubscription())
	{
		mGetStatusSubscriptions[displaySetIndex].subscribe(request);
		subscribed = true;
	}
	appendCurrentStatus(responseObj, displaySetIndex);
#else
	if (request.isSubscription())
	{
		mGetStatusSubscriptions.subscribe(request);
		subscribed = true;
	}
	appendCurrentStatus(responseObj);
#endif

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::queryAvailable(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	bool subscribed = false;

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, SCHEMA_1(PROP(subscribe, boolean)), &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

#ifdef MULTI_SESSION_SUPPORT
	LSUtils::DisplaySetId displaySetIndex = LSUtils::getDisplaySetIdIndex(message, this);
	if (request.isSubscription())
	{
		mQueryAvailableSubscriptions[displaySetIndex].subscribe(request);
		subscribed = true;
	}
	appendAvailableStatus(responseObj, displaySetIndex);
#else
	if (request.isSubscription())
	{
		mQueryAvailableSubscriptions.subscribe(request);
		subscribed = true;
	}
	appendAvailableStatus(responseObj);
#endif

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::startFilteringDiscovery(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_2(PROP(typeOfDevice, string), PROP(accessCode, string)));
	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	std::string typeOfDevice;
	std::string accessCode;
	TransportType transportType = TransportType::BT_TRANSPORT_TYPE_NONE;
	InquiryAccessCode inquiryAccessCode = InquiryAccessCode::BT_ACCESS_CODE_NONE; // CID 166097

	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!findAdapterInfo(adapterAddress)->getPowerState())
	{
		LSUtils::respondWithError(request, BT_ERR_START_DISC_ADAPTER_OFF_ERR);
		return true;
	}

	const char* senderName = LSMessageGetApplicationID(&message);
	if(senderName == NULL)
	{
	    senderName = LSMessageGetSenderServiceName(&message);
		if(senderName == NULL)
		{
			LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("typeOfDevice"))
	{
		std::string typeOfDevice = requestObj["typeOfDevice"].asString();
		if(typeOfDevice == "none")
			transportType = TransportType::BT_TRANSPORT_TYPE_NONE;
		else if(typeOfDevice == "bredr")
			transportType = TransportType::BT_TRANSPORT_TYPE_BR_EDR;
		else if(typeOfDevice == "ble")
			transportType = TransportType::BT_TRANSPORT_TYPE_LE;
		else
			transportType = TransportType::BT_TRANSPORT_TYPE_DUAL;
	}

	if (requestObj.hasKey("accessCode"))
	{
		std::string accessCode = requestObj["accessCode"].asString();
		if(accessCode == "none")
			inquiryAccessCode = InquiryAccessCode::BT_ACCESS_CODE_NONE;
		else if(accessCode == "liac")
			inquiryAccessCode = InquiryAccessCode::BT_ACCESS_CODE_LIMIT;
		else
			inquiryAccessCode = InquiryAccessCode::BT_ACCESS_CODE_GENERAL;
	}

	BluetoothError error = BLUETOOTH_ERROR_NONE;
	// Outgoing pairing performs in two steps, cancelDiscovery() and pair().
	// startDiscovery request in the middle of pairing must be ignored.
	if (!findAdapterInfo(adapterAddress)->getPairState().isPairing())
		error = findAdapterInfo(adapterAddress)->getAdapter()->startDiscovery(transportType, inquiryAccessCode);
	else
	{
		LSUtils::respondWithError(request, BT_ERR_PAIRING_IN_PROG);
		return true;
	}

	if (error != BLUETOOTH_ERROR_NONE)
	{
		LSUtils::respondWithError(request, BT_ERR_START_DISC_FAIL);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::startDiscovery(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, SCHEMA_1(PROP(adapterAddress, string)), &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->startDiscovery(request, requestObj);
}

bool BluetoothManagerService::cancelDiscovery(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, SCHEMA_1(PROP(adapterAddress, string)), &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->cancelDiscovery(request);
}

bool BluetoothManagerService::getLinkKey(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getLinkKey(request, requestObj);
}

bool BluetoothManagerService::startSniff(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(address, string), PROP(adapterAddress, string),
													PROP(minInterval, integer), PROP(maxInterval, integer),
													PROP(attempt, integer), PROP(timeout, integer))
													REQUIRED_5(address, minInterval, maxInterval, attempt, timeout));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	auto device = findAdapterInfo(adapterAddress)->findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	int minInterval = 0, maxInterval = 0, attempt = 0, timeout = 0;

	if (requestObj.hasKey("minInterval"))
		minInterval = requestObj["minInterval"].asNumber<int32_t>();

	if (requestObj.hasKey("maxInterval"))
		maxInterval = requestObj["maxInterval"].asNumber<int32_t>();

	if (requestObj.hasKey("attempt"))
		attempt = requestObj["attempt"].asNumber<int32_t>();

	if (requestObj.hasKey("timeout"))
		timeout = requestObj["timeout"].asNumber<int32_t>();

	BluetoothError error;
	pbnjson::JValue responseObj = pbnjson::Object();

	error = findAdapterInfo(adapterAddress)->getAdapter()->startSniff(address, minInterval, maxInterval, attempt, timeout);
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", true);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::stopSniff(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string))
													REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string address = requestObj["address"].asString();
	auto device = findAdapterInfo(adapterAddress)->findDevice(address);
	if (!device)
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return true;
	}

	BluetoothError error;
	pbnjson::JValue responseObj = pbnjson::Object();

	error = findAdapterInfo(adapterAddress)->getAdapter()->stopSniff(address);
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", true);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::getFilteringDeviceStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_4(PROP(subscribe, boolean), PROP(adapterAddress, string), PROP(classOfDevice, integer), PROP(uuid, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getFilteringDeviceStatus(request, requestObj);

}

bool BluetoothManagerService::getConnectedDevices(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_3(PROP(subscribe, boolean), PROP(adapterAddress, string), PROP(classOfDevice, integer)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getConnectedDevices(request, requestObj);
}

bool BluetoothManagerService::getPairedDevicesStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_2(PROP(subscribe, boolean), PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getPairedDevicesStatus(request, requestObj);
}

bool BluetoothManagerService::getDiscoveredDeviceStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_2(PROP(subscribe, boolean), PROP(adapterAddress, string)) REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getDiscoveredDeviceStatus(request, requestObj);
}

bool BluetoothManagerService::getDeviceStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_3(PROP(subscribe, boolean), PROP(adapterAddress, string), PROP(classOfDevice, integer)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->getDeviceStatus(request, requestObj);
}

bool BluetoothManagerService::setDeviceState(LSMessage &msg)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&msg);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_4(
                                    PROP(address, string), PROP(trusted, boolean), PROP(blocked, boolean), PROP(adapterAddress, string))
                                    REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->setDeviceState(request, requestObj);
}

bool BluetoothManagerService::pair(LSMessage &message)
{
	pbnjson::JValue requestObj;
	int parseError = 0;

	LS::Message request(&message);

	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string),
                                              PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress,string))
                                              REQUIRED_2(address,subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->pair(request, requestObj);
}

bool BluetoothManagerService::supplyPasskey(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(passkey, integer), PROP(adapterAddress, string))
                                              REQUIRED_2(address, passkey));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("passkey"))
			LSUtils::respondWithError(request, BT_ERR_PASSKEY_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->supplyPasskey(request, requestObj);
}

bool BluetoothManagerService::supplyPinCode(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(pin, string), PROP(adapterAddress, string))
                                             REQUIRED_2(address, pin));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("pin"))
			LSUtils::respondWithError(request, BT_ERR_PIN_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->supplyPinCode(request, requestObj);
}

bool BluetoothManagerService::supplyPasskeyConfirmation(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(accept, boolean), PROP(adapterAddress, string))
                                              REQUIRED_2(address, accept));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("accept"))
			LSUtils::respondWithError(request, BT_ERR_ACCEPT_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->supplyPasskeyConfirmation(request, requestObj);
}

bool BluetoothManagerService::cancelPairing(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->cancelPairing(request, requestObj);
}

bool BluetoothManagerService::unpair(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->unpair(request, requestObj);
}

bool BluetoothManagerService::awaitPairingRequests(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string)) REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);
	return adapter->awaitPairingRequests(request, requestObj);
}

bool BluetoothManagerService::setWoBle(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(woBleEnabled, boolean), PROP(adapterAddress, string) , PROP(suspend, boolean)) REQUIRED_2(woBleEnabled, suspend));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("woBleEnabled"))
			LSUtils::respondWithError(request, BT_ERR_WOBLE_SET_WOBLE_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothError error = BLUETOOTH_ERROR_NONE;
	bool woBleEnabled = false;
	bool suspend = false;

	if (requestObj.hasKey("suspend"))
		suspend = requestObj["suspend"].asBool();

	if (requestObj.hasKey("woBleEnabled"))
	{
		woBleEnabled = requestObj["woBleEnabled"].asBool();
		if (woBleEnabled)
			error = findAdapterInfo(adapterAddress)->getAdapter()->enableWoBle(suspend);
		else
			error = findAdapterInfo(adapterAddress)->getAdapter()->disableWoBle(suspend);
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", true);

		mWoBleEnabled = woBleEnabled;
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::setWoBleTriggerDevices(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(ARRAY(triggerDevices, string), PROP(adapterAddress, string)) REQUIRED_1(triggerDevices));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("triggerDevices"))
			LSUtils::respondWithError(request, BT_ERR_WOBLE_SET_WOBLE_TRIGGER_DEVICES_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothError error = BLUETOOTH_ERROR_NONE;
	BluetoothWoBleTriggerDeviceList triggerDevices;

	if (requestObj.hasKey("triggerDevices"))
	{
		auto tiggerDevicesObjArray = requestObj["triggerDevices"];
		for (int n = 0; n < tiggerDevicesObjArray.arraySize(); n++)
		{
			pbnjson::JValue element = tiggerDevicesObjArray[n];
			triggerDevices.push_back(element.asString());
		}

		error = findAdapterInfo(adapterAddress)->getAdapter()->setWoBleTriggerDevices(triggerDevices);
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", true);

		mWoBleTriggerDevices.clear();
		mWoBleTriggerDevices.assign(triggerDevices.begin(),triggerDevices.end());
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::getWoBleStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_1(PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("returnValue", true);
	responseObj.put("woBleEnabled", mWoBleEnabled);

	pbnjson::JValue triggerDevicesObj = pbnjson::Array();
	for (auto triggerDevice : mWoBleTriggerDevices)
	{
		triggerDevicesObj.append(triggerDevice);
	}
	responseObj.put("woBleTriggerDevices", triggerDevicesObj);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::sendHciCommand(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(ogf, integer), PROP(ocf, integer), ARRAY(parameters, integer)) REQUIRED_3(ogf, ocf, parameters));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	uint16_t ogf = 0;
	uint16_t ocf = 0;
	BluetoothHCIParameterList parameters;

	if(requestObj.hasKey("ogf"))
		ogf = requestObj["ogf"].asNumber<int32_t>();

	if(requestObj.hasKey("ocf"))
		ocf = requestObj["ocf"].asNumber<int32_t>();

	if (requestObj.hasKey("parameters"))
	{
		auto parametersObjArray = requestObj["parameters"];
		for (int n = 0; n < parametersObjArray.arraySize(); n++)
		{
			pbnjson::JValue element = parametersObjArray[n];
			parameters.push_back(element.asNumber<int32_t>());
		}
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto sendHciCommandCallback = [requestMessage, adapterAddress](BluetoothError error, uint16_t eventCode, BluetoothHCIParameterList parameters ) {
		LS::Message request(requestMessage);
		pbnjson::JValue responseObj = pbnjson::Object();
		if (error != BLUETOOTH_ERROR_NONE)
		{
			appendErrorResponse(responseObj, error);
			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
			return;
		}

		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("eventCode", (int32_t)eventCode);

		pbnjson::JValue parametersArray = pbnjson::Array();
		for (size_t i=0; i < parameters.size(); i++)
					parametersArray.append((int32_t) parameters[i]);

		responseObj.put("eventParameters", parametersArray);

		LSUtils::postToClient(request, responseObj);
		LSMessageUnref(requestMessage);

	};
	findAdapterInfo(adapterAddress)->getAdapter()->sendHciCommand(ogf, ocf, parameters, sendHciCommandCallback);
	return true;
}

bool BluetoothManagerService::setTrace(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_7(
									PROP(stackTraceEnabled, boolean), PROP(snoopTraceEnabled, boolean),
									PROP(stackTraceLevel, integer), PROP(isTraceLogOverwrite, boolean),
									PROP(stackLogPath, string), PROP(snoopLogPath, string),
									PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothError error;

	if (requestObj.hasKey("stackTraceLevel"))
	{
		int stackTraceLevel = requestObj["stackTraceLevel"].asNumber<int32_t>();
		error = findAdapterInfo(adapterAddress)->getAdapter()->setStackTraceLevel(stackTraceLevel);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_STACK_TRACE_LEVEL_CHANGE_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("stackLogPath"))
	{
		std::string stackLogPath = requestObj["stackLogPath"].asString();
		error = findAdapterInfo(adapterAddress)->getAdapter()->setLogPath(TraceType::BT_TRACE_TYPE_STACK, stackLogPath);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_STACK_LOG_PATH_CHANGE_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("snoopLogPath"))
	{
		std::string snoopLogPath = requestObj["snoopLogPath"].asString();
		error = findAdapterInfo(adapterAddress)->getAdapter()->setLogPath(TraceType::BT_TRACE_TYPE_SNOOP, snoopLogPath);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_SNOOP_LOG_PATH_CHANGE_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("isTraceLogOverwrite"))
	{
		bool isTraceLogOverwrite = requestObj["isTraceLogOverwrite"].asBool();
		error = findAdapterInfo(adapterAddress)->getAdapter()->setTraceOverwrite(isTraceLogOverwrite);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_SNOOP_TRACE_STATE_CHANGE_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("snoopTraceEnabled"))
	{
		bool snoopTraceEnabled = requestObj["snoopTraceEnabled"].asBool();
		if (snoopTraceEnabled)
			error = findAdapterInfo(adapterAddress)->getAdapter()->enableTrace(TraceType::BT_TRACE_TYPE_SNOOP);
		else
			error = findAdapterInfo(adapterAddress)->getAdapter()->disableTrace(TraceType::BT_TRACE_TYPE_SNOOP);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_SNOOP_TRACE_STATE_CHANGE_FAIL);
			return true;
		}
	}

	if (requestObj.hasKey("stackTraceEnabled"))
	{
		bool stackTraceEnabled = requestObj["stackTraceEnabled"].asBool();
		if (stackTraceEnabled)
			error = findAdapterInfo(adapterAddress)->getAdapter()->enableTrace(TraceType::BT_TRACE_TYPE_STACK);
		else
			error = findAdapterInfo(adapterAddress)->getAdapter()->disableTrace(TraceType::BT_TRACE_TYPE_STACK);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_STACK_TRACE_STATE_CHANGE_FAIL);
			return true;
		}
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("returnValue", true);


	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::getTraceStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_1(PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto getTraceStatusCallback  = [requestMessage, adapterAddress](BluetoothError error,
																bool stackTraceEnabled, bool snoopTraceEnabled,
																int stackTraceLevel,
																const std::string &stackLogPath,
																const std::string &snoopLogPath,
																bool  IsTraceLogOverwrite) {
			pbnjson::JValue responseObj = pbnjson::Object();
			if (error != BLUETOOTH_ERROR_NONE)
			{
				appendErrorResponse(responseObj, error);
				LSUtils::postToClient(requestMessage, responseObj);
				LSMessageUnref(requestMessage);
				return;
			}

			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("stackTraceEnabled", stackTraceEnabled);
			responseObj.put("snoopTraceEnabled", snoopTraceEnabled);
			responseObj.put("stackTraceLevel", stackTraceLevel);
			responseObj.put("stackLogPath", stackLogPath);
			responseObj.put("snoopLogPath", snoopLogPath);
			responseObj.put("IsTraceLogOverwrite", IsTraceLogOverwrite);

			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
	};
	findAdapterInfo(adapterAddress)->getAdapter()->getTraceStatus(getTraceStatusCallback);

	return true;
}

bool BluetoothManagerService::setKeepAlive(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(keepAliveEnabled, boolean), PROP(adapterAddress, string), PROP(keepAliveInterval, integer)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothError error = BLUETOOTH_ERROR_NONE;
	bool keepAliveEnabled = false;

	if (requestObj.hasKey("keepAliveInterval"))
	{
		int keepAliveInterval = requestObj["keepAliveInterval"].asNumber<int32_t>();
		error = findAdapterInfo(adapterAddress)->getAdapter()->setKeepAliveInterval(keepAliveInterval);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_KEEP_ALIVE_INTERVAL_CHANGE_FAIL);
			return true;
		}

		mKeepAliveInterval = (uint32_t)keepAliveInterval;
	}

	if (requestObj.hasKey("keepAliveEnabled"))
	{
		keepAliveEnabled = requestObj["keepAliveEnabled"].asBool();
		if (keepAliveEnabled != mKeepAliveEnabled)
		{
			if (keepAliveEnabled)
				error = findAdapterInfo(adapterAddress)->getAdapter()->enableKeepAlive();
			else
				error = findAdapterInfo(adapterAddress)->getAdapter()->disableKeepAlive();
		}
		else
			error = BLUETOOTH_ERROR_NONE;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", true);

		mKeepAliveEnabled = keepAliveEnabled;
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::getKeepAliveStatus(LSMessage &message)
{
	BT_INFO("MANAGER_SERVICE", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	bool subscribed = false;

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, SCHEMA_1(PROP(subscribe, boolean)), &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	pbnjson::JValue responseObj = pbnjson::Object();

	if (request.isSubscription())
	{
		mGetKeepAliveStatusSubscriptions.subscribe(request);
		subscribed = true;
	}

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", subscribed);
	responseObj.put("adapterAddress", mAddress);

	if (subscribed)
	{
		responseObj.put("keepAliveEnabled", mKeepAliveEnabled);
		responseObj.put("keepAliveInterval", (int32_t)mKeepAliveInterval);
	}


	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::notifyAdvertisingDisabled(uint8_t advertiserId)
{
	notifySubscribersAdvertisingChanged(mAddress);

	BT_DEBUG("Advertiser(%d) disabled", advertiserId);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("advertiserId", advertiserId);
	responseObj.put("subscribed", false);
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(mAdvertisingWatch->getMessage(), responseObj);

	return true;
}

bool BluetoothManagerService::notifyAdvertisingDropped(uint8_t advertiserId)
{
	BT_DEBUG("Advertiser(%d) dropped", advertiserId);

	std::string adapterAddress = mAdvIdAdapterMap[advertiserId];

	if (adapterAddress.empty())
		return true;

	auto leAdvEnableCallback = [this, advertiserId, adapterAddress](BluetoothError enableError)
	{
		auto unregisterAdvCallback = [this,adapterAddress, advertiserId](BluetoothError registerError)
		{
			pbnjson::JValue responseObj = pbnjson::Object();

			if (BLUETOOTH_ERROR_NONE == registerError)
			{
				notifySubscribersAdvertisingChanged(adapterAddress);
				responseObj.put("advertiserId", advertiserId);
			}
			else
			{
				appendErrorResponse(responseObj, registerError);
			}

			responseObj.put("adapterAddress", mAddress);
			responseObj.put("subscribed", false);
			responseObj.put("returnValue", true);
			LSUtils::postToClient(mAdvertisingWatch->getMessage(), responseObj);
			auto itr = mAdvIdAdapterMap.find(advertiserId);
			if (itr != mAdvIdAdapterMap.end())
				mAdvIdAdapterMap.erase(itr);
		};
		findAdapterInfo(adapterAddress)->getAdapter()->unregisterAdvertiser(advertiserId, unregisterAdvCallback);

		if (enableError != BLUETOOTH_ERROR_NONE)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("adapterAddress", adapterAddress);
			appendErrorResponse(responseObj, enableError);
			LSUtils::postToClient(mAdvertisingWatch->getMessage(), responseObj);
		}
	};

	findAdapterInfo(adapterAddress)->getAdapter()->disableAdvertiser(advertiserId, leAdvEnableCallback);
	return true;
}

bool BluetoothManagerService::getPowered(const std::string &address)
{
	return findAdapterInfo(address)->getPowerState();
}


bool BluetoothManagerService::configureAdvertisement(LSMessage &message)
{

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_9(PROP(adapterAddress, string), PROP(connectable, boolean), PROP(includeTxPower, boolean),
                                                     PROP(TxPower,integer), PROP(includeName, boolean), PROP(isScanResponse, boolean),
													 ARRAY(manufacturerData, integer),
                                                     OBJARRAY(services, OBJSCHEMA_2(PROP(uuid, string), ARRAY(data,integer))),
			                                         OBJARRAY(proprietaryData, OBJSCHEMA_2(PROP(type, integer), ARRAY(data, integer)))));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		return true;
	}

	bool connectable = true;
	bool includeTxPower = false;
	bool includeName = false;
	bool isScanResponse = false;
	uint8_t TxPower = 0x00;

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if(requestObj.hasKey("connectable"))
		connectable = requestObj["connectable"].asBool();
	else
		connectable = true;

	if(requestObj.hasKey("includeTxPower"))
		includeTxPower = requestObj["includeTxPower"].asBool();

	if(requestObj.hasKey("TxPower"))
		TxPower = (uint8_t) requestObj["TxPower"].asNumber<int32_t>();

	if(requestObj.hasKey("includeName"))
		includeName = requestObj["includeName"].asBool();

	if(requestObj.hasKey("isScanResponse"))
		isScanResponse = requestObj["isScanResponse"].asBool();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	if ((!requestObj.hasKey("manufacturerData") && !requestObj.hasKey("services") && !requestObj.hasKey("proprietaryData") && !isScanResponse))
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("returnValue", false);
		responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_CONFIG_DATA_PARAM_MISSING));
		responseObj.put("errorCode", BT_ERR_BLE_ADV_CONFIG_DATA_PARAM_MISSING);

		LSUtils::postToClient(request,responseObj);
		return true;
	}

	BluetoothLowEnergyServiceList serviceList;
	BluetoothLowEnergyData manufacturerData;
	ProprietaryDataList proprietaryDataList;
	bool serviceDataFound = false;

	if(requestObj.hasKey("services"))
	{
		auto servicesObjArray = requestObj["services"];
		for(int i = 0; i < servicesObjArray.arraySize(); i++)
		{
			auto serviceObj = servicesObjArray[i];
			if(serviceObj.hasKey("data") && !serviceDataFound)
			{
				auto serviceDataArray = serviceObj["data"];
				BluetoothLowEnergyData serviceData;

				for(int j = 0; j < serviceDataArray.arraySize(); j++)
				{
					pbnjson::JValue element = serviceDataArray[j];
					serviceData.push_back((uint8_t)element.asNumber<int32_t>());
				}

				if(serviceObj.hasKey("uuid"))
				{
					serviceList[serviceObj["uuid"].asString()] = serviceData;
					serviceDataFound = true;
				}
				else
				{
					pbnjson::JValue responseObj = pbnjson::Object();
					responseObj.put("adapterAddress", adapterAddress);
					responseObj.put("returnValue", false);
					responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_UUID_FAIL));
					responseObj.put("errorCode", BT_ERR_BLE_ADV_UUID_FAIL);

					LSUtils::postToClient(request,responseObj);
					return true;
				}
			}
			else if(serviceObj.hasKey("data") && serviceDataFound)
			{
				pbnjson::JValue responseObj = pbnjson::Object();
				responseObj.put("adapterAddress", adapterAddress);
				responseObj.put("returnValue", false);
				responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_SERVICE_DATA_FAIL));
				responseObj.put("errorCode", BT_ERR_BLE_ADV_SERVICE_DATA_FAIL);

				LSUtils::postToClient(request,responseObj);
				return true;
			}
			else
			{
				serviceList[serviceObj["uuid"].asString()];
			}
		}
	}

	if (requestObj.hasKey("manufacturerData"))
	{
		auto manufacturerDataArray = requestObj["manufacturerData"];
		for(int i = 0; i < manufacturerDataArray.arraySize(); i++)
		{
			pbnjson::JValue element = manufacturerDataArray[i];
			manufacturerData.push_back((uint8_t)element.asNumber<int32_t>());
		}
	}

	if (requestObj.hasKey("proprietaryData"))
	{
		auto proprietaryObjArray = requestObj["proprietaryData"];
		for(int i = 0; i < proprietaryObjArray.arraySize(); i++)
		{
			ProprietaryData proprietaryData;
			auto proprietaryObj = proprietaryObjArray[i];
			proprietaryData.type = (uint8_t)(proprietaryObj["type"].asNumber<int32_t>());

			auto proprietaryArray = proprietaryObj["data"];
			for(int j = 0; j < proprietaryArray.arraySize(); j++)
			{
				pbnjson::JValue element = proprietaryArray[j];
				proprietaryData.data.push_back((uint8_t)element.asNumber<int32_t>());
			}
			proprietaryDataList.push_back(proprietaryData);
		}
	}

	auto leConfigCallback = [requestMessage,adapterAddress](BluetoothError error) {
		pbnjson::JValue responseObj = pbnjson::Object();

		if (BLUETOOTH_ERROR_NONE == error)
		{
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("returnValue", true);
		}
		else
		{
			responseObj.put("adapterAddress", adapterAddress);
			appendErrorResponse(responseObj, error);
		}
		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);
	};
	findAdapterInfo(adapterAddress)->getAdapter()->configureAdvertisement(connectable, includeTxPower, includeName, isScanResponse,
	                                        manufacturerData, serviceList, proprietaryDataList, leConfigCallback, TxPower);
	return true;

}

bool BluetoothManagerService::setAdvertiseData(LSMessage &message, pbnjson::JValue &value, AdvertiseData &data, bool isScanRsp)
{
	LS::Message request(&message);
	AdvertiseData *advData = &data;

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothLowEnergyServiceList serviceList;
	auto advertiseObj = isScanRsp ? value["scanResponse"] : value["advertiseData"];
	if(advertiseObj.hasKey("services"))
	{
		bool serviceDataFound = false;
		auto servicesObjArray = advertiseObj["services"];
		for(int i = 0; i < servicesObjArray.arraySize(); i++)
		{
			auto serviceObj = servicesObjArray[i];
			if(serviceObj.hasKey("data") && !serviceDataFound)
			{
				auto serviceDataArray = serviceObj["data"];
				BluetoothLowEnergyData serviceData;
				for(int j = 0; j < serviceDataArray.arraySize(); j++)
				{
					pbnjson::JValue element = serviceDataArray[j];
					serviceData.push_back((uint8_t)element.asNumber<int32_t>());
				}
				if(serviceObj.hasKey("uuid"))
				{
					serviceList[serviceObj["uuid"].asString()] = serviceData;
					serviceDataFound = true;
				}
				else
				{
					pbnjson::JValue responseObj = pbnjson::Object();
					responseObj.put("adapterAddress", mAddress);
					responseObj.put("returnValue", false);
					responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_UUID_FAIL));
					responseObj.put("errorCode", BT_ERR_BLE_ADV_UUID_FAIL);

					LSUtils::postToClient(requestMessage,responseObj);
					LSMessageUnref(requestMessage);
					return false;
				}
			}
			else if(serviceObj.hasKey("data") && serviceDataFound)
			{
				pbnjson::JValue responseObj = pbnjson::Object();
				responseObj.put("adapterAddress", mAddress);
				responseObj.put("returnValue", false);
				responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_SERVICE_DATA_FAIL));
				responseObj.put("errorCode", BT_ERR_BLE_ADV_SERVICE_DATA_FAIL);

				LSUtils::postToClient(requestMessage,responseObj);
				LSMessageUnref(requestMessage);
				return false;
			}
			else
			{
				serviceList[serviceObj["uuid"].asString()];
			}
		}
		advData->services = serviceList;
	}

	if (advertiseObj.hasKey("manufacturerData"))
	{
		auto manufacturerDataArray = advertiseObj["manufacturerData"];
		for(int i = 0; i < manufacturerDataArray.arraySize(); i++)
		{
			pbnjson::JValue element = manufacturerDataArray[i];
			advData->manufacturerData.push_back((uint8_t)element.asNumber<int32_t>());
		}
	}

	if (advertiseObj.hasKey("proprietaryData"))
	{
		auto proprietaryObjArray = advertiseObj["proprietaryData"];
		for(int i = 0; i < proprietaryObjArray.arraySize(); i++)
		{
			ProprietaryData proprietaryData;
			auto proprietaryObj = proprietaryObjArray[i];
			proprietaryData.type = (uint8_t)(proprietaryObj["type"].asNumber<int32_t>());

			auto proprietaryArray = proprietaryObj["data"];
			for(int j = 0; j < proprietaryArray.arraySize(); j++)
			{
				pbnjson::JValue element = proprietaryArray[j];
				proprietaryData.data.push_back((uint8_t)element.asNumber<int32_t>());
			}
			advData->proprietaryData.push_back(proprietaryData);
		}
	}

	if (advertiseObj.hasKey("includeTxPower"))
	{
		advData->includeTxPower = advertiseObj["includeTxPower"].asBool();
	}

	if (advertiseObj.hasKey("includeName"))
	{
		if(advertiseObj["includeName"].asBool())
		{
			if(!isScanRsp)
			{
				pbnjson::JValue responseObj = pbnjson::Object();
				responseObj.put("adapterAddress", mAddress);
				responseObj.put("returnValue", false);
				responseObj.put("errorText",retrieveErrorText(BT_ERR_BLE_ADV_CONFIG_FAIL));
				responseObj.put("errorCode", BT_ERR_BLE_ADV_CONFIG_FAIL);

				LSUtils::postToClient(requestMessage,responseObj);
				LSMessageUnref(requestMessage);
			}
			else
			{
				advData->includeName = advertiseObj["includeName"].asBool();
			}
		}
		else
			advData->includeName = false;
	}
	return true;
}

bool BluetoothManagerService::startAdvertising(LSMessage &message)
{
	BT_DEBUG("BluetoothManagerService::%s %d \n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_5(PROP(adapterAddress, string), PROP(subscribe, boolean),
											  OBJECT(settings, OBJSCHEMA_5(PROP(connectable, boolean), PROP(txPower, integer),
													  PROP(minInterval, integer), PROP(maxInterval, integer), PROP(timeout, integer))),
											  OBJECT(advertiseData, OBJSCHEMA_5(PROP(includeTxPower, boolean), PROP(includeName, boolean),
													  ARRAY(manufacturerData, integer), OBJARRAY(services, OBJSCHEMA_2(PROP(uuid, string),ARRAY(data,integer))),
													  OBJARRAY(proprietaryData, OBJSCHEMA_2(PROP(type, integer), ARRAY(data, integer))))),
											  OBJECT(scanResponse, OBJSCHEMA_5(PROP(includeTxPower, boolean), PROP(includeName, boolean),
													  ARRAY(manufacturerData, integer), OBJARRAY(services, OBJSCHEMA_2(PROP(uuid, string),ARRAY(data,integer))),
													  OBJARRAY(proprietaryData, OBJSCHEMA_2(PROP(type, integer), ARRAY(data, integer)))))) REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	AdvertiserInfo advInfo{};
	//Assign default value true
	advInfo.settings.connectable = true;
	BT_DEBUG("BluetoothManagerService::%s %d advertiseData.includeTxPower:%d", __FUNCTION__, __LINE__, advInfo.advertiseData.includeTxPower);
	BT_DEBUG("BluetoothManagerService::%s %d scanResponse.includeTxPower:%d", __FUNCTION__, __LINE__, advInfo.scanResponse.includeTxPower);

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (requestObj.hasKey("settings"))
	{
		auto settingsObj = requestObj["settings"];
		if (settingsObj.hasKey("connectable"))
			advInfo.settings.connectable = settingsObj["connectable"].asBool();

		if (settingsObj.hasKey("minInterval"))
			advInfo.settings.minInterval = settingsObj["minInterval"].asNumber<int32_t>();

		if (settingsObj.hasKey("maxInterval"))
			advInfo.settings.maxInterval = settingsObj["maxInterval"].asNumber<int32_t>();

		if (settingsObj.hasKey("txPower"))
			advInfo.settings.txPower = settingsObj["txPower"].asNumber<int32_t>();

		if (settingsObj.hasKey("timeout"))
			advInfo.settings.timeout = settingsObj["timeout"].asNumber<int32_t>();
	}

	if(requestObj.hasKey("advertiseData"))
	{
		if(!setAdvertiseData(message, requestObj,advInfo.advertiseData, false))
			return true;
	}

	if(requestObj.hasKey("scanResponse"))
	{
		if(!setAdvertiseData(message, requestObj, advInfo.scanResponse, true))
			return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	if (requestObj.hasKey("settings") || requestObj.hasKey("advertiseData") || requestObj.hasKey("advertiseData"))
	{
		mAdvertisingWatch = new LSUtils::ClientWatch(get(), &message, nullptr);
		auto leRegisterAdvCallback = [this,requestMessage,advInfo,adapterAddress](BluetoothError error, uint8_t advertiserId) {

			pbnjson::JValue responseObj = pbnjson::Object();
			if (BLUETOOTH_ERROR_NONE == error)
			{
				auto leStartAdvCallback = [this,requestMessage,advertiserId,adapterAddress](BluetoothError error) {
					pbnjson::JValue responseObj = pbnjson::Object();

					if (BLUETOOTH_ERROR_NONE == error)
					{
						responseObj.put("adapterAddress", adapterAddress);
						responseObj.put("returnValue", true);
						responseObj.put("advertiserId", advertiserId);
						notifySubscribersAdvertisingChanged(adapterAddress);
						mAdvIdAdapterMap[advertiserId] = adapterAddress;
					}
					else
					{
						responseObj.put("adapterAddress", adapterAddress);
						appendErrorResponse(responseObj, error);
					}
					LSUtils::postToClient(requestMessage, responseObj);
					LSMessageUnref(requestMessage);
				};

				LS::Message request(requestMessage);
				if(request.isSubscription())
					mAdvertisingWatch->setCallback(std::bind(&BluetoothManagerService::notifyAdvertisingDropped, this, advertiserId));

				findAdapterInfo(adapterAddress)->getAdapter()->startAdvertising(advertiserId, advInfo.settings, advInfo.advertiseData, advInfo.scanResponse, leStartAdvCallback);
			}
			else
			{
				responseObj.put("adapterAddress", adapterAddress);
				appendErrorResponse(responseObj, error);
				LSUtils::postToClient(requestMessage, responseObj);
				LSMessageUnref(requestMessage);
			}
		};

		if (getAdvSize(advInfo.advertiseData, true) > MAX_ADVERTISING_DATA_BYTES ||
						getAdvSize(advInfo.scanResponse, false) > MAX_ADVERTISING_DATA_BYTES)
		{
			LSUtils::respondWithError(request, BT_ERR_BLE_ADV_EXCEED_SIZE_LIMIT);
			return true;
		}

		findAdapterInfo(adapterAddress)->getAdapter()->registerAdvertiser(leRegisterAdvCallback);
	}
	else
	{
		auto leStartAdvCallback = [this,requestMessage,adapterAddress](BluetoothError error) {

			pbnjson::JValue responseObj = pbnjson::Object();

			if (BLUETOOTH_ERROR_NONE == error)
			{
				responseObj.put("adapterAddress", adapterAddress);
				responseObj.put("returnValue", true);
				mAdvertising = true;

				notifySubscribersAdvertisingChanged(adapterAddress);
			}
			else
			{
				responseObj.put("adapterAddress", adapterAddress);
				appendErrorResponse(responseObj, error);
			}

			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
		};

		findAdapterInfo(adapterAddress)->getAdapter()->startAdvertising(leStartAdvCallback);
	}

	return true;

}

bool BluetoothManagerService::disableAdvertising(LSMessage &message)
{
	BT_DEBUG("BluetoothManagerService::%s %d \n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(advertiserId, integer)) REQUIRED_1(advertiserId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("advertiserId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_ADVERTISERID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	uint8_t advertiserId = (uint8_t)requestObj["advertiserId"].asNumber<int32_t>();

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto leAdvEnableCallback = [this, advertiserId, adapterAddress](BluetoothError error)
	{
		auto unregisterAdvCallback = [this,adapterAddress, advertiserId](BluetoothError error)
		{
			pbnjson::JValue responseObj = pbnjson::Object();

			if (BLUETOOTH_ERROR_NONE == error)
			{
				notifySubscribersAdvertisingChanged(adapterAddress);
				responseObj.put("advertiserId", advertiserId);
			}
			else
			{
				appendErrorResponse(responseObj, error);
			}

			responseObj.put("adapterAddress", mAddress);
			responseObj.put("subscribed", false);
			responseObj.put("returnValue", true);
			LSUtils::postToClient(mAdvertisingWatch->getMessage(), responseObj);
			auto itr = mAdvIdAdapterMap.find(advertiserId);
			if (itr != mAdvIdAdapterMap.end())
				mAdvIdAdapterMap.erase(itr);
		};
		findAdapterInfo(adapterAddress)->getAdapter()->unregisterAdvertiser(advertiserId, unregisterAdvCallback);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("adapterAddress", adapterAddress);
			appendErrorResponse(responseObj, error);
			LSUtils::postToClient(mAdvertisingWatch->getMessage(), responseObj);
		}
	};

	findAdapterInfo(adapterAddress)->getAdapter()->disableAdvertiser(advertiserId, leAdvEnableCallback);

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("advertiserId", advertiserId);
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::updateAdvertising(LSMessage &message)
{
	BT_DEBUG("BluetoothManagerService::%s %d \n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_5(PROP(adapterAddress, string), PROP(advertiserId, integer),
											  OBJECT(settings, OBJSCHEMA_5(PROP(connectable, boolean), PROP(txPower, integer),
													  PROP(minInterval, integer), PROP(maxInterval, integer), PROP(timeout, integer))),
											  OBJECT(advertiseData, OBJSCHEMA_5(PROP(includeTxPower, boolean), PROP(includeName, boolean),
													  ARRAY(manufacturerData, integer), OBJARRAY(services, OBJSCHEMA_2(PROP(uuid, string),ARRAY(data,integer))),
													  OBJARRAY(proprietaryData, OBJSCHEMA_2(PROP(type, integer), ARRAY(data, integer))))),
											  OBJECT(scanResponse, OBJSCHEMA_5(PROP(includeTxPower, boolean), PROP(includeName, boolean),
													  ARRAY(manufacturerData, integer), OBJARRAY(services, OBJSCHEMA_2(PROP(uuid, string),ARRAY(data,integer))),
													  OBJARRAY(proprietaryData, OBJSCHEMA_2(PROP(type, integer), ARRAY(data, integer)))))) REQUIRED_1(advertiserId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	AdvertiserInfo advInfo{};
	BT_DEBUG("BluetoothManagerService::%s %d advertiseData.includeTxPower:%d", __FUNCTION__, __LINE__, advInfo.advertiseData.includeTxPower);
	BT_DEBUG("BluetoothManagerService::%s %d scanResponse.includeTxPower:%d", __FUNCTION__, __LINE__, advInfo.scanResponse.includeTxPower);

	uint8_t advertiserId = (uint8_t)requestObj["advertiserId"].asNumber<int32_t>();

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (requestObj.hasKey("settings"))
	{
		auto settingsObj = requestObj["settings"];
		if (settingsObj.hasKey("connectable"))
			advInfo.settings.connectable = settingsObj["connectable"].asBool();

		if (settingsObj.hasKey("minInterval"))
			advInfo.settings.minInterval = settingsObj["minInterval"].asNumber<int32_t>();

		if (settingsObj.hasKey("maxInterval"))
			advInfo.settings.maxInterval = settingsObj["maxInterval"].asNumber<int32_t>();

		if (settingsObj.hasKey("txPower"))
			advInfo.settings.txPower = settingsObj["txPower"].asNumber<int32_t>();

		if (settingsObj.hasKey("timeout"))
			advInfo.settings.timeout = settingsObj["timeout"].asNumber<int32_t>();
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto leUpdateAdvCallback = [requestMessage,adapterAddress](BluetoothError error) {
		if (BLUETOOTH_ERROR_NONE != error)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("adapterAddress", adapterAddress);
			appendErrorResponse(responseObj, error);
			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
		}
	};

	if(requestObj.hasKey("advertiseData"))
	{
		if(!setAdvertiseData(message, requestObj,advInfo.advertiseData, false))
			return true;

		findAdapterInfo(adapterAddress)->getAdapter()->setAdvertiserData(advertiserId, false, advInfo.advertiseData, leUpdateAdvCallback);
	}

	if(requestObj.hasKey("scanResponse"))
	{
		if(!setAdvertiseData(message, requestObj, advInfo.scanResponse, true))
			return true;

		findAdapterInfo(adapterAddress)->getAdapter()->setAdvertiserData(advertiserId, true, advInfo.scanResponse, leUpdateAdvCallback);
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("advertiserId", advertiserId);
	responseObj.put("adapterAddress", mAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

void BluetoothManagerService::updateAdvertiserData(LSMessage *requestMessage, uint8_t advertiserId, AdvertiserInfo &advInfo,
		bool isSettingsChanged, bool isAdvDataChanged, bool isScanRspChanged)
{
	if(isSettingsChanged)
	{
		auto leAdvSettingCallback = [this, requestMessage,advInfo](BluetoothError error)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			if (BLUETOOTH_ERROR_NONE == error)
			{
				notifySubscribersAdvertisingChanged(mAddress);
			}
			else
			{
				responseObj.put("adapterAddress", mAddress);
				appendErrorResponse(responseObj, error);
				LSUtils::postToClient(requestMessage, responseObj);
				LSMessageUnref(requestMessage);
			}
		};
		//TODO remove default adapter usage
		mDefaultAdapter->setAdvertiserParameters(advertiserId, advInfo.settings, leAdvSettingCallback);
	}

	if(isAdvDataChanged)
	{
		auto leAdvDataChangedCallback = [this, requestMessage,advInfo](BluetoothError error)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			if (BLUETOOTH_ERROR_NONE == error)
			{
				notifySubscribersAdvertisingChanged(mAddress);
			}
			else
			{
				responseObj.put("adapterAddress", mAddress);
				appendErrorResponse(responseObj, error);
				LSUtils::postToClient(requestMessage, responseObj);
				LSMessageUnref(requestMessage);
			}
		};

		//TODO remove default adapter usage
		mDefaultAdapter->setAdvertiserData(advertiserId, false, advInfo.advertiseData, leAdvDataChangedCallback);
	}

	if(isScanRspChanged)
	{
		auto leScanRspChangedCallback = [this,requestMessage,advInfo](BluetoothError error)
		{
			pbnjson::JValue responseObj = pbnjson::Object();
			if (BLUETOOTH_ERROR_NONE == error)
			{
				notifySubscribersAdvertisingChanged(mAddress);
			}
			else
			{
				responseObj.put("adapterAddress", mAddress);
				appendErrorResponse(responseObj, error);
				LSUtils::postToClient(requestMessage, responseObj);
				LSMessageUnref(requestMessage);
			}
		};

		//TODO remove default adapter usage
		mDefaultAdapter->setAdvertiserData(advertiserId, true, advInfo.scanResponse, leScanRspChangedCallback);
	}
}

bool BluetoothManagerService::stopAdvertising(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_1(PROP(adapterAddress, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto leStopAdvCallback = [this,requestMessage,adapterAddress](BluetoothError error) {

		pbnjson::JValue responseObj = pbnjson::Object();

		if (BLUETOOTH_ERROR_NONE == error)
		{
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("returnValue", true);
			mAdvertising = false;

			notifySubscribersAdvertisingChanged(adapterAddress);
		}
		else
		{
			responseObj.put("adapterAddress", adapterAddress);
			appendErrorResponse(responseObj, error);
		}

		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);
	};

	findAdapterInfo(adapterAddress)->getAdapter()->stopAdvertising(leStopAdvCallback);
	return true;
}

bool BluetoothManagerService::getAdvStatus(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string),PROP(subscribe,boolean)));

	if(!LSUtils::parsePayload(request.getPayload(),requestObj,schema,&parseError))
	{
		if(parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request,BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	pbnjson::JValue responseObj = pbnjson::Object();

	if (request.isSubscription())
	{
		mGetAdvStatusSubscriptions.subscribe(request);
		responseObj.put("subscribed", true);
	}

	responseObj.put("adapterAddress",adapterAddress);
	responseObj.put("advertising", mAdvertising);
	responseObj.put("returnValue", true);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothManagerService::startScan(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_7(PROP(address, string), PROP(name, string),
													PROP(subscribe, boolean), PROP(adapterAddress, string),
													OBJECT(serviceUuid, OBJSCHEMA_2(PROP(uuid, string), PROP(mask, string))),
													OBJECT(serviceData, OBJSCHEMA_3(PROP(uuid, string), ARRAY(data, integer), ARRAY(mask, integer))),
													OBJECT(manufacturerData, OBJSCHEMA_3(PROP(id, integer), ARRAY(data, integer), ARRAY(mask, integer)))) REQUIRED_1(subscribe));

	if(!LSUtils::parsePayload(request.getPayload(),requestObj,schema,&parseError))
	{
		if(parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else
			LSUtils::respondWithError(request,BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = findAdapterInfo(adapterAddress);

	return adapter->startScan(request, requestObj);
}

void BluetoothManagerService::leConnectionRequest(const std::string &address, bool state)
{
	for (auto profile : mProfiles)
	{
		if (profile->getName() == "GATT")
		{
			auto gattProfile = dynamic_cast<BluetoothGattProfileService *>(profile);
			if (gattProfile)
				gattProfile->incomingLeConnectionRequest(address, state);
		}
	}
}
// vim: noai:ts=4:sw=4:ss=4:expandtab
