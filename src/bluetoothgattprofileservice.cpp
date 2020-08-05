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


#include "bluetoothgattprofileservice.h"
#include "bluetoothgattancsprofile.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothdevice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "logging.h"
#include "utils.h"

using namespace std::placeholders;

BluetoothGattProfileService::BluetoothGattProfileService(BluetoothManagerService *manager) :
	BluetoothProfileService(manager, "GATT", "00001801-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, connect)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, disconnect)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, openServer)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, closeServer)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, discoverServices)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, addService)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, removeService)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, getServices)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, writeCharacteristicValue)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, readCharacteristicValue)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, readCharacteristicValues)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, monitorCharacteristic)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, monitorCharacteristics)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, readDescriptorValue)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, readDescriptorValues)
		LS_CATEGORY_CLASS_METHOD(BluetoothGattProfileService, writeDescriptorValue)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/gatt", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/gatt", this);

	BT_DEBUG("Gatt Service Created");
}

uint16_t idToInt(const std::string input)
{
	if(input.empty())
		return 0;

	std::string::size_type sz;   // alias of size_t
	return (uint16_t)(std::stoi(input,&sz));
}

std::string idToString(const uint16_t input)
{
	std::string ret = "";
	if(input < 10)
		ret += "00";
	else if(input < 100)
		ret += "0";

	ret += std::to_string(input);
	return ret;
}

BluetoothGattProfileService::~BluetoothGattProfileService()
{
}

BluetoothGattProfileService::BluetoothGattProfileService(BluetoothManagerService *manager, const std::string &name, const std::string &uuid):
		BluetoothProfileService(manager, name, uuid)
{
	//Constructor to override ls registration when Gatt sub Service class is instantiated.
}

void BluetoothGattProfileService::initialize()
{
	if (mImpl)
	{
		return;
	}
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothGattProfile>()->registerObserver(this);

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->initialize(mImpl);
	}
}

void BluetoothGattProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	BluetoothProfile *impl = findImpl(adapterAddress);

	if (impl)
		getImpl<BluetoothGattProfile>(adapterAddress)->registerObserver(this);

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->initialize(impl);
	}
}

void BluetoothGattProfileService::registerGattStatusObserver(BluetoothGattProfileService *statusObserver)
{
	mGattObservers.push_back(statusObserver);
}

bool BluetoothGattProfileService::isDevicePaired(const std::string &address)
{
	auto device = getManager()->findDevice(address);
	if (!device)
		return false;

	if (device->getType() == BLUETOOTH_DEVICE_TYPE_BLE || device->getType() == BLUETOOTH_DEVICE_TYPE_DUAL)
		return true;

	BT_INFO("BLE", 0, "address %s paired\n", address.c_str());
	return BluetoothProfileService::isDevicePaired(address);
}

void BluetoothGattProfileService::notifyStatusSubscribers(
		const std::string &adapterAddr, const std::string &devAddress, bool connected)
{
	BT_INFO("GATT", 0, "%s is called", __FUNCTION__);

	std::string adapterAddress = convertToLower(adapterAddr);
	std::string address = convertToLower(devAddress);

	if (!connected)
		removeConnectWatchForDevice(adapterAddress, address, !connected, true);

	auto subscriptionsIter = mGetStatusSubsMap.find(adapterAddress);
	if (subscriptionsIter == mGetStatusSubsMap.end())
		return;

	auto subscriptionIter = (subscriptionsIter->second).find(address);
	if (subscriptionIter == (subscriptionsIter->second).end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second.first;
	GattStatusSubsInfo* statusPtr = subscriptionIter->second.second;

	bool connecting = isDeviceConnecting(adapterAddress, address);
	bool discServ = mDiscoveringServices[address];
	if (statusPtr->isChanged(adapterAddress, address, connecting, connected, discServ))
	{
		pbnjson::JValue responseObj = buildGetStatusResp(connected, connecting,
							true, true, adapterAddress, address);
		LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
	}
}

pbnjson::JValue BluetoothGattProfileService::buildGetStatusResp(bool connected,
		bool connecting, bool subscribed, bool returnValue,
                std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	appendCommonProfileStatus(responseObj, connected, connecting, subscribed,
	                          returnValue, adapterAddress, deviceAddress);
	responseObj.put("discoveringServices", mDiscoveringServices[deviceAddress]);

	return responseObj;
}

void BluetoothGattProfileService::removeConnectWatchForDevice(const std::string &adapterAddress,
		const std::string &key,	bool disconnected, bool remoteDisconnect)
{
	auto watchesIter = mConnectSubsMap.find(adapterAddress);
	if (watchesIter == mConnectSubsMap.end())
		return;
	auto watchIter = (watchesIter->second).find(key);
	if (watchIter == (watchesIter->second).end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = watchIter->second.first;
	GattConnSubsInfo *connPtr = watchIter->second.second;
	if (connPtr->isChanged(adapterAddress, key, remoteDisconnect))
	{
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("returnValue", true);
		responseObj.put("adapaterAddress", adapterAddress);
		responseObj.put("address", key);
		responseObj.put("subscribed", true);
		responseObj.put("disconnectByRemote", remoteDisconnect);

		LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
	}
}

void BluetoothGattProfileService::serviceFound(const std::string &address, const BluetoothGattService &service)
{
	bool localAdapterChanged = false;
	std::string adapterAddress;
	std::string deviceAddress;

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->serviceFound(address, service);
		BT_INFO("BLE", 0, "address:%s service:%s Found\n", address.c_str(), service.getUuid().toString().c_str());
	}

	BluetoothGattServiceList serviceList;
	serviceList.push_back(service);
	if(getManager()->isAdapterAvailable(address))
	{
		localAdapterChanged = true;
		adapterAddress = address;
		deviceAddress = "";
	}
	else
	{
		localAdapterChanged = false;
		adapterAddress = getManager()->getAddress();  //choose default adapter
		deviceAddress = address;
	}

	notifyGetServicesSubscribers(localAdapterChanged, adapterAddress, deviceAddress, serviceList);

	for (auto iter : mGetStatusSubsMap)
	{
		if (iter.second.find(address) != iter.second.end())
		{
			notifyStatusSubscribers(iter.first, address, true);
			break;
		}
	}
}

void BluetoothGattProfileService::serviceLost(const std::string &address, const BluetoothGattService &service)
{
	//TODO: notify getServices subscriptions

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->serviceLost(address, service);
		BT_INFO("BLE", 0, "address:%s service:%s Lost\n", address.c_str(), service.getUuid().toString().c_str());
	}

	for (auto iter : mConnectSubsMap)
	{
		if (iter.second.find(address) != iter.second.end())
		{
			notifyStatusSubscribers(iter.first, address, false);
			break;
		}
	}
}

void BluetoothGattProfileService::characteristicValueChanged(const std::string &address, const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic)
{
	BT_INFO("BLE", 0, "characteristic value changed for device %s, service %s, characteristics %s", address.c_str(), service.toString().c_str(), characteristic.getUuid().toString().c_str());

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->characteristicValueChanged(address, service, characteristic);
	}
	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		auto subscriptionValue = it->second;

		if ((subscriptionValue.deviceAddress != address) || (subscriptionValue.serviceUuid != service))
			continue;

		bool foundCharacteristic = false;
		if (subscriptionValue.characteristicUuids.size() > 0)
		{
			for (auto it2 = subscriptionValue.characteristicUuids.begin(); it2 != subscriptionValue.characteristicUuids.end(); ++it2)
			{
				if ((*it2) == characteristic.getUuid())
				{
					foundCharacteristic = true;
					break;
				}
			}
		}
		else if (subscriptionValue.characteristicUuid == characteristic.getUuid())
		{
			foundCharacteristic = true;
		}

		if (!foundCharacteristic)
			continue;

		auto monitorCharacteristicsWatch = it->first;
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("subscribed", true);
		responseObj.put("adapterAddress", getManager()->getAddress());
		if(address != "")
			responseObj.put("address", address);

		pbnjson::JValue characteristicObj = pbnjson::Object();
		characteristicObj.put("characteristic", characteristic.getUuid().toString());
		pbnjson::JValue valueObj = pbnjson::Object();
		BluetoothGattValue values = characteristic.getValue();
		pbnjson::JValue bytesArray = pbnjson::Array();
		for (size_t i=0; i < values.size(); i++)
			bytesArray.append((int32_t) values[i]);
		valueObj.put("bytes", bytesArray);

		characteristicObj.put("value", valueObj);
		responseObj.put("changed", characteristicObj);

		LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);
	}
}

void BluetoothGattProfileService::characteristicValueChanged(const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic)
{
	BT_INFO("BLE", 0, "characteristic value changed for local adapter with service %s, characteristics %s", service.toString().c_str(), characteristic.getUuid().toString().c_str());

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->characteristicValueChanged(service, characteristic);
	}

	auto localService = findLocalService(service);
	if (localService)
	{
		localService->desc.updateCharacteristicValue(characteristic.getUuid(), characteristic.getValue());
		auto descList = characteristic.getDescriptors();
		for (auto it = descList.begin(); it != descList.end(); it++)
		{
			localService->desc.updateDescriptorValue(characteristic.getUuid(), it->getUuid(), it->getValue());
		}
	}

	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		auto subscriptionValue = it->second;

		if (subscriptionValue.serviceUuid != service)
			continue;

		bool foundCharacteristic = false;
		if (subscriptionValue.characteristicUuids.size() > 0)
		{
			for (auto it2 = subscriptionValue.characteristicUuids.begin(); it2 != subscriptionValue.characteristicUuids.end(); ++it2)
			{
				if ((*it2) == characteristic.getUuid())
				{
					foundCharacteristic = true;
					break;
				}
			}
		}
		else if (subscriptionValue.characteristicUuid == characteristic.getUuid())
			foundCharacteristic = true;

		if (!foundCharacteristic)
			continue;

		auto monitorCharacteristicsWatch = it->first;
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("subscribed", true);
		responseObj.put("adapterAddress", getManager()->getAddress());

		pbnjson::JValue characteristicObj = pbnjson::Object();
		characteristicObj.put("characteristic", characteristic.getUuid().toString());
		pbnjson::JValue valueObj = pbnjson::Object();
		BluetoothGattValue values = characteristic.getValue();
		pbnjson::JValue bytesArray = pbnjson::Array();
		for (size_t i=0; i < values.size(); i++)
			bytesArray.append((int32_t) values[i]);
		valueObj.put("bytes", bytesArray);

		characteristicObj.put("value", valueObj);
		responseObj.put("changed", characteristicObj);

		LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);
	}

}

void BluetoothGattProfileService::descriptorValueChanged(const BluetoothUuid &service, const BluetoothUuid &characteristic, BluetoothGattDescriptor &descriptor)
{
	BT_INFO("BLE", 0, "descriptor value changed for local adapter with service %s, characteristics %s", service.toString().c_str(), characteristic.toString().c_str());

	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->descriptorValueChanged(service, characteristic, descriptor);
	}

	auto localService = findLocalService(service);
	if (localService)
	{
		localService->desc.updateDescriptorValue(characteristic, descriptor.getUuid(), descriptor.getValue());
	}
}

void BluetoothGattProfileService::incomingLeConnectionRequest(const std::string &address, bool state)
{
	BT_INFO("BLE", 0, "incomingLeConnectionRequest device %s\n", address.c_str());
	for (auto obsIter = mGattObservers.begin(); obsIter != mGattObservers.end(); obsIter++)
	{
		(*obsIter)->incomingLeConnectionRequest(address, state);
	}
}

bool BluetoothGattProfileService::discoverServices(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(address, string)));

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
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string address;
	bool remoteServiceDiscovery = false;
	if (requestObj.hasKey("address"))
	{
		remoteServiceDiscovery = true;
		address = convertToLower(requestObj["address"].asString());
		auto iter = mConnectedDevicesMap.find(adapterAddress);
		if ((iter == mConnectedDevicesMap.end()) ||
			(iter->second.find(address) == iter->second.end()))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
		if (!isDeviceConnected(adapterAddress, address))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto discoverServicesCallback  = [this, requestMessage, remoteServiceDiscovery, adapterAddress, address](BluetoothError error) {
		BT_INFO("BLE", 0, "Service discovery process finished for device %s", address.c_str());

		if (mDiscoveringServices[address])
		{
			mDiscoveringServices[address] = false;
			notifyStatusSubscribers(adapterAddress, address, isDeviceConnected(adapterAddress, address));
		}

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_SERVICE_DISCOVERY_FAIL);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		if (remoteServiceDiscovery)
			responseObj.put("address", address);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	if (remoteServiceDiscovery)
	{
		auto discoveringServicesIter = mDiscoveringServices.find(address);
		if (discoveringServicesIter == mDiscoveringServices.end())
			mDiscoveringServices.insert(std::pair<std::string, bool>(address, true));
		else
			mDiscoveringServices[address] = true;

		notifyStatusSubscribers(getManager()->getAddress(), address, isDeviceConnected(adapterAddress, address));
		BT_DEBUG("getImpl->discoverServices\n");
		getImpl<BluetoothGattProfile>(adapterAddress)->discoverServices(address, discoverServicesCallback);
	}
	else
	{
		BT_DEBUG("getImpl->discoverServices\n");
		getImpl<BluetoothGattProfile>(adapterAddress)->discoverServices(discoverServicesCallback);
	}

	return true;
}

BluetoothGattService::Type serviceTypeStringToType(const std::string str)
{
	BluetoothGattService::Type type = BluetoothGattService::Type::UNKNOWN;

	if (str == "primary")
		return BluetoothGattService::Type::PRIMARY;

	if (str == "secondary")
		return BluetoothGattService::Type::SECONDARY;

	return type;
}

bool BluetoothGattProfileService::parseValue(pbnjson::JValue valueObj, BluetoothGattValue *value)
{
	if (valueObj.hasKey("bytes"))
	{
		auto bytesObjArray = valueObj["bytes"];
		for (int j = 0; j < bytesObjArray.arraySize(); j++)
		{
			value->push_back(bytesObjArray[j].asNumber<int32_t>());
		}
	}
	else if (valueObj.hasKey("string"))
	{
		std::string valueString = valueObj["string"].asString();
		for (char &valueStringChar : valueString)
		{
			value->push_back((uint8_t)valueStringChar);
		}
	}
	else if (valueObj.hasKey("number"))
	{
		int valueNumber = valueObj["number"].asNumber<int32_t>();
		value->push_back((uint8_t) (valueNumber & 0xFF));
		value->push_back((uint8_t) ((valueNumber >> 8) & 0xFF));
		value->push_back((uint8_t) ((valueNumber >> 16) & 0xFF));
		value->push_back((uint8_t) ((valueNumber >> 24) & 0xFF));
	}
	else
	{
		return false;
	}

	return true;
}

bool BluetoothGattProfileService::addService(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(serverId, string), PROP(service, string), PROP(type, string), ARRAY(includes, string),
	                                         OBJARRAY(characteristics, OBJSCHEMA_5(PROP(characteristic, string),
	                                                  OBJECT(value, OBJSCHEMA_3(PROP(value,string), PROP(number, integer), ARRAY(bytes, integer))),
	                                                  OBJECT(properties, OBJSCHEMA_8(PROP(broadcast, boolean), PROP(read, boolean),
	                                                         PROP(writeWithoutResponse, boolean), PROP(write, boolean), PROP(notify, boolean),
	                                                         PROP(indicate, boolean), PROP(authenticatedSignedWrites, boolean), PROP(extendedProperties, boolean))),
	                                                  OBJECT(permissions, OBJSCHEMA_8(PROP(read, boolean), PROP(readEncrypted, boolean),
	                                                         PROP(readEncryptedMitm, boolean), PROP(write, boolean), PROP(writeEncrypted, boolean),
	                                                         PROP(writeEncryptedMitm, boolean), PROP(writeSigned, boolean), PROP(writeSignedMitm, boolean))),
	                                                         OBJARRAY(descriptors, OBJSCHEMA_3(
	                                                                      PROP(descriptor, string),
	                                                                      OBJECT(value, OBJSCHEMA_1(ARRAY(bytes, integer))),
	                                                                      OBJECT(permissions, OBJSCHEMA_8(PROP(read, boolean), PROP(readEncrypted, boolean),
						                                                         PROP(readEncryptedMitm, boolean), PROP(write, boolean), PROP(writeEncrypted, boolean),
						                                                         PROP(writeEncryptedMitm, boolean), PROP(writeSigned, boolean), PROP(writeSignedMitm, boolean)))
	                                                         )))))
	                                         REQUIRED_4(service, type, includes, characteristics));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	// TODO: Change all inputs to lowercase

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string serviceUuid = requestObj["service"].asString();
	std::string type = requestObj["type"].asString();

	BT_DEBUG("%s: serviceUuid %s type %s", __func__, serviceUuid.c_str(), type.c_str());

	// TODO: Alloc
	BluetoothGattService gattService;
	gattService.setUuid(BluetoothUuid(serviceUuid));
	gattService.setType(serviceTypeStringToType(type));

	auto includesObjArray = requestObj["includes"];
	for (int n = 0; n < includesObjArray.arraySize(); n++)
	{
		pbnjson::JValue element = includesObjArray[n];
		gattService.includeService(element.asString());
	}

	auto characteristicsObjArray = requestObj["characteristics"];
	for (int i = 0; i < characteristicsObjArray.arraySize(); i++)
	{
		BluetoothGattCharacteristic characteristic;

		auto characteristicsObj = characteristicsObjArray[i];
		std::string characteristicUuid = characteristicsObj["characteristic"].asString();
		BT_DEBUG("%s: characteristicUuid is %s for characteristic loop %d", __func__, characteristicUuid.c_str(), i);
		characteristic.setUuid(characteristicUuid);

		auto valueObj = characteristicsObj["value"];
		BluetoothGattValue value;

		if (!parseValue(valueObj, &value))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTC_INVALID_VALUE_PARAM);
			return true;
		}
		characteristic.setValue(value);

		auto propertiesObj = characteristicsObj["properties"];
		BluetoothGattCharacteristicProperties properties = ((propertiesObj["broadcast"].asBool() << 0) |
		                                                    (propertiesObj["read"].asBool() << 1) |
		                                                    (propertiesObj["writeWithoutResponse"].asBool() << 2) |
		                                                    (propertiesObj["write"].asBool() << 3) |
		                                                    (propertiesObj["notify"].asBool() << 4) |
		                                                    (propertiesObj["indicate"].asBool() << 5) |
		                                                    (propertiesObj["authenticatedSignedWrites"].asBool() << 6) |
		                                                    (propertiesObj["extendedProperties"].asBool() << 7) );

		BT_DEBUG("%s: properties are %d for characteristic loop %d", __func__, properties, i);
		characteristic.setProperties(properties);

		auto permissionsObj = characteristicsObj["permissions"];
		BluetoothGattCharacteristicPermissions permissions = ((permissionsObj["read"].asBool() << 0) |
		                                                      (permissionsObj["readEncrypted"].asBool() << 1) |
		                                                      (permissionsObj["readEncryptedMitm"].asBool() << 2) |
		                                                      (permissionsObj["write"].asBool() << 3) |
		                                                      (permissionsObj["writeEncrypted"].asBool() << 4) |
		                                                      (permissionsObj["writeEncryptedMitm"].asBool() << 5) |
		                                                      (permissionsObj["writeSigned"].asBool() << 6) |
		                                                      (permissionsObj["writeSignedMitm"].asBool() << 7) );

		BT_DEBUG("%s: permissions are %d for characteristic loop %d", __func__, permissions, i);
		characteristic.setPermissions(permissions);

		auto descriptorsObjArray = characteristicsObj["descriptors"];
		for (int k = 0; k < descriptorsObjArray.arraySize(); k++)
		{
			BluetoothGattDescriptor descriptor;
			auto descriptorObj = descriptorsObjArray[k];
			std::string descriptorUuid = descriptorObj["descriptor"].asString();
			descriptor.setUuid(descriptorUuid);

			auto permissionsObj = characteristicsObj["permissions"];
			BluetoothGattDescriptorPermissions permissions = ((permissionsObj["read"].asBool() << 0) |
			                                                  (permissionsObj["readEncrypted"].asBool() << 1) |
			                                                  (permissionsObj["readEncryptedMitm"].asBool() << 2) |
			                                                  (permissionsObj["write"].asBool() << 3) |
			                                                  (permissionsObj["writeEncrypted"].asBool() << 4) |
			                                                  (permissionsObj["writeEncryptedMitm"].asBool() << 5) |
			                                                  (permissionsObj["writeSigned"].asBool() << 6) |
			                                                  (permissionsObj["writeSignedMitm"].asBool() << 7) );

			descriptor.setPermissions(permissions);

			auto descValueObj = descriptorObj["value"];
			BluetoothGattValue descValue;

			if (!parseValue(descValueObj, &descValue))
			{
				LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTC_INVALID_VALUE_PARAM);
				return true;
			}

			descriptor.setValue(descValue);
			characteristic.addDescriptor(descriptor);
		}
		gattService.addCharacteristic(characteristic);
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto addServiceCallback = [this, requestMessage, serviceUuid, adapterAddress](BluetoothError error) {
		if (error != BLUETOOTH_ERROR_NONE)
		{
			BT_ERROR("ADD_SERVICE_FAILED", 0, "Add service %s fail code %d", serviceUuid.c_str(), error);
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_ADD_SERVICE_FAIL);
			return;
		}
		BT_DEBUG("Add service %s complete", serviceUuid.c_str());

        auto localService = findLocalService(serviceUuid);
        if (!localService)
        {
            BT_ERROR("ADD_SERVICE_FAILED", 0, "Failed to findLocalService %s ", serviceUuid.c_str());
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_ADD_SERVICE_FAIL);
			return;
        }

        auto localServer = findLocalServerByServiceId(localService->id);
        if (!localServer)
        {
            BT_ERROR("ADD_SERVICE_FAILED", 0, "Failed to find localServer %d", localService->id);
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_ADD_SERVICE_FAIL);
			return;
        }

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("serverId", idToString(localServer->id));
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(requestMessage, responseObj);
	};


	if (requestObj.hasKey("serverId"))
	{
		uint16_t appId = idToInt(requestObj["serverId"].asString());
		for (auto iterServer : mLocalServer)
		{
			if(iterServer.second->id == appId)
			{
				addLocalService(iterServer.first, gattService, addServiceCallback, adapterAddress);
				return true;
			}
		}
		LSUtils::respondWithError(request, BT_ERR_GATT_ADD_SERVICE_FAIL);
		return true;
	}
	else
		addLocalService(gattService.getUuid(), gattService, addServiceCallback, adapterAddress);

	return true;
}

bool BluetoothGattProfileService::removeService(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(serverId, string), PROP(service, string)) REQUIRED_1(service));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("service"))
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string serviceUuid = requestObj["service"].asString();
	auto removeServiceCallback = [this, requestMessage, serviceUuid, adapterAddress](BluetoothError error) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			BT_ERROR("REMOVE_SERVICE_FAILED", 0, "Remove service %s fail code %d", serviceUuid.c_str(), error);
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_REMOVE_SERVICE_FAIL);
			return;
		}

		BT_INFO("BLE", 0, "Remove service %s complete", serviceUuid.c_str());
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	// TODO: remove Callback
	bool res = false;
	if (requestObj.hasKey("serverId"))
	{
		uint16_t appId = idToInt(requestObj["serverId"].asString());
		res = removeLocalService(appId, BluetoothUuid(serviceUuid), adapterAddress);
	}
	else
		res = removeLocalService(BluetoothUuid(serviceUuid), adapterAddress);

	removeServiceCallback(res ? BLUETOOTH_ERROR_NONE : BLUETOOTH_ERROR_FAIL);

	return true;
}

bool BluetoothGattProfileService::connect(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string),
			                         PROP(autoConnect, boolean), PROP(subscribe, boolean)) REQUIRED_1(address));

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
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	connectToStack(request, requestObj, adapterAddress);

	return true;
}

bool BluetoothGattProfileService::disconnect(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(clientId, string),
						PROP(adapterAddress, string)) REQUIRED_1(clientId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("clientId"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	disconnectToStack(request, requestObj, adapterAddress);

	return true;
}

bool BluetoothGattProfileService::getStatus(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	bool subscribed = false;

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

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string address = convertToLower(requestObj["address"].asString());

	if (request.isSubscription())
	{
		LS::SubscriptionPoint *subscriptionPoint = 0;
		GattStatusSubsInfo *subsPtr = 0;
		auto subscriptionsIter = mGetStatusSubsMap.find(adapterAddress);
		if (subscriptionsIter == mGetStatusSubsMap.end())
		{
			std::map<std::string, std::pair<LS::SubscriptionPoint*, GattStatusSubsInfo*>> subscriptions;
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			subsPtr = new GattStatusSubsInfo;
			subsPtr->init();
			subscriptions[address] = {subscriptionPoint, subsPtr};
			mGetStatusSubsMap[adapterAddress] = subscriptions;
		}
		else
		{
			auto subscriptionIter = (subscriptionsIter->second).find(address);
			if (subscriptionIter == (subscriptionsIter->second).end())
			{
				std::map<std::string, std::pair<LS::SubscriptionPoint*, GattStatusSubsInfo*>> subscriptions;
				subscriptionPoint = new LS::SubscriptionPoint;
				subscriptionPoint->setServiceHandle(getManager());
				subsPtr = new GattStatusSubsInfo;
				subsPtr->init();
				(subscriptionsIter->second)[address] = {subscriptionPoint, subsPtr};
			}
			else
			{
				subscriptionPoint = subscriptionIter->second.first;
			}
		}

		subscriptionPoint->subscribe(request);
		subscribed = true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	bool isConnected = false;
	auto iter = mConnectedDevicesMap.find(adapterAddress);
	if ((iter != mConnectedDevicesMap.end()) && (iter->second.find(address) != iter->second.end()))
		isConnected = true;

	pbnjson::JValue responseObj = buildGetStatusResp(isConnected,
			isDeviceConnecting(adapterAddress, address), subscribed,
			true, adapterAddress, address);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothGattProfileService::openServer(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
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
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	std::string server = std::to_string(nextClientId());
	if(findLocalServer(BluetoothUuid(server)) != nullptr)
	{
		BT_ERROR("GATT_FAILED_TO_OPEN_SERVER", 0, "server %s already registered", server.c_str());
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_PARAM_INVALID);
		return true;
	}

	LocalServer* newServer = new LocalServer;
	if(!addLocalServer(BluetoothUuid(server), newServer, adapterAddress))
	{
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_FAIL);
		return true;
	}
	BT_DEBUG("Add server %s complete", server.c_str());

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("serverId", idToString(newServer->id));
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(requestMessage, responseObj);

	return true;
}

bool BluetoothGattProfileService::closeServer(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	std::string server;
	uint16_t appId = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(serverId, string)) REQUIRED_1(serverId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("serverId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVERID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}


	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (requestObj.hasKey("serverId"))
	{
		appId = idToInt(requestObj["serverId"].asString());
		std::string adptAddress1 = mServerAdapterMap[appId];

		if(adptAddress1.compare(adapterAddress) != 0 || findLocalServer(appId) == nullptr)
		{
			BT_ERROR("GATT_FAILED_TO_CLOSE_SERVER", 0, "server %s not exist", server.c_str());
			LSUtils::respondWithError(request, BT_ERR_GATT_REMOVE_SERVER_FAIL);
			return true;
		}
		if(!removeLocalServer(appId, adapterAddress))
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_REMOVE_SERVER_FAIL);
			return true;
		}
		BT_DEBUG("Close server %d complete", appId);
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(requestMessage, responseObj);

	return true;
}

std::string serviceTypeToStr(BluetoothGattService::Type type)
{
	std::string str = "unknown";

	switch (type)
	{
		case BluetoothGattService::Type::UNKNOWN:
			str = "unknown";
			break;

		case BluetoothGattService::Type::PRIMARY:
			str = "primary";
            break;

		case BluetoothGattService::Type::SECONDARY:
			str = "secondary";
			break;

		default:
			break;
	}
	return str;
}

void BluetoothGattProfileService::notifyGetServicesSubscribers(bool localAdapterChanged, const std::string &adapterAddress,
                                                                       const std::string &deviceAddress, BluetoothGattServiceList serviceList)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	std::string address;
	if (localAdapterChanged)
		address = adapterAddress;
	else
		address = deviceAddress;

	auto subscriptionIter = mGetServicesSubscriptions.find(address);
	if (subscriptionIter == mGetServicesSubscriptions.end())
		return;

	LS::SubscriptionPoint *subscriptionPoint = subscriptionIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("adapterAddress", adapterAddress);
	if (!deviceAddress.empty())
		responseObj.put("address", deviceAddress);
	appendServiceResponse(localAdapterChanged, responseObj, serviceList);

	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
}

pbnjson::JValue BluetoothGattProfileService::buildDescriptor(const BluetoothGattDescriptor &descriptor, bool localAdapterServices)
{
	pbnjson::JValue descriptorObj = pbnjson::Object();
	descriptorObj.put("descriptor", descriptor.getUuid().toString());
	pbnjson::JValue descValueObj = pbnjson::Object();
	BluetoothGattValue descValues = descriptor.getValue();
	pbnjson::JValue descBytesArray = pbnjson::Array();
	for (size_t j=0; j < descValues.size(); j++)
		descBytesArray.append((int32_t) descValues[j]);

	descValueObj.put("bytes", descBytesArray);
	descriptorObj.put("value", descValueObj);

	pbnjson::JValue permissionsObj = pbnjson::Object();
	if (localAdapterServices)
	{
		permissionsObj.put("read", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ));
		permissionsObj.put("write", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE));
		permissionsObj.put("readEncrypted", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED));
		permissionsObj.put("readEncryptedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED_MITM));
		permissionsObj.put("writeEncrypted", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED));
		permissionsObj.put("writeEncryptedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED_MITM));
		permissionsObj.put("writeSigned", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED));
		permissionsObj.put("writeSignedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED_MITM));
	}

	descriptorObj.put("permissions", permissionsObj);

	return descriptorObj;
}

pbnjson::JValue BluetoothGattProfileService::buildDescriptors(const BluetoothGattDescriptorList &descriptorsList, bool localAdapterServices)
{
	pbnjson::JValue descriptors = pbnjson::Array();

	for (auto descriptor : descriptorsList)
	{
		if (!descriptor.isValid())
			continue;

		pbnjson::JValue descriptorObj = pbnjson::Object();
		descriptorObj.put("descriptor", descriptor.getUuid().toString());
		pbnjson::JValue descValueObj = pbnjson::Object();
		BluetoothGattValue descValues = descriptor.getValue();

		pbnjson::JValue descBytesArray = pbnjson::Array();
		for (size_t j=0; j < descValues.size(); j++)
			descBytesArray.append((int32_t) descValues[j]);

		descValueObj.put("bytes", descBytesArray);
		descriptorObj.put("value", descValueObj);

		pbnjson::JValue permissionsObj = pbnjson::Object();
		if (localAdapterServices)
		{
			permissionsObj.put("read", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ));
			permissionsObj.put("write", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE));
			permissionsObj.put("readEncrypted", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED));
			permissionsObj.put("readEncryptedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED_MITM));
			permissionsObj.put("writeEncrypted", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED));
			permissionsObj.put("writeEncryptedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED_MITM));
			permissionsObj.put("writeSigned", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED));
			permissionsObj.put("writeSignedMitm", descriptor.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED_MITM));
		}

		descriptorObj.put("permissions", permissionsObj);

		descriptors.append(descriptorObj);
	}

	return descriptors;
}

pbnjson::JValue BluetoothGattProfileService::buildCharacteristic(bool localAdapterServices, const BluetoothGattCharacteristic &characteristic)
{
	pbnjson::JValue characteristicObj = pbnjson::Object();
	characteristicObj.put("characteristic", characteristic.getUuid().toString());
	pbnjson::JValue valueObj = pbnjson::Object();
	BluetoothGattValue values = characteristic.getValue();

	pbnjson::JValue bytesArray = pbnjson::Array();
	for (size_t i=0; i < values.size(); i++)
		bytesArray.append((int32_t) values[i]);

	valueObj.put("bytes", bytesArray);
	characteristicObj.put("value", valueObj);

	pbnjson::JValue propertiesObj = pbnjson::Object();
	propertiesObj.put("broadcast", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_BROADCAST));
	propertiesObj.put("read", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_READ));
	propertiesObj.put("writeWithoutResponse", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_WRITE_WITHOUT_RESPONSE));
	propertiesObj.put("write", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_WRITE));
	propertiesObj.put("notify", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_NOTIFY));
	propertiesObj.put("indicate", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_INDICATE));
	propertiesObj.put("authenticatedSignedWrites", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_AUTHENTICATED_SIGNED_WRITES));
	propertiesObj.put("extendedProperties", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_EXTENDED_PROPERTIES));

	characteristicObj.put("properties", propertiesObj);

	pbnjson::JValue permissionsObj = pbnjson::Object();
	if (localAdapterServices)
	{
		permissionsObj.put("read", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ));
		permissionsObj.put("write", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE));
		permissionsObj.put("readEncrypted", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED));
		permissionsObj.put("readEncryptedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED_MITM));
		permissionsObj.put("writeEncrypted", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED));
		permissionsObj.put("writeEncryptedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED_MITM));
		permissionsObj.put("writeSigned", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED));
		permissionsObj.put("writeSignedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED_MITM));
	}
	characteristicObj.put("permissions", permissionsObj);

	auto descriptorsList = characteristic.getDescriptors();
	characteristicObj.put("descriptors", buildDescriptors(descriptorsList, localAdapterServices));

	return characteristicObj;
}

pbnjson::JValue BluetoothGattProfileService::buildCharacteristics(bool localAdapterServices, const BluetoothGattCharacteristicList &characteristicsList)
{
	pbnjson::JValue characteristics = pbnjson::Array();
	for (auto characteristic : characteristicsList)
	{
		pbnjson::JValue characteristicObj = pbnjson::Object();
		characteristicObj.put("characteristic", characteristic.getUuid().toString());
		pbnjson::JValue valueObj = pbnjson::Object();
		BluetoothGattValue values = characteristic.getValue();

		pbnjson::JValue bytesArray = pbnjson::Array();
		for (size_t i=0; i < values.size(); i++)
			bytesArray.append((int32_t) values[i]);

		valueObj.put("bytes", bytesArray);
		characteristicObj.put("value", valueObj);

		pbnjson::JValue propertiesObj = pbnjson::Object();
		propertiesObj.put("broadcast", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_BROADCAST));
		propertiesObj.put("read", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_READ));
		propertiesObj.put("writeWithoutResponse", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_WRITE_WITHOUT_RESPONSE));
		propertiesObj.put("write", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_WRITE));
		propertiesObj.put("notify", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_NOTIFY));
		propertiesObj.put("indicate", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_INDICATE));
		propertiesObj.put("authenticatedSignedWrites", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_AUTHENTICATED_SIGNED_WRITES));
		propertiesObj.put("extendedProperties", characteristic.isPropertySet(BluetoothGattCharacteristic::Property::PROPERTY_EXTENDED_PROPERTIES));

		characteristicObj.put("properties", propertiesObj);

		pbnjson::JValue permissionsObj = pbnjson::Object();
		if (localAdapterServices)
		{
			permissionsObj.put("read", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ));
			permissionsObj.put("write", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE));
			permissionsObj.put("readEncrypted", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED));
			permissionsObj.put("readEncryptedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_READ_ENCRYPTED_MITM));
			permissionsObj.put("writeEncrypted", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED));
			permissionsObj.put("writeEncryptedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_ENCRYPTED_MITM));
			permissionsObj.put("writeSigned", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED));
			permissionsObj.put("writeSignedMitm", characteristic.isPermissionSet(BluetoothGattPermission::PERMISSION_WRITE_SIGNED_MITM));
		}
		characteristicObj.put("permissions", permissionsObj);

		auto descriptorsList = characteristic.getDescriptors();
		characteristicObj.put("descriptors", buildDescriptors(descriptorsList, localAdapterServices));

		characteristics.append(characteristicObj);
	}

	return characteristics;
}

void BluetoothGattProfileService::appendServiceResponse(bool localAdapterServices, pbnjson::JValue responseObj, BluetoothGattServiceList serviceList)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	pbnjson::JValue responseServices = pbnjson::Array();

	for (auto service : serviceList)
	{
		BT_DEBUG("%s: Got service %s", __func__, service.getUuid().toString().c_str());
		if (service.isValid())
		{
			pbnjson::JValue serviceObj = pbnjson::Object();
			serviceObj.put("service", service.getUuid().toString());
			//TODO: Add name for standard services
			serviceObj.put("type", serviceTypeToStr(service.getType()));

			pbnjson::JValue includeServices = pbnjson::Array();
			BluetoothUuidList includeServiceList = service.getIncludedServices();
			for (auto includeService : includeServiceList)
			{
				includeServices.append(includeService.toString());
			}
			serviceObj.put("includes", includeServices);

			BluetoothGattCharacteristicList characteristicsList = service.getCharacteristics();
			pbnjson::JValue characteristics = buildCharacteristics(localAdapterServices, characteristicsList);
			serviceObj.put("characteristics", characteristics);

			responseServices.append(serviceObj);
		}
	}
	responseObj.put("services", responseServices);
}

bool BluetoothGattProfileService::getServices(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string), PROP(subscribe, boolean)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	bool localServices = false;
	if (requestObj.hasKey("adapterAddress"))
	{
		localServices = true;
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	std::string deviceAddress;
	bool remoteServices = false;
	if (requestObj.hasKey("address"))
	{
		if (localServices)
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_DISCOVERY_INVALID_PARAM);
			return true;
		}
		adapterAddress.clear();
		remoteServices = true;
		deviceAddress = convertToLower(requestObj["address"].asString());
		for (auto entry : mConnectedDevicesMap)
		{
			for(auto eachPair : entry.second)
			{
				if (eachPair.first == deviceAddress)
				{
					adapterAddress = entry.first;
					break;
				}
			}
			if (!adapterAddress.empty())
				break;
		}
		if (adapterAddress.empty())
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	if (!localServices && !remoteServices)
	{
		LSUtils::respondWithError(request, BT_ERR_GATT_DISCOVERY_INVALID_PARAM);
		return true;
	}

	std::string address;
	if (localServices)
		address = adapterAddress;
	else if (remoteServices)
		address = deviceAddress;

	if (request.isSubscription())
	{
		LS::SubscriptionPoint *subscriptionPoint = 0;

		auto subscriptionIter = mGetServicesSubscriptions.find(address);
		if (subscriptionIter == mGetServicesSubscriptions.end())
		{
			subscriptionPoint = new LS::SubscriptionPoint;
			subscriptionPoint->setServiceHandle(getManager());
			mGetServicesSubscriptions.insert(std::pair<std::string, LS::SubscriptionPoint*>(address, subscriptionPoint));
		}
		else
		{
			subscriptionPoint = subscriptionIter->second;
		}

		subscriptionPoint->subscribe(request);
	}

	BluetoothGattServiceList serviceList;
	if(localServices)
	{
		serviceList = getLocalServices();
	}
	else
	{
		BT_DEBUG("[%s](%d) getImpl->getServices\n", __FUNCTION__, __LINE__);
		serviceList = getImpl<BluetoothGattProfile>(adapterAddress)->getServices(address);
	}
	BT_DEBUG("Got list of GATT services for address %s", address.c_str());

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	if (request.isSubscription())
		responseObj.put("subscribed", true);
	if (!deviceAddress.empty())
		responseObj.put("address", deviceAddress);
	appendServiceResponse(localServices, responseObj, serviceList);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothGattProfileService::isCharacteristicValid(const std::string &adapterAddress, const std::string &address,
		const uint16_t &handle, BluetoothGattCharacteristic *characteristic)
{
	bool valid_characteristic = false;
	BluetoothGattCharacteristic retCharacteristic;

	if (address.empty())
	{
		BluetoothGattCharacteristic characteristicElement;
		if(getLocalCharacteristic(handle, characteristicElement))
		{
			retCharacteristic = characteristicElement;
			valid_characteristic = true;
		}
	}
	else
	{
		BluetoothGattServiceList services = getImpl<BluetoothGattProfile>(adapterAddress)->getServices(address);
		for (auto service : services)
		{
			BluetoothGattCharacteristicList characteristicList = service.getCharacteristics();
			for (auto characteristicElement : characteristicList)
			{
				if (characteristicElement.getHandle() == handle)
				{
					retCharacteristic = characteristicElement;
					valid_characteristic = true;
					break;
				}
			}
		}
	}
	*characteristic = retCharacteristic;
	return valid_characteristic;
}

bool BluetoothGattProfileService::isCharacteristicValid(const std::string &adapterAddress, const std::string &address,
		const std::string &serviceUuid, const std::string &characteristicUuid, BluetoothGattCharacteristic *characteristic)
{
	bool valid_characteristic = false;
	BluetoothGattCharacteristic retCharacteristic;

	BluetoothGattService service;
	if (address.empty())
		service = getLocalService(serviceUuid);
	else
		service = getImpl<BluetoothGattProfile>(adapterAddress)->getService(address, serviceUuid);

	BluetoothGattCharacteristicList characteristicList = service.getCharacteristics();
	for (auto characteristicElement : characteristicList)
	{
		if (characteristicElement.getUuid().toString() == characteristicUuid)
		{
			retCharacteristic = characteristicElement;
			valid_characteristic = true;
			break;
		}
	}
	*characteristic = retCharacteristic;
	return valid_characteristic;
}

bool BluetoothGattProfileService::getConnectId(uint16_t clientId, uint16_t &connectId,
	std::string &deviceAddress, const std::string &adapterAddress)
{
	auto iter1 = mConnectedDevicesMap.find(adapterAddress);
	if (iter1 != mConnectedDevicesMap.end())
	{
		for (auto entry : iter1->second)
		{
			if (clientId == entry.second)
			{
				deviceAddress = convertToLower(entry.first);
				connectId = entry.second;
				return true;
			}
		}
	}
	return false;
}

bool BluetoothGattProfileService::writeCharacteristicValue(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_7(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string),
	                                                 PROP(writeType, string),
	                                                 OBJECT(value, OBJSCHEMA_3(PROP(string, string),
	                                                                           PROP(number, integer),
	                                                                           ARRAY(bytes, integer))))
	                                                 REQUIRED_1(value));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("value"))
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTC_VALUE_PARAM_MISSING);

		else if (requestObj.hasKey("instanceId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!requestObj.hasKey("instanceId"))
	{
		if (!requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("characteristic"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);
			return true;
		}
		else if(!requestObj.hasKey("characteristic") && !requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_HANDLE_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	std::string serviceUuid;
	std::string characteristicUuid;
	if (requestObj.hasKey("service"))
		serviceUuid = requestObj["service"].asString();

	if (requestObj.hasKey("characteristic"))
		characteristicUuid = requestObj["characteristic"].asString();

	auto valueObj = requestObj["value"];
	BluetoothGattValue value;

	if (!parseValue(valueObj, &value))
	{
		LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTC_INVALID_VALUE_PARAM);
		return true;
	}

	std::string address;
	if (!deviceAddress.empty())
		address = deviceAddress;

	BluetoothGattCharacteristic characteristicToWrite;
	if (requestObj.hasKey("instanceId"))
	{
		uint16_t handle = idToInt(requestObj["instanceId"].asString());
		if (!isCharacteristicValid(adapterAddress, address, handle, &characteristicToWrite))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}
	}
	else
	{
		if (!isCharacteristicValid(adapterAddress, address, serviceUuid, characteristicUuid, &characteristicToWrite))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}
	}
	characteristicToWrite.setValue(value);

	if (requestObj.hasKey("writeType"))
	{
		std::string writeType = requestObj["writeType"].asString();
		if(writeType == "default")
			characteristicToWrite.setWriteType(WriteType::DEFAULT);
		else if(writeType == "noresponse")
			characteristicToWrite.setWriteType(WriteType::NO_RESPONSE);
		else if(writeType == "signed")
			characteristicToWrite.setWriteType(WriteType::SIGNED);
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto writeCharacteristicCallback  = [this, requestMessage, characteristicUuid, serviceUuid, adapterAddress, deviceAddress](BluetoothError error) {
		BT_INFO("BLE", 0, "write characteristic complete for characteristic %s of service %s", characteristicUuid.c_str(), serviceUuid.c_str());

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_WRITE_CHARACTERISTIC_FAIL);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		if (!deviceAddress.empty())
			responseObj.put("address", deviceAddress);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_INFO("BLE", 0, "[%s](%d) getImpl->writeCharacteristic\n", __FUNCTION__, __LINE__);
	if (!deviceAddress.empty())
	{
		if(connectId > 0)
		{
			if (!requestObj.hasKey("instanceId"))
				getImpl<BluetoothGattProfile>()->writeCharacteristic(connectId, serviceUuid, characteristicToWrite, writeCharacteristicCallback);
			else
				getImpl<BluetoothGattProfile>()->writeCharacteristic(connectId, characteristicToWrite, writeCharacteristicCallback);
		}
		else
		{
			if (!requestObj.hasKey("instanceId"))
				getImpl<BluetoothGattProfile>()->writeCharacteristic(deviceAddress, serviceUuid, characteristicToWrite, writeCharacteristicCallback);
			else
				getImpl<BluetoothGattProfile>()->writeCharacteristic(deviceAddress, characteristicToWrite, writeCharacteristicCallback);
		}
	}
	else
	{
		if (!requestObj.hasKey("instanceId"))
			writeLocalCharacteristic(serviceUuid, characteristicToWrite, writeCharacteristicCallback);
		else
			writeLocalCharacteristic(characteristicToWrite, writeCharacteristicCallback);
	}
	return true;
}

bool BluetoothGattProfileService::writeRemoteCharacteristic(const std::string deviceAddress,
		const BluetoothUuid &serviceUuid, const BluetoothGattCharacteristic &characteristicToWrite,
		BluetoothResultCallback callback)
{
	uint16_t connectId = getImpl<BluetoothGattProfile>()->getConnectId(deviceAddress);
	if(connectId > 0)
	{
		if (!serviceUuid.toString().empty())
			getImpl<BluetoothGattProfile>()->writeCharacteristic(connectId, serviceUuid, characteristicToWrite, callback);
		else
			getImpl<BluetoothGattProfile>()->writeCharacteristic(connectId, characteristicToWrite, callback);
	}
	else
	{
		if (!serviceUuid.toString().empty())
			getImpl<BluetoothGattProfile>()->writeCharacteristic(deviceAddress, serviceUuid, characteristicToWrite, callback);
		else
			getImpl<BluetoothGattProfile>()->writeCharacteristic(deviceAddress, characteristicToWrite, callback);
	}
	return true;
}

bool BluetoothGattProfileService::readRemoteCharacteristic(const std::string adapterAddress, const std::string deviceAddress,
		const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid, const uint16_t characteristicHandle,
		BluetoothGattReadCharacteristicCallback callback)
{
	uint16_t connectId = getImpl<BluetoothGattProfile>(adapterAddress)->getConnectId(deviceAddress);

	if(connectId > 0)
	{
		if (!serviceUuid.toString().empty() && !characteristicUuid.toString().empty())
			getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristic(connectId, serviceUuid, characteristicUuid, callback);
		else
			getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristic(connectId, characteristicHandle, callback);
	}
	else
	{
		if (!serviceUuid.toString().empty() && !characteristicUuid.toString().empty())
			getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristic(deviceAddress, serviceUuid, characteristicUuid, callback);
		else
			getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristic(deviceAddress, characteristicHandle, callback);
	}
	return true;
}

bool BluetoothGattProfileService::readRemoteCharacteristics(const std::string adapterAddress, const std::string deviceAddress,
		const BluetoothUuid &serviceUuid, const BluetoothUuidList &characteristicUuids,
		BluetoothGattReadCharacteristicsCallback callback)
{
	uint16_t connectId = getImpl<BluetoothGattProfile>(adapterAddress)->getConnectId(deviceAddress);
	if(connectId > 0)
			getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristics(connectId, serviceUuid, characteristicUuids, callback);
	else
		getImpl<BluetoothGattProfile>(adapterAddress)->readCharacteristics(deviceAddress, serviceUuid, characteristicUuids, callback);

	return true;
}

bool BluetoothGattProfileService::readCharacteristicValue(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (requestObj.hasKey("instanceId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		return true;
	}

	if (!requestObj.hasKey("instanceId"))
	{
		if (!requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("characteristic"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);
			return true;
		}
		else if(!requestObj.hasKey("characteristic") && !requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_HANDLES_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;
	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string address;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());
	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, address, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	BluetoothUuidList characteristicUuids;
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	std::string serviceUuid;
	std::string characteristicUuid;
	BluetoothGattCharacteristic characteristicToRead;
	if (requestObj.hasKey("instanceId"))
	{
		uint16_t handle = idToInt(requestObj["instanceId"].asString());
		if (!isCharacteristicValid(adapterAddress, address, handle, &characteristicToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}
	}
	else
	{
		serviceUuid = requestObj["service"].asString();
		characteristicUuid = requestObj["characteristic"].asString();
		if (!isCharacteristicValid(adapterAddress, address, serviceUuid, characteristicUuid, &characteristicToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}
	}

	auto readCharacteristicCallback  = [this, requestMessage, adapterAddress, address](BluetoothError error, BluetoothGattCharacteristic characteristic) {
		BT_INFO("BLE", 0, "Read characteristic complete");
		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_READ_CHARACTERISTIC_FAIL);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		if (!address.empty())
			responseObj.put("address", address);

		auto characteristicValue = buildCharacteristic(address.empty(), characteristic);

		responseObj.put("value", characteristicValue);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_DEBUG("[%s](%d) getImpl->readCharacteristics\n", __FUNCTION__, __LINE__);
	if (!address.empty())
		readRemoteCharacteristic(adapterAddress, address, serviceUuid, characteristicUuid, characteristicToRead.getHandle(), readCharacteristicCallback);
	else
		readCharacteristicCallback(BLUETOOTH_ERROR_NONE, characteristicToRead);

	return true;
}

bool BluetoothGattProfileService::readCharacteristicValues(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), ARRAY(characteristics, string))
													 REQUIRED_2(service, characteristics));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("service"))
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);

		else if (!requestObj.hasKey("characteristics"))
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTICS_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;
	if (!getImpl<BluetoothGattProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	std::string address;
	if (!deviceAddress.empty())
		address = deviceAddress;

	BluetoothUuidList characteristicUuids;
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	std::string serviceUuid = requestObj["service"].asString();
	auto characteristicUuidsArray = requestObj["characteristics"];
	for (int i=0; i < characteristicUuidsArray.arraySize(); i++)
	{
		BluetoothGattCharacteristic characteristicToRead;
		if (!isCharacteristicValid(adapterAddress, address, serviceUuid,
					characteristicUuidsArray[i].asString(), &characteristicToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}

		characteristicUuids.push_back(BluetoothUuid(characteristicUuidsArray[i].asString()));
	}

	auto readCharacteristicCallback  = [this, requestMessage, serviceUuid, adapterAddress, deviceAddress](BluetoothError error, BluetoothGattCharacteristicList characteristicsList) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_READ_CHARACTERISTIC_FAIL);
			return;
		}

		BT_INFO("BLE", 0, "Read characteristics complete for service %s", serviceUuid.c_str());
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("service", serviceUuid);

		if (!deviceAddress.empty())
			responseObj.put("address", deviceAddress);

		auto characteristics = buildCharacteristics(deviceAddress.empty(), characteristicsList);

		responseObj.put("values", characteristics);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_DEBUG("[%s](%d) getImpl->readCharacteristics\n", __FUNCTION__, __LINE__);
	if (!deviceAddress.empty())
		readRemoteCharacteristics(adapterAddress, deviceAddress, serviceUuid, characteristicUuids, readCharacteristicCallback);
	else
		readLocalCharacteristics(serviceUuid, characteristicUuids, readCharacteristicCallback);

	return true;
}

void BluetoothGattProfileService::handleMonitorCharacteristicClientDropped(MonitorCharacteristicSubscriptionInfo &subscriptionInfo, LSUtils::ClientWatch *monitorCharacteristicsWatch)
{
	BT_INFO("BLE", 0, "%s: Monitor client disappeared for device %s", __func__, subscriptionInfo.deviceAddress.c_str());
	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		if (it->first != monitorCharacteristicsWatch)
			continue;

		auto candidate = it->second;

		if (subscriptionInfo.handle > 0)
		{
			if((subscriptionInfo.deviceAddress != candidate.deviceAddress) || (subscriptionInfo.handle != candidate.handle))
				continue;
		}
		else if ((subscriptionInfo.deviceAddress != candidate.deviceAddress) || (subscriptionInfo.serviceUuid != candidate.serviceUuid)
			|| (subscriptionInfo.characteristicUuid != candidate.characteristicUuid))
			continue;

		auto foundWatch = std::find_if(mCharacteristicWatchList.begin(), mCharacteristicWatchList.end(), [subscriptionInfo] (const CharacteristicWatch *watchElement) {

			if ((watchElement->deviceAddress == subscriptionInfo.deviceAddress) && (watchElement->handle == subscriptionInfo.handle))
				return true;

			else if ((watchElement->deviceAddress == subscriptionInfo.deviceAddress) && (watchElement->serviceId == subscriptionInfo.serviceUuid))
				return true;

			return false;
		});

		if (foundWatch != mCharacteristicWatchList.end())
		{
			(*foundWatch)->unref();
			if (!((*foundWatch)->isUsed()))
			{
				BT_DEBUG("Disabling characteristic watch to device %s", (*foundWatch)->deviceAddress.c_str());

				getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus((*foundWatch)->deviceAddress, (*foundWatch)->serviceId, (*foundWatch)->characteristicId, false, [this](BluetoothError error) {
					BT_WARNING(MSGID_SUBSCRIPTION_CLIENT_DROPPED, 0, "No LS2 error response can be issued since subscription client has dropped");
				});
				mCharacteristicWatchList.erase(foundWatch);
			}
		}

		auto monitorSubscriptionWatch = it->first;
		mMonitorCharacteristicSubscriptions.erase(it);
		delete monitorSubscriptionWatch;
		monitorSubscriptionWatch = 0;
		break;
	}
}

void BluetoothGattProfileService::handleMonitorCharacteristicsClientDropped(MonitorCharacteristicSubscriptionInfo &subscriptionInfo, LSUtils::ClientWatch *monitorCharacteristicsWatch)
{
	BT_INFO("BLE", 0, "%s: Monitor client disappeared for device %s", __func__, subscriptionInfo.deviceAddress.c_str());
	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		if (it->first != monitorCharacteristicsWatch)
			continue;

		auto candidate = it->second;
		if ((subscriptionInfo.deviceAddress != candidate.deviceAddress) || (subscriptionInfo.serviceUuid != candidate.serviceUuid)
			|| (subscriptionInfo.characteristicUuids != candidate.characteristicUuids))
			continue;

		auto foundWatch = std::find_if(mCharacteristicWatchList.begin(), mCharacteristicWatchList.end(), [subscriptionInfo] (const CharacteristicWatch *watchElement) {
			if ((watchElement->deviceAddress == subscriptionInfo.deviceAddress) && (watchElement->serviceId == subscriptionInfo.serviceUuid))
			{
				auto foundCharacteristic = std::find(subscriptionInfo.characteristicUuids.begin(), subscriptionInfo.characteristicUuids.end(), watchElement->characteristicId);
				if (foundCharacteristic != subscriptionInfo.characteristicUuids.end())
					return true;
			}
			return false;
		});

		if (foundWatch != mCharacteristicWatchList.end())
		{
			(*foundWatch)->unref();
			if (!((*foundWatch)->isUsed()))
			{
				BT_DEBUG("Disabling characteristic watch to device %s", (*foundWatch)->deviceAddress.c_str());

				getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus((*foundWatch)->deviceAddress, (*foundWatch)->serviceId, (*foundWatch)->characteristicId, false, [this](BluetoothError error) {
					BT_WARNING(MSGID_SUBSCRIPTION_CLIENT_DROPPED, 0, "No LS2 error response can be issued since subscription client has dropped");
				});
				mCharacteristicWatchList.erase(foundWatch);
			}
		}

		auto monitorSubscriptionWatch = it->first;
		mMonitorCharacteristicSubscriptions.erase(it);
		delete monitorSubscriptionWatch;
		monitorSubscriptionWatch = 0;
		break;
	}
}

bool BluetoothGattProfileService::monitorCharacteristic(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string),
	                                                 PROP(subscribe, boolean))
	                                                 REQUIRED_1(subscribe));
	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("subscribe"))
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else if (requestObj.hasKey("instanceId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!requestObj.hasKey("instanceId"))
	{
		if (!requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("characteristic"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);
			return true;
		}
		else if(!requestObj.hasKey("characteristic") && !requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_HANDLE_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	LSUtils::ClientWatch *monitorCharacteristicsWatch;
	monitorCharacteristicsWatch = new LSUtils::ClientWatch(getManager()->get(), request.get(),nullptr);

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	BluetoothGattCharacteristic characteristicToMonitor;
	MonitorCharacteristicSubscriptionInfo subscriptionInfo;
	if (!deviceAddress.empty())
		subscriptionInfo.deviceAddress = deviceAddress;

	if (requestObj.hasKey("instanceId"))
	{
		uint16_t handle = idToInt(requestObj["instanceId"].asString());
		if (!isCharacteristicValid(adapterAddress, deviceAddress, handle, &characteristicToMonitor))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}

		subscriptionInfo.handle = handle;
	}
	else
	{
		BluetoothGattService service;
		std::string serviceUuid = requestObj["service"].asString();

		if (deviceAddress.empty())
			service = getLocalService(serviceUuid);
		else
			service = getImpl<BluetoothGattProfile>()->getService(deviceAddress, serviceUuid);

		if (!service.isValid())
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_SERVICE);
			return true;
		}

		std::string characteristicUuid = requestObj["characteristic"].asString();
		//check if every characteristic to monitor is a characteristic of the given service
		if (!isCharacteristicValid(adapterAddress, deviceAddress, serviceUuid, characteristicUuid, &characteristicToMonitor))
		{
			LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_GATT_INVALID_CHARACTERISTIC) + characteristicUuid, BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}

		subscriptionInfo.serviceUuid = serviceUuid;
		subscriptionInfo.characteristicUuid = characteristicUuid;
		subscriptionInfo.handle = 0;
	}

	//Set a callback to see if client dropped. We do it this way because we first need to get the client watch object and pass this
	//object to the callback. This object is later used to check if the client which dropped has the same client watch created here.
	//When two apps are registered for monitoring on the same address-service-characteristics combination, there will be
	//two entries in the mMonitorCharacteristicSubscriptions list. Now if the second app drops subscription, the first entry of the
	//combination is picked up in the list iteration and the wrong watch is dropped. To avoid this, the client watch object is saved
	//and verified before dropping the subscription.
	monitorCharacteristicsWatch->setCallback (std::bind(&BluetoothGattProfileService::handleMonitorCharacteristicClientDropped, this, subscriptionInfo, monitorCharacteristicsWatch));

	mMonitorCharacteristicSubscriptions.push_back(std::make_pair(monitorCharacteristicsWatch, subscriptionInfo));

	auto foundWatch = std::find_if(mCharacteristicWatchList.begin(), mCharacteristicWatchList.end(), [deviceAddress, subscriptionInfo](const CharacteristicWatch* watchElement)
	{
		return ((watchElement->deviceAddress == deviceAddress) &&
			 (watchElement->handle == subscriptionInfo.handle));
	});

	if (foundWatch != mCharacteristicWatchList.end())
	{
		// The address-service-characteristic combination already exists, so increment the reference count;
		// this is unref-ed when client subscription is dropped
		BT_DEBUG("Found watch in the characteristic list, incrementing ref count");
		(*foundWatch)->ref();
	}
	else
	{
		BT_DEBUG("Watch element not found in the list, creating new watch to the characteristic list");
		auto watch = new CharacteristicWatch();
		watch->deviceAddress = deviceAddress;
		watch->serviceId = subscriptionInfo.serviceUuid;
		watch->characteristicId = subscriptionInfo.characteristicUuid;
		watch->handle = subscriptionInfo.handle;
		watch->ref();
		mCharacteristicWatchList.push_back(watch);
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	BT_DEBUG("Registering with SIL API for %zu characteristic watch list elements", mCharacteristicWatchList.size());
	for (auto characteristicWatch : mCharacteristicWatchList)
	{
		//eliminate the watches that are already registered with SIL API changeCharacteristicWatchStatus()
		if (characteristicWatch->isRegistered())
			continue;

		BT_DEBUG("Registering a watch with SIL API for device %s, service %s, characteristic %s",
		          characteristicWatch->deviceAddress.c_str(), characteristicWatch->serviceId.toString().c_str(), characteristicWatch->characteristicId.toString().c_str());

		auto monitorCallback = [this, requestMessage, characteristicWatch] (BluetoothError error)
		{
			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(requestMessage, retrieveErrorText(BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL) + characteristicWatch->characteristicId.toString(), BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL, true);
				return;
			}
			else
			{
				characteristicWatch->markRegistered();
			}
		};

		BT_DEBUG("[%s](%d) getImpl->changeCharacteristicWatchStatus\n", __FUNCTION__, __LINE__);
		if (!characteristicWatch->deviceAddress.empty())
		{
			uint16_t appId = getImpl<BluetoothGattProfile>()->getAppId(deviceAddress);
			if (appId > 0)
			{
				if(characteristicWatch->handle == 0)
					getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, appId, characteristicWatch->serviceId, characteristicWatch->characteristicId, true, monitorCallback);
				else
					getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, appId, characteristicWatch->handle, true, monitorCallback);
			}
			else
			{
				if(characteristicWatch->handle == 0)
					getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, characteristicWatch->serviceId, characteristicWatch->characteristicId, true, monitorCallback);
				else
					getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, characteristicWatch->handle, true, monitorCallback);
			}
		}

	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("adapterAddress", adapterAddress);
	if(deviceAddress != "")
		responseObj.put("address", deviceAddress);
	LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);

	return true;
}

bool BluetoothGattProfileService::monitorCharacteristics(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), ARRAY(characteristics, string),
	                                                 PROP(subscribe, boolean))
	                                                 REQUIRED_3(subscribe, service, characteristics));
	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("subscribe"))
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		if (!requestObj.hasKey("service"))
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);

		else if (!requestObj.hasKey("characteristics"))
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTICS_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	LSUtils::ClientWatch *monitorCharacteristicsWatch;
	monitorCharacteristicsWatch = new LSUtils::ClientWatch(getManager()->get(), request.get(),nullptr);

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	BluetoothGattService service;
	std::string serviceUuid = requestObj["service"].asString();

	if (deviceAddress.empty())
		service = getLocalService(serviceUuid);
	else
		service = getImpl<BluetoothGattProfile>()->getService(deviceAddress, serviceUuid);

	auto characteristicUuidsArray = requestObj["characteristics"];
	if (!service.isValid())
	{
		LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_SERVICE);
		return true;
	}

	BluetoothUuidList characteristics;
	for (int i=0; i < characteristicUuidsArray.arraySize(); i++)
	{
		BluetoothGattCharacteristic characteristicToMonitor;
		std::string characteristicUuid = characteristicUuidsArray[i].asString();
		//check if every characteristic to monitor is a characteristic of the given service
		if (!isCharacteristicValid(adapterAddress, deviceAddress, serviceUuid, characteristicUuid, &characteristicToMonitor))
		{
			LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_GATT_INVALID_CHARACTERISTIC) + characteristicUuidsArray[i].asString(), BT_ERR_GATT_INVALID_CHARACTERISTIC);
			return true;
		}
		characteristics.push_back(BluetoothUuid(characteristicUuid));
	}

	MonitorCharacteristicSubscriptionInfo subscriptionInfo;
	if (!deviceAddress.empty())
		subscriptionInfo.deviceAddress = deviceAddress;
	subscriptionInfo.serviceUuid = serviceUuid;
	subscriptionInfo.characteristicUuids = characteristics;

	//Set a callback to see if client dropped. We do it this way because we first need to get the client watch object and pass this
	//object to the callback. This object is later used to check if the client which dropped has the same client watch created here.
	//When two apps are registered for monitoring on the same address-service-characteristics combination, there will be
	//two entries in the mMonitorCharacteristicSubscriptions list. Now if the second app drops subscription, the first entry of the
	//combination is picked up in the list iteration and the wrong watch is dropped. To avoid this, the client watch object is saved
	//and verified before dropping the subscription.
	monitorCharacteristicsWatch->setCallback (std::bind(&BluetoothGattProfileService::handleMonitorCharacteristicsClientDropped, this, subscriptionInfo, monitorCharacteristicsWatch));

	mMonitorCharacteristicSubscriptions.push_back(std::make_pair(monitorCharacteristicsWatch, subscriptionInfo));

	for (auto characteristic : characteristics)
	{
		auto foundWatch = std::find_if(mCharacteristicWatchList.begin(), mCharacteristicWatchList.end(), [deviceAddress, serviceUuid, characteristic](const CharacteristicWatch* watchElement)
						{
							return ((watchElement->deviceAddress == deviceAddress) && (watchElement->serviceId == BluetoothUuid(serviceUuid))
								 && (watchElement->characteristicId == BluetoothUuid(characteristic)));
						});

		if (foundWatch != mCharacteristicWatchList.end())
		{
			// The address-service-characteristic combination already exists, so increment the reference count;
			// this is unref-ed when client subscription is dropped
			BT_DEBUG("Found watch in the characteristic list, incrementing ref count");
			(*foundWatch)->ref();
		}
		else
		{
			BT_DEBUG("Watch element not found in the list, creating new watch to the characteristic list");
			auto watch = new CharacteristicWatch();
			watch->deviceAddress = deviceAddress;
			watch->serviceId = BluetoothUuid(serviceUuid);
			watch->characteristicId = BluetoothUuid(characteristic);
			watch->ref();
			mCharacteristicWatchList.push_back(watch);
		}
	}


	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	BT_DEBUG("Registering with SIL API for %zu characteristic watch list elements", mCharacteristicWatchList.size());
	for (auto characteristicWatch : mCharacteristicWatchList)
	{
		//eliminate the watches that are already registered with SIL API changeCharacteristicWatchStatus()
		if (characteristicWatch->isRegistered())
			continue;

		BT_DEBUG("Registering a watch with SIL API for device %s, service %s, characteristic %s",
		          characteristicWatch->deviceAddress.c_str(), characteristicWatch->serviceId.toString().c_str(), characteristicWatch->characteristicId.toString().c_str());

		auto monitorCallback = [this, requestMessage, characteristicWatch] (BluetoothError error)
		{
			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSUtils::respondWithError(requestMessage, retrieveErrorText(BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL) + characteristicWatch->characteristicId.toString(), BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL, true);
				return;
			}
			else
			{
				characteristicWatch->markRegistered();
			}
		};

		BT_DEBUG("[%s](%d) getImpl->changeCharacteristicWatchStatus\n", __FUNCTION__, __LINE__);
		if (!characteristicWatch->deviceAddress.empty())
		{
			uint16_t appId = getImpl<BluetoothGattProfile>()->getAppId(deviceAddress);
			if (appId > 0)
				getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, appId, characteristicWatch->serviceId, characteristicWatch->characteristicId, true, monitorCallback);

			else
				getImpl<BluetoothGattProfile>()->changeCharacteristicWatchStatus(characteristicWatch->deviceAddress, characteristicWatch->serviceId, characteristicWatch->characteristicId, true, monitorCallback);

		}
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);

	return true;
}

bool BluetoothGattProfileService::isDescriptorValid(const std::string &address, const uint16_t &handle,
                                                    BluetoothGattDescriptor &descriptor)
{
	bool validDescriptor = false;
	if (address.empty())
	{
		BluetoothGattDescriptor descriptorElement;
		if(getLocalDescriptor(handle, descriptorElement))
		{
			descriptor = descriptorElement;
			validDescriptor = true;
		}
	}
	else
	{
		BluetoothGattServiceList services = getImpl<BluetoothGattProfile>()->getServices(address);
		for (auto service : services)
		{
			for (auto characteristicElement : service.getCharacteristics())
			{
				for (auto descriptorElement : characteristicElement.getDescriptors())
				{
					if (descriptorElement.getHandle() == handle)
					{
						descriptor = descriptorElement;
						validDescriptor = true;
						break;
					}
				}
			}
		}
	}

	return validDescriptor;
}

void BluetoothGattProfileService::removeSubscriptionPoint(const std::string &adapterAddress, const std::string &devAddress)
{
	if (mConnectSubsMap.find(adapterAddress) != mConnectSubsMap.end())
	{
		auto iter = mConnectSubsMap[adapterAddress].find(devAddress);
		if (iter != mConnectSubsMap[adapterAddress].end())
		{
			LS::SubscriptionPoint* sPtr = mConnectSubsMap[adapterAddress][devAddress].first;
			GattConnSubsInfo* gPtr = mConnectSubsMap[adapterAddress][devAddress].second;
			delete sPtr;
			delete gPtr;
		}
		mConnectSubsMap[adapterAddress].erase(iter);
	}
}

bool BluetoothGattProfileService::isDescriptorValid(const std::string &address, const std::string &serviceUuid,
                                                    const std::string &characteristicUuid,
                                                    const std::string &descriptorUuuid, BluetoothGattDescriptor &descriptor)
{
	bool validDescriptor = false;

	BT_DEBUG("address %s serviceUuid %s", address.c_str(), serviceUuid.c_str());

	BluetoothGattService service;
	if (address.empty())
		service = getLocalService(serviceUuid);
	else
		service = getImpl<BluetoothGattProfile>()->getService(address, serviceUuid);
	if (!service.isValid())
		return false;

	BT_DEBUG("service.uuid %s", service.getUuid().toString().c_str());

	BluetoothGattCharacteristicList characteristicList = service.getCharacteristics();

	BT_DEBUG("char count %zu", characteristicList.size());

	for (auto characteristicElement : characteristicList)
	{
		BT_DEBUG("characteristicElement %s", characteristicElement.getUuid().toString().c_str());
		if (characteristicElement.getUuid().toString() == characteristicUuid)
		{
			for (auto descriptorElement : characteristicElement.getDescriptors())
			{
				BT_DEBUG("descriptorElement %s", descriptorElement.getUuid().toString().c_str());

				if (descriptorElement.getUuid() != descriptorUuuid)
					continue;

				descriptor = descriptorElement;
				validDescriptor = true;
				break;
			}

			if (validDescriptor)
				break;
		}
	}

	return validDescriptor;
}

bool BluetoothGattProfileService::readRemoteDescriptor(const std::string deviceAddress,
		const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid, const BluetoothUuid &descriptorUuid, const uint16_t descriptorHandle,
		BluetoothGattReadDescriptorCallback callback)
{
	uint16_t connectId = getImpl<BluetoothGattProfile>()->getConnectId(deviceAddress);
	if(connectId > 0)
	{
		if (!serviceUuid.toString().empty() && !characteristicUuid.toString().empty() && !descriptorUuid.toString().empty())
			getImpl<BluetoothGattProfile>()->readDescriptor(connectId, serviceUuid, characteristicUuid, descriptorUuid, callback);
		else
			getImpl<BluetoothGattProfile>()->readDescriptor(connectId, descriptorHandle, callback);
	}
	else
	{
		if (!serviceUuid.toString().empty() && !characteristicUuid.toString().empty() && !descriptorUuid.toString().empty())
			getImpl<BluetoothGattProfile>()->readDescriptor(deviceAddress, serviceUuid, characteristicUuid, descriptorUuid, callback);
		else
			getImpl<BluetoothGattProfile>()->readDescriptor(deviceAddress, descriptorHandle, callback);
	}
	return true;
}

bool BluetoothGattProfileService::readRemoteDescriptors(const std::string deviceAddress,
		const BluetoothUuid &serviceUuid, const BluetoothUuid &characteristicUuid, const BluetoothUuidList &descriptorUuids,
		BluetoothGattReadDescriptorsCallback callback)
{
	uint16_t connectId = getImpl<BluetoothGattProfile>()->getConnectId(deviceAddress);
	if(connectId > 0)
		getImpl<BluetoothGattProfile>()->readDescriptors(connectId, serviceUuid, characteristicUuid, descriptorUuids, callback);
	else
		getImpl<BluetoothGattProfile>()->readDescriptors(deviceAddress, serviceUuid, characteristicUuid, descriptorUuids, callback);

	return true;
}

bool BluetoothGattProfileService::readDescriptorValue(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string),
	                                                 PROP(descriptor, string))
	                                                 );

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (requestObj.hasKey("instanceId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!requestObj.hasKey("instanceId"))
	{
		if (!requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("characteristic"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("descriptor"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_DESCRIPTORS_PARAM_MISSING);
			return true;
		}
		else if(!requestObj.hasKey("characteristic") && !requestObj.hasKey("service") && !requestObj.hasKey("descriptor"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_HANDLES_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	std::string address;
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	if (!deviceAddress.empty())
		address = deviceAddress;

	std::string serviceUuid;
	std::string characteristicUuid;
	std::string descriptorUuid;
	uint16_t handle = 0;
	BluetoothGattDescriptor descriptorToRead;

	if (requestObj.hasKey("instanceId"))
	{
		uint16_t handle = idToInt(requestObj["instanceId"].asString());
		BluetoothGattDescriptor descriptorToRead;
		if (!isDescriptorValid(address, handle, descriptorToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_DESCRIPTOR);
			return true;
		}
	}
	else
	{
		serviceUuid = requestObj["service"].asString();
		characteristicUuid = requestObj["characteristic"].asString();
		descriptorUuid = requestObj["descriptor"].asString();
		if (!isDescriptorValid(address, serviceUuid, characteristicUuid, descriptorUuid, descriptorToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_DESCRIPTOR);
			return true;
		}
	}

	auto readDescriptorCallback  = [this, requestMessage, adapterAddress, deviceAddress](BluetoothError error, BluetoothGattDescriptor descriptor) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_READ_DESCRIPTOR_FAIL);
			return;
		}

		BT_INFO("BLE", 0, "Read descriptor complete");
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		if (!deviceAddress.empty())
			responseObj.put("address", deviceAddress);

		auto descriptorValue = buildDescriptor(descriptor);

		responseObj.put("value", descriptorValue);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_DEBUG("[%s](%d) getImpl->readDescriptors\n", __FUNCTION__, __LINE__);

	if (!deviceAddress.empty())
		readRemoteDescriptor(deviceAddress, serviceUuid, characteristicUuid, descriptorUuid, descriptorToRead.getHandle(), readDescriptorCallback);
	else
		readDescriptorCallback(BLUETOOTH_ERROR_NONE, descriptorToRead);

	return true;
}

bool BluetoothGattProfileService::readDescriptorValues(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_6(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string),
	                                                 ARRAY(descriptors, string))
	                                                 REQUIRED_3(service, characteristic, descriptors));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("service"))
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);

		else if (!requestObj.hasKey("characteristic"))
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);

		else if (!requestObj.hasKey("descriptors"))
			LSUtils::respondWithError(request, BT_ERR_GATT_DESCRIPTORS_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	std::string address;
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	if (!deviceAddress.empty())
		address = deviceAddress;


	std::string serviceUuid = requestObj["service"].asString();
	std::string characteristicUuid = requestObj["characteristic"].asString();
	auto descriptorsUuidsArray = requestObj["descriptors"];
	BluetoothUuidList descriptors;
	for (int i = 0; i < descriptorsUuidsArray.arraySize(); i++)
	{
		BluetoothGattDescriptor descriptorToRead;
		if (!isDescriptorValid(address, serviceUuid, characteristicUuid, descriptorsUuidsArray[i].asString(), descriptorToRead))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_DESCRIPTOR);
			return true;
		}

		descriptors.push_back(BluetoothUuid(descriptorsUuidsArray[i].asString()));
	}

	auto readDescriptorsCallback  = [this, requestMessage, serviceUuid, characteristicUuid, adapterAddress, deviceAddress](BluetoothError error, BluetoothGattDescriptorList descriptorList) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_READ_DESCRIPTORS_FAIL);
			return;
		}

		BT_INFO("BLE", 0, "Read descriptors complete for service %s", serviceUuid.c_str());
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("service", serviceUuid);
		responseObj.put("characteristic", characteristicUuid);

		if (!deviceAddress.empty())
			responseObj.put("address", deviceAddress);

		auto descriptors = buildDescriptors(descriptorList);

		responseObj.put("values", descriptors);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_DEBUG("[%s](%d) getImpl->readDescriptors\n", __FUNCTION__, __LINE__);
	if (!deviceAddress.empty())
		readRemoteDescriptors(deviceAddress, serviceUuid, characteristicUuid, descriptors, readDescriptorsCallback);
	else
		readLocalDescriptors(serviceUuid, characteristicUuid, descriptors, readDescriptorsCallback);

	return true;
}

bool BluetoothGattProfileService::writeDescriptorValue(LSMessage &message)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothGattProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_8(PROP(adapterAddress, string), PROP(serverId, string), PROP(clientId, string),
	                                                 PROP(service, string), PROP(characteristic, string),
	                                                 PROP(descriptor, string), PROP(writeType, string),
	                                                 OBJECT(value, OBJSCHEMA_3(PROP(string, string),
	                                                                           PROP(number, integer),
	                                                                           ARRAY(bytes, integer))))
	                                                 REQUIRED_1(value));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("value"))
			LSUtils::respondWithError(request, BT_ERR_GATT_DESCRIPTOR_VALUE_PARAM_MISSING);

		else if (requestObj.hasKey("instanceId"))
			LSUtils::respondWithError(request, BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	if (!requestObj.hasKey("instanceId"))
	{
		if (!requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("characteristic"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING);
			return true;
		}
		else if (!requestObj.hasKey("descriptor"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_DESCRIPTOR_PARAM_MISSING);
			return true;
		}
		else if(!requestObj.hasKey("characteristic") && !requestObj.hasKey("service"))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_HANDLE_PARAM_MISSING);
			return true;
		}
	}

	std::string adapterAddress;
	if (requestObj.hasKey("adapterAddress"))
	{
		adapterAddress = requestObj["adapterAddress"].asString();
		if (!getManager()->isAdapterAvailable(adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_INVALID_ADAPTER_ADDRESS);
			return true;
		}
	}
	else
	{
		adapterAddress = getManager()->getAddress();
	}

	uint16_t appId = 0;
	uint16_t connectId = 0;
	std::string deviceAddress;

	if (requestObj.hasKey("serverId"))
		appId = idToInt(requestObj["serverId"].asString());

	else if(requestObj.hasKey("clientId"))
	{
		appId = idToInt(requestObj["clientId"].asString());
		if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}
	}

	std::string address;
	if (!deviceAddress.empty())
		address = deviceAddress;

	std::string serviceUuid;
	std::string characteristicUuid;
	std::string descriptorUuid;

	if (requestObj.hasKey("service"))
		serviceUuid = requestObj["service"].asString();

	if (requestObj.hasKey("characteristic"))
		characteristicUuid = requestObj["characteristic"].asString();

	if (requestObj.hasKey("descriptor"))
		descriptorUuid = requestObj["descriptor"].asString();

	BluetoothGattDescriptor descriptorToWrite;
	if (requestObj.hasKey("instanceId"))
	{
		uint16_t handle = idToInt(requestObj["instanceId"].asString());
		if (!isDescriptorValid(address, handle, descriptorToWrite))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_DESCRIPTOR);
			return true;
		}
	}
	else
	{
		if (!isDescriptorValid(address, serviceUuid, characteristicUuid, descriptorUuid, descriptorToWrite))
		{
			LSUtils::respondWithError(request, BT_ERR_GATT_INVALID_DESCRIPTOR);
			return true;
		}
	}

	BluetoothGattValue value;
	if (!parseValue(requestObj["value"], &value))
	{
		LSUtils::respondWithError(request, BT_ERR_GATT_DESCRIPTOR_INVALID_VALUE_PARAM);
		return true;
	}
	descriptorToWrite.setValue(value);

	if (requestObj.hasKey("writeType"))
	{
		std::string writeType = requestObj["writeType"].asString();
		if(writeType == "default")
			descriptorToWrite.setWriteType(WriteType::DEFAULT);
		else if(writeType == "noresponse")
			descriptorToWrite.setWriteType(WriteType::NO_RESPONSE);
		else if(writeType == "signed")
			descriptorToWrite.setWriteType(WriteType::SIGNED);
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto writeDescriptorCallback  = [this, requestMessage, serviceUuid, characteristicUuid, descriptorUuid, adapterAddress, deviceAddress](BluetoothError error) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(requestMessage, BT_ERR_GATT_WRITE_DESCRIPTOR_FAIL);
			return;
		}

		BT_INFO("BLE", 0, "Write descriptor complete for service %s", serviceUuid.c_str());
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);

		if (!deviceAddress.empty())
			responseObj.put("address", deviceAddress);

		LSUtils::postToClient(requestMessage, responseObj);
	};

	BT_DEBUG("[%s](%d) getImpl->writeDescriptor\n", __FUNCTION__, __LINE__);
	if (!deviceAddress.empty())
	{
		if (connectId > 0)
		{
			if (!requestObj.hasKey("instanceId"))
				getImpl<BluetoothGattProfile>()->writeDescriptor(connectId, serviceUuid, characteristicUuid, descriptorToWrite, writeDescriptorCallback);
			else
				getImpl<BluetoothGattProfile>()->writeDescriptor(connectId, descriptorToWrite, writeDescriptorCallback);
		}
		else
		{
			if (!requestObj.hasKey("instanceId"))
				getImpl<BluetoothGattProfile>()->writeDescriptor(deviceAddress, serviceUuid, characteristicUuid, descriptorToWrite, writeDescriptorCallback);
			else
				getImpl<BluetoothGattProfile>()->writeDescriptor(deviceAddress, descriptorToWrite, writeDescriptorCallback);
		}
	}
	else
	{
		if (!requestObj.hasKey("instanceId"))
			writeLocalDescriptor(serviceUuid, characteristicUuid, descriptorToWrite, writeDescriptorCallback);
		else
			writeLocalDescriptor(descriptorToWrite, writeDescriptorCallback);
	}

	return true;
}

#define safe_callback(callback, ...) do { if (callback) callback(__VA_ARGS__); } while(0)

bool BluetoothGattProfileService::addLocalServer(const BluetoothUuid applicationUuid, LocalServer* newServer, const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) getImpl->addApplication\n", __FUNCTION__, __LINE__);
	uint16_t server_if = getImpl<BluetoothGattProfile>(adapterAddress)->addApplication(BluetoothUuid(applicationUuid), ApplicationType::SERVER);
	if (server_if == static_cast<uint16_t>(0))
		return false;

	newServer->id = server_if;
	mServerAdapterMap.insert({newServer->id, adapterAddress});
	mLocalServer.insert({applicationUuid, newServer});
	return true;
}

void BluetoothGattProfileService::addLocalService(const BluetoothUuid applicationUuid, const BluetoothGattService &service, BluetoothResultCallback callback,
	const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto server = findLocalServer(applicationUuid);
	if (server == nullptr)
	{
		BT_ERROR("GATT_SERVER_NOT_FOUND", 0,
		             "Server %s not exist",
					 applicationUuid.toString().c_str());
		LocalServer* newServer = new LocalServer;
		if(!addLocalServer(applicationUuid, newServer, adapterAddress))
		{
			BT_ERROR("GATT_SERVICE_ALREADY_REGISTERED", 0, "addLocalServer is failed");
			safe_callback(callback, BLUETOOTH_ERROR_FAIL);
			return;
		}
		server = newServer;
	}

	std::string adptAddress1 = mServerAdapterMap[server->id];
	if (adptAddress1.compare(adapterAddress) != 0)
	{
		BT_ERROR("GATT_SERVER_NOT_FOUND", 0,
					 "Server %s not exist for %s adapterAddress",
					 applicationUuid.toString().c_str(), adapterAddress);
		safe_callback(callback, BLUETOOTH_ERROR_FAIL);
		return;
	}

	if (service.getCharacteristics().size() == 0)
	{
		BT_ERROR("GATT_SERVICE_WITHOUT_CHARACTERISTICS", 0,
		              "Can't register service %s without any characteristics",
		              service.getUuid().toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	if (isLocalServiceRegistered(service.getUuid()))
	{
		BT_ERROR("GATT_SERVICE_ALREADY_REGISTERED", 0,
		              "Service %s is already registered",
		              service.getUuid().toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	BT_DEBUG("Starting to register service %s with %zu characteristics",
	              service.getUuid().toString().c_str(),
	              service.getCharacteristics().size());

	// NOTE: We need to add the service item before we call BSA_BleSeCreateService
	// as the corresponding event can be recieved while we're still in the call or
	// afterwards. If we receive it while still in the call and don't have the item
	// added yet we will never tell our caller the result of the operation.
	LocalService* newService = new LocalService;
	newService->desc = service; // TODO: Change to pointer assign
	newService->addServiceCallback = callback;

	auto addServiceCallback = [this, server, newService, adapterAddress](BluetoothError error, uint16_t serviceId) {

		if (error != BLUETOOTH_ERROR_NONE)
		{
			safe_callback(newService->addServiceCallback, error);
			delete newService;
			return;
		}

		BT_INFO("BLE", 0, "add serviceId:%d complete\n", serviceId);
		newService->id = serviceId;
		initCharacteristic(newService); // iterator
		addLocalCharacteristic(server, newService, adapterAddress);
	};
	BT_DEBUG("[%s](%d) getImpl->addService server:%d service:%s\n", __FUNCTION__, __LINE__, server->id, service.getUuid().toString().c_str());
	// TODO: Needs server_if, uuid, handles, primary
	getImpl<BluetoothGattProfile>(adapterAddress)->addService(server->id, service, addServiceCallback);
}

void BluetoothGattProfileService::initCharacteristic(LocalService* newService)
{
	newService->characteristics = newService->desc.getCharacteristics();
	newService->charIt = newService->characteristics.begin();
	newService->descriptors.clear();
	newService->descIt = newService->descriptors.end();
}

bool BluetoothGattProfileService::hasNext(LocalService* newService)
{
	// next descriptor
	if (newService->descIt == newService->descriptors.end())
	{
		newService->descriptors = (*newService->charIt).getDescriptors();
		newService->descIt = newService->descriptors.begin();
	}
	else
		++newService->descIt;

	// has descriptor
	if (newService->descIt != newService->descriptors.end())
	{
		return true;
	}

	// no more descriptor, next characteristic
	++newService->charIt;

	// has characteristic
	if (newService->charIt != newService->characteristics.end())
	{
		return true;
	}

	// no more characteristic
	newService->characteristics.clear();
	newService->descriptors.clear();
	return false;
}

void BluetoothGattProfileService::addLocalCharacteristic(LocalServer* server, LocalService* newService, const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) getImpl->addCharacteristic\n", __FUNCTION__, __LINE__);
	if (newService->descIt == newService->descriptors.end())
	{
		auto characteristic = *newService->charIt;
		getImpl<BluetoothGattProfile>(adapterAddress)->addCharacteristic(
				server->id,
				newService->id,
				characteristic,
				std::bind(&BluetoothGattProfileService::addCharacteristicCallback, this, server, newService, _1, _2, adapterAddress));
	}
	else
	{
		auto descriptor = *newService->descIt;
		getImpl<BluetoothGattProfile>(adapterAddress)->addDescriptor(
				server->id,
				newService->id,
				descriptor,
				std::bind(&BluetoothGattProfileService::addCharacteristicCallback, this, server, newService, _1, _2, adapterAddress));
	}
}

void BluetoothGattProfileService::addCharacteristicCallback(LocalServer* server, LocalService* newService, BluetoothError error, uint16_t charId,
	const std::string &adapterAddress)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto& service = newService->desc;

	if (error != BLUETOOTH_ERROR_NONE)
		return;

	BluetoothUuid itemUuid;
	BluetoothGattValue itemValue;

	if (newService->descIt != newService->descriptors.end())
	{
		itemUuid = LocalService::buildDescriptorKey((*newService->charIt).getUuid(),
				(*newService->descIt).getUuid());
		itemValue = (*newService->descIt).getValue();
	}
	else
	{
		itemUuid = (*newService->charIt).getUuid();
		itemValue = (*newService->charIt).getValue();
	}

	BT_DEBUG("Storing item %s with handle %d", itemUuid.toString().c_str(), charId);

	// To be able to process the characteristic/descriptor later we need to
	// store it's handle as that is the only way we can access it
	// once it is registered with the server.
	if (newService->descIt != newService->descriptors.end())
	{
		newService->desc.updateDescriptorValue((*newService->charIt).getUuid(), (*newService->descIt).getUuid(), itemValue);
		newService->desc.updateDescriptorHandle((*newService->charIt), (*newService->descIt), charId);
	}
	else
	{
		newService->desc.updateCharacteristicValue(itemUuid, itemValue);
		newService->desc.updateCharacteristicHandle((*newService->charIt), charId);
	}

	BT_DEBUG("Storing value for item %s of service %s:",
				  itemUuid.toString().c_str(), service.getUuid().toString().c_str());
	//BT_DEBUG(formatGattValue(itemValue).c_str());

	// We always override store values for new characteristics/descriptors here.
	// Generally that shouldn't happen but is the better way to do here.

	if (hasNext(newService))
	{
		addLocalCharacteristic(server, newService, adapterAddress);
		return;
	}

	auto callback = [this, server, newService](BluetoothError serviceError) {
		if (serviceError != BLUETOOTH_ERROR_NONE)
			return;

		BT_INFO("BLE", 0, "startService complete \n");
		server->addLocalService(newService);
		safe_callback(newService->addServiceCallback, serviceError);
	};

	getImpl<BluetoothGattProfile>(adapterAddress)->startService(server->id, newService->id, BluetoothGattTransportMode::GATT_TRANSPORT_MODE_LE_BR_EDR , callback);
}

bool BluetoothGattProfileService::removeLocalServer(uint16_t serverId, const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto server = findLocalServer(serverId);
	if (server == nullptr)
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found service item for server %d", serverId);
		return false;
	}

	for (auto serverIter : mLocalServer)
	{
		if(serverIter.second->id == serverId)
		{
			BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
			if(!getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication(server->id, ApplicationType::SERVER))
				server->removeAllLocalService();
			else
				delete server;

			mLocalServer.erase(serverIter.first);
			break;

		}
	}

	return true;
}

bool BluetoothGattProfileService::removeLocalService(uint16_t serverId, const BluetoothUuid &uuid,
	const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);

	auto server = findLocalServer(serverId);
	if (server == nullptr)
		return false;

	std::string adptAddress1 = mServerAdapterMap[server->id];
	if(adptAddress1.compare(adapterAddress) != 0)
		return false;

	auto service = server->findLocalService(uuid);
	if (service == nullptr)
		return false;

	auto callback = [this, server, uuid](BluetoothError error) {
		server->removeLocalService(uuid);
	};
	BT_DEBUG("[%s](%d) getImpl->removeService\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>(adapterAddress)->removeService(server->id, service->id, callback);

	return true;
}

bool BluetoothGattProfileService::removeLocalService(const BluetoothUuid &uuid, const std::string &adapterAddress)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for(auto serverIter : mLocalServer)
	{
		auto server = serverIter.second;
		if (server == nullptr)
			return false;

		auto service = serverIter.second->findLocalService(uuid);
		if (service == nullptr)
			continue;

		std::string adptAddress1 = mServerAdapterMap[server->id];
		if(adptAddress1.compare(adapterAddress) != 0)
			return false;

		auto callback = [this, server, uuid](BluetoothError error) {
			server->removeLocalService(uuid);
		};
		BT_DEBUG("[%s](%d) getImpl->removeService\n", __FUNCTION__, __LINE__);
		getImpl<BluetoothGattProfile>(adapterAddress)->removeService(serverIter.second->id, service->id, callback);
		return true;
	}
	return false;
}

BluetoothGattProfileService::LocalServer* BluetoothGattProfileService::findLocalServer(const BluetoothUuid &uuid)
{
	BT_DEBUG("[%s](%d) called server:%s \n", __FUNCTION__, __LINE__, uuid.toString().c_str());
	auto iterServer = mLocalServer.find(uuid);
	if (iterServer == mLocalServer.end())
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found service item for server %s",
					  uuid.toString().c_str());
		return nullptr;
	}
	BT_DEBUG("[%s](%d) find server %s\n", __FUNCTION__, __LINE__, uuid.toString().c_str());
	return iterServer->second;
}

BluetoothGattProfileService::LocalServer* BluetoothGattProfileService::findLocalServer(uint16_t serverId)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		if(iterServer.second->id == serverId)
		{
			BT_DEBUG("[%s](%d) find server id %d\n", __FUNCTION__, __LINE__, serverId);
			return iterServer.second;
		}
	}

	return nullptr;
}

BluetoothGattProfileService::LocalService* BluetoothGattProfileService::findLocalService(uint16_t serviceId)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		auto ret = iterServer.second->findLocalService(serviceId);
		if(ret != nullptr)
		{
			BT_DEBUG("[%s](%d) find service id %d\n", __FUNCTION__, __LINE__, serviceId);
			return ret;
		}
	}

	return nullptr;
}

BluetoothGattProfileService::LocalServer* BluetoothGattProfileService::findLocalServerByServiceId(uint16_t serviceId)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		for (auto iterService : iterServer.second->mLocalServices)
		{
			if(iterService.second->id == serviceId)
			{
				BT_DEBUG("[%s](%d) find server include service id %d\n", __FUNCTION__, __LINE__, serviceId);
				return iterServer.second;
			}
		}
	}

	return nullptr;
}

BluetoothGattProfileService::LocalService* BluetoothGattProfileService::findLocalServiceByCharId(uint16_t charId)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		for (auto iterService : iterServer.second->mLocalServices)
		{
			for (auto iterChar: iterService.second->desc.getCharacteristics())
			{
				if(iterChar.getHandle() == charId)
				{
					BT_DEBUG("[%s](%d) find service include characteristic id %d\n", __FUNCTION__, __LINE__, charId);
					return iterService.second;
				}
			}
		}
	}

	return nullptr;
}

BluetoothGattProfileService::LocalService* BluetoothGattProfileService::findLocalService(const BluetoothUuid &uuid)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		auto ret = iterServer.second->findLocalService(uuid);
		if(ret != nullptr)
		{
			BT_DEBUG("[%s](%d) find service %s\n", __FUNCTION__, __LINE__, uuid.toString().c_str());
			return ret;
		}
	}

	return nullptr;
}

bool BluetoothGattProfileService::getLocalCharacteristic(const uint16_t &handle, BluetoothGattCharacteristic &characteristic)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		if(iterServer.second->getLocalCharacteristic(handle, characteristic))
		{
			BT_INFO("BLE", 0, "[%s](%d) found characteristic %s\n", __FUNCTION__, __LINE__, characteristic.getUuid().toString().c_str());
			return true;
		}
	}
	return false;
}

bool BluetoothGattProfileService::getLocalDescriptor(const uint16_t &handle, BluetoothGattDescriptor &descriptor)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	for (auto iterServer : mLocalServer)
	{
		if(iterServer.second->getLocalDescriptor(handle, descriptor))
		{
			BT_INFO("BLE", 0, "[%s](%d) found descriptor %s\n", __FUNCTION__, __LINE__, descriptor.getUuid().toString().c_str());
			return true;
		}
	}
	return false;
}

// TODO: change
BluetoothGattService BluetoothGattProfileService::getLocalService(const std::string &serviceUuid)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	BluetoothGattService ret;
	for (auto iterServer : mLocalServer)
	{
		auto localService = iterServer.second-> findLocalService(serviceUuid);
		if (!localService)
			continue;
		else
		{
			BT_INFO("BLE", 0, "[%s](%d) found service %s\n", __FUNCTION__, __LINE__, serviceUuid.c_str());
			ret = localService->desc;
			break;
		}
	}
	return ret;
}

// TODO: change
BluetoothGattProfileService::LocalServer* BluetoothGattProfileService::getLocalServer(const std::string &serverUuid)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	if(serverUuid.size() > 0)
	{
		auto serverIter = mLocalServer.find(serverUuid);
		if (serverIter == mLocalServer.end())
			return nullptr;

		BT_INFO("BLE", 0, "[%s](%d) found server %s\n", __FUNCTION__, __LINE__, serverUuid.c_str());
		return serverIter->second;
	}
	else
	{
		auto serverIter = mLocalServer.begin();
		if (serverIter == mLocalServer.end())
			return nullptr;

		BT_INFO("BLE", 0, "[%s](%d) found server %s\n", __FUNCTION__, __LINE__, serverUuid.c_str());
		return serverIter->second;
	}
}

BluetoothGattServiceList BluetoothGattProfileService::getLocalServices()
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	BluetoothGattServiceList localServices;
	for (auto server : mLocalServer)
	{
		for (auto service : server.second->mLocalServices)
		{
			localServices.push_back(service.second->desc);
		}
	}

	return localServices;
}

void BluetoothGattProfileService::readLocalCharacteristics(
		const BluetoothUuid &service,
		const BluetoothUuidList &characteristics,
		BluetoothGattReadCharacteristicsCallback callback)
{
	BT_DEBUG("Reading local characteristic (count %zu) of service %s",
	              characteristics.size(),
	              service.toString().c_str());

	auto localService = findLocalService(service);
	if (!localService)
	{
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID, {});
		return;
	}

	auto availableCharacteristics = localService->desc.getCharacteristics();
	BluetoothGattCharacteristicList result;

	for (auto &currentCharacteristic : characteristics)
	{
		bool found = false;

		for (auto characteristic : availableCharacteristics)
		{
			if (characteristic.getUuid() == currentCharacteristic)
			{
				result.push_back(characteristic);
				found = true;
			}
		}

		if (!found)
		{
			safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID, {});
			return;
		}
	}

	safe_callback(callback, BLUETOOTH_ERROR_NONE, result);
}

void BluetoothGattProfileService::readLocalDescriptors(
		const BluetoothUuid& service,
		const BluetoothUuid &characteristic,
		const BluetoothUuidList &descriptors,
		BluetoothGattReadDescriptorsCallback callback)
{
	BT_DEBUG("service %s characteristic %s", service.toString().c_str(),
	              characteristic.toString().c_str());

	auto localService = findLocalService(service);
	if (!localService)
	{
		BT_ERROR("GATT_FAILED_TO_READ_DESC", 0, "Failed to read descriptors for characteristic %s from local service %s: unknown service",
		              characteristic.toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID, {});
		return;
	}

	if (!localService->hasCharacteristic(characteristic))
	{
		BT_ERROR("GATT_FAILED_TO_READ_DESC", 0, "Failed to read descriptors for characteristic %s from local service %s: unknown characteristic",
		              characteristic.toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID, {});
		return;
	}

	auto availableDescriptors = localService->desc.getCharacteristic(characteristic).getDescriptors();
	BluetoothGattDescriptorList result;

	for (auto &currentDescriptor : descriptors)
	{
		bool found = false;

		for (auto descriptor : availableDescriptors)
		{
			if (descriptor.getUuid() == currentDescriptor)
			{
				result.push_back(descriptor);
				found = true;
			}
		}

		if (!found)
		{
			safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID, {});
			return;
		}
	}

	safe_callback(callback, BLUETOOTH_ERROR_NONE, result);
}

void BluetoothGattProfileService::writeLocalCharacteristic(
		const BluetoothGattCharacteristic &characteristic,
		BluetoothResultCallback callback)
{
	// This will override the already stored value or if no one is
	// stored yet put in the new one.
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto localService = findLocalServiceByCharId(characteristic.getHandle());
	if (localService)
	{
		localService->desc.updateCharacteristicValue(characteristic.getUuid(), characteristic.getValue());
		safe_callback(callback, BLUETOOTH_ERROR_NONE);
		BT_DEBUG("[%s](%d) getImpl->notifyCharacteristicValueChanged\n", __FUNCTION__, __LINE__);
		getImpl<BluetoothGattProfile>()->notifyCharacteristicValueChanged(localService->id, characteristic, characteristic.getHandle());
		for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
		{
			auto subscriptionValue = it->second;

			bool foundCharacteristic = false;
			if (subscriptionValue.characteristicUuids.size() > 0)
			{
				for (auto it2 = subscriptionValue.characteristicUuids.begin(); it2 != subscriptionValue.characteristicUuids.end(); ++it2)
				{
					if ((*it2) == characteristic.getUuid())
					{
						foundCharacteristic = true;
						break;
					}
				}
			}
			else if (subscriptionValue.characteristicUuid == characteristic.getUuid())
			{
				foundCharacteristic = true;
			}

			if (!foundCharacteristic)
				continue;

			auto monitorCharacteristicsWatch = it->first;
			pbnjson::JValue responseObj = pbnjson::Object();
			responseObj.put("returnValue", true);
			responseObj.put("subscribed", true);
			responseObj.put("adapterAddress", getManager()->getAddress());

			pbnjson::JValue characteristicObj = pbnjson::Object();
			characteristicObj.put("characteristic", characteristic.getUuid().toString());

			pbnjson::JValue valueObj = pbnjson::Object();
			BluetoothGattValue values = characteristic.getValue();
			pbnjson::JValue bytesArray = pbnjson::Array();
			for (size_t i=0; i < values.size(); i++)
				bytesArray.append((int32_t) values[i]);
			valueObj.put("bytes", bytesArray);

			characteristicObj.put("value", valueObj);
			responseObj.put("changed", characteristicObj);

			LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);
		}
		return;
	}
	BT_ERROR("GATT_FAILED_TO_WRITE_CHAR", 0, "Failed to write local characteristic %s because the service isn't registered",
				  characteristic.getUuid().toString().c_str());
	safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
	return;
}

void BluetoothGattProfileService::writeLocalCharacteristic(
		const BluetoothUuid &service,
		const BluetoothGattCharacteristic &characteristic,
		BluetoothResultCallback callback)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto localService = findLocalService(service);
	if (!localService)
	{
		BT_ERROR("GATT_FAILED_TO_WRITE_CHAR", 0, "Failed to write local characteristic %s of service %s because the service isn't registered",
		              characteristic.getUuid().toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	BT_DEBUG("Writing local characteristic %s of service %s; value is",
	              characteristic.getUuid().toString().c_str(),
	              service.toString().c_str());
	//BT_DEBUG("%s", formatGattValue(characteristic.getValue()).c_str());

	if (!localService->hasCharacteristic(characteristic.getUuid()))
	{
		BT_ERROR("GATT_FAILED_TO_WRITE_CHAR", 0, "Failed to write local characteristic %s of service %s because it is not registered",
		              characteristic.getUuid().toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	// This will override the already stored value or if no one is
	// stored yet put in the new one.
	localService->desc.updateCharacteristicValue(characteristic.getUuid(), characteristic.getValue());

	safe_callback(callback, BLUETOOTH_ERROR_NONE);
	auto localServer = findLocalServerByServiceId(localService->id);
	BT_DEBUG("[%s](%d) getImpl->notifyCharacteristicValueChanged\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>()->notifyCharacteristicValueChanged(localServer->id, localService->id, characteristic, characteristic.getHandle());

	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		auto subscriptionValue = it->second;

		bool foundCharacteristic = false;
		if (subscriptionValue.characteristicUuids.size() > 0)
		{
			for (auto it2 = subscriptionValue.characteristicUuids.begin(); it2 != subscriptionValue.characteristicUuids.end(); ++it2)
			{
				if ((*it2) == characteristic.getUuid())
				{
					foundCharacteristic = true;
					break;
				}
			}
		}
		else if (subscriptionValue.characteristicUuid == characteristic.getUuid())
		{
			foundCharacteristic = true;
		}

		if (!foundCharacteristic)
			continue;

		auto monitorCharacteristicsWatch = it->first;
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("subscribed", true);
		responseObj.put("adapterAddress", getManager()->getAddress());

		pbnjson::JValue characteristicObj = pbnjson::Object();
		characteristicObj.put("characteristic", characteristic.getUuid().toString());

		pbnjson::JValue valueObj = pbnjson::Object();
		BluetoothGattValue values = characteristic.getValue();
		pbnjson::JValue bytesArray = pbnjson::Array();
		for (size_t i=0; i < values.size(); i++)
			bytesArray.append((int32_t) values[i]);
		valueObj.put("bytes", bytesArray);

		characteristicObj.put("value", valueObj);
		responseObj.put("changed", characteristicObj);

		LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);
	}
}

void BluetoothGattProfileService::writeLocalDescriptor(
		const BluetoothGattDescriptor &descriptor,
		BluetoothResultCallback callback)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	//todo: implement
	for (auto server : mLocalServer)
	{
		for (auto service : server.second->mLocalServices)
		{
			auto localService = service.second;
			if(localService->hasDescriptor(descriptor.getHandle()))
			{
				BluetoothGattCharacteristic characteristic = localService->getParentCharacteristic(descriptor.getHandle());
				localService->desc.updateDescriptorValue(characteristic.getUuid(), descriptor.getUuid(), descriptor.getValue());
				getImpl<BluetoothGattProfile>()->notifyDescriptorValueChanged(server.second->id, localService->id, descriptor.getHandle(), descriptor, characteristic.getHandle());
				safe_callback(callback, BLUETOOTH_ERROR_NONE);
			}
		}
	}
	BT_ERROR("GATT_FAILED_TO_READ_DESC", 0, "Failed to write descriptor %s: unknown descriptor",
				  descriptor.getUuid().toString().c_str());
	safe_callback(callback, BLUETOOTH_ERROR_FAIL);
}

void BluetoothGattProfileService::writeLocalDescriptor(
		const BluetoothUuid& service,
		const BluetoothUuid &characteristic,
		const BluetoothGattDescriptor &descriptor,
		BluetoothResultCallback callback)
{
	BT_DEBUG("[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto localService = findLocalService(service);
	if (!localService)
	{
		BT_ERROR("GATT_FAILED_TO_WRITE_DESC", 0, "Failed to write descriptor %s for characteristic %s from local service %s: unknown service",
		              descriptor.getUuid().toString().c_str(),
		              characteristic.toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	if (!localService->hasCharacteristic(characteristic))
	{
		BT_ERROR("GATT_FAILED_TO_WRITE_DESC", 0, "Failed to write descriptor %s for characteristic %s from local service %s: unknown characteristic",
		              descriptor.getUuid().toString().c_str(),
		              characteristic.toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_PARAM_INVALID);
		return;
	}

	if (!localService->hasDescriptor(descriptor.getHandle()))
	{
		BT_ERROR("GATT_FAILED_TO_READ_DESC", 0, "Failed to write descriptor %s for characteristic %s from local service %s: unknown descriptor",
		              descriptor.getUuid().toString().c_str(),
		              characteristic.toString().c_str(),
		              service.toString().c_str());
		safe_callback(callback, BLUETOOTH_ERROR_FAIL);
		return;
	}

	auto localServer = findLocalServerByServiceId(localService->id);
	if (localServer)
	{
		BluetoothGattCharacteristic gattCharacteristic = localService->getParentCharacteristic(descriptor.getHandle());
		getImpl<BluetoothGattProfile>()->notifyDescriptorValueChanged(localServer->id, localService->id, descriptor.getHandle(), descriptor, gattCharacteristic.getHandle());
	}
	localService->desc.updateDescriptorValue(characteristic, descriptor.getUuid(), descriptor.getValue());
	safe_callback(callback, BLUETOOTH_ERROR_NONE);
}

void BluetoothGattProfileService::characteristicValueReadRequested(uint32_t requestId, const std::string &address, uint16_t server_if, uint16_t charId)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto localService = findLocalServiceByCharId(charId);
	if (!localService)
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found service id %d to process read request from remote device %s",
				server_if, address.c_str());
		// FIXME unsure what status to send here. BSA API doesn't define any
		// so we send "1" until we know which one to send.
		getImpl<BluetoothGattProfile>()->characteristicValueReadResponse(requestId, BLUETOOTH_ERROR_FAIL, BluetoothGattValue());

		return;
	}

	BluetoothGattCharacteristic characteristic;
	if (!getLocalCharacteristic(charId, characteristic))
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found characteristic id %d to process read request from remote device %s",
				charId, address.c_str());
		getImpl<BluetoothGattProfile>()->characteristicValueReadResponse(requestId, BLUETOOTH_ERROR_FAIL, BluetoothGattValue());
		return;
	}

	BT_DEBUG("[%s](%d) getImpl<BluetoothGattProfile>()->characteristicValueReadResponse\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>()->characteristicValueReadResponse(requestId, BLUETOOTH_ERROR_NONE, characteristic.getValue());
}

void BluetoothGattProfileService::characteristicValueWriteRequested(uint32_t requestId, const std::string &address, uint16_t server_if, uint16_t charId, const BluetoothGattValue &value, bool response)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto localService = findLocalServiceByCharId(charId);
	if (!localService)
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found service id %d to process read request from remote device %s",
				server_if, address.c_str());
		// FIXME unsure what status to send here. BSA API doesn't define any
		// so we send "1" until we know which one to send.

		getImpl<BluetoothGattProfile>()->characteristicValueWriteResponse(requestId, BLUETOOTH_ERROR_FAIL, BluetoothGattValue());
		return;
	}

	BluetoothGattCharacteristic characteristic;
	if (!getLocalCharacteristic(charId, characteristic))
	{
		BT_ERROR("INVALID_STATE", 0, "Didn't found characteristic id %d to process read request from remote device %s",
				charId, address.c_str());
		getImpl<BluetoothGattProfile>()->characteristicValueWriteResponse(requestId, BLUETOOTH_ERROR_FAIL, BluetoothGattValue());
		return;
	}

	for (auto it = mMonitorCharacteristicSubscriptions.begin() ; it != mMonitorCharacteristicSubscriptions.end(); ++it)
	{
		auto subscriptionValue = it->second;
		auto tempService = findLocalService(subscriptionValue.serviceUuid);
		if (!tempService)
			continue;

		bool foundCharacteristic = false;
		for (auto it2 = subscriptionValue.characteristicUuids.begin(); it2 != subscriptionValue.characteristicUuids.end(); ++it2)
		{
			if ((*it2) == characteristic.getUuid())
			{
				foundCharacteristic = true;
				break;
			}
		}
		if (!foundCharacteristic)
			continue;

		auto monitorCharacteristicsWatch = it->first;
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("subscribed", true);
		responseObj.put("adapterAddress", getManager()->getAddress());
		responseObj.put("address", address);

		pbnjson::JValue characteristicObj = pbnjson::Object();
		characteristicObj.put("characteristic", characteristic.getUuid().toString());
		pbnjson::JValue valueObj = pbnjson::Object();
		BluetoothGattValue values = value;
		pbnjson::JValue bytesArray = pbnjson::Array();
		std::string stringValue = "";
		for (size_t i=0; i < values.size(); i++)
		{
			bytesArray.append((int32_t) values[i]);
			stringValue += std::to_string(values[i]) + (i < values.size()-1 ? ",":"");
		}

		valueObj.put("bytes", bytesArray);

		characteristicObj.put("value", valueObj);
		responseObj.put("changed", characteristicObj);

		BT_INFO("BLE", 0, "[%s](%d) characteristic %s value changed to %s\n", __FUNCTION__, __LINE__, characteristic.getUuid().toString().c_str(), stringValue.c_str());
		LSUtils::postToClient(monitorCharacteristicsWatch->getMessage(), responseObj);
	}

	if (response)
	{
		BT_DEBUG("[%s](%d) getImpl->characteristicValueWriteResponse\n", __FUNCTION__, __LINE__);
		getImpl<BluetoothGattProfile>()->characteristicValueWriteResponse(requestId, BLUETOOTH_ERROR_NONE, value);
	}

}

void BluetoothGattProfileService::handleConnectClientDisappeared(const uint16_t &clientId, const uint16_t &connectId, const std::string &adapterAddress, const std::string &address)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	auto watchIter = mConnectWatches.find(address);
	if (watchIter == mConnectWatches.end())
		return;

	if (!getImpl<BluetoothProfile>())
		return;

	auto disconnectCallback = [this, clientId, address, adapterAddress](BluetoothError error) {

		BT_INFO("BLE", 0, "[%s](%d) disconnect from device %s complete\n", __FUNCTION__, __LINE__, address.c_str());
		markDeviceAsNotConnected(adapterAddress,address);
		markDeviceAsNotConnecting(adapterAddress,address);
		mConnectedDevices.erase(clientId);
		removeSubscriptionPoint(adapterAddress, address);
		if (mConnectedDevicesMap.find(adapterAddress) != mConnectedDevicesMap.end())
			mConnectedDevicesMap[adapterAddress].erase(address);
	};

	BT_DEBUG("[%s](%d) getImpl->disconnectGatt\n", __FUNCTION__, __LINE__);
	if(clientId == 0 && connectId == 0)
		getImpl<BluetoothProfile>()->disconnect(address, disconnectCallback);
	else
		getImpl<BluetoothGattProfile>()->disconnectGatt(clientId, connectId, address, disconnectCallback);
}

uint16_t BluetoothGattProfileService::nextClientId()
{
	// FIXME care about upper limit of uint16_t
	static uint16_t nextId = 1;
	return nextId++;
}

bool BluetoothGattProfileService::isConnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(autoConnect, boolean), PROP(address, string), PROP(adapterAddress, string),
            PROP(subscribe, boolean)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		BT_INFO("BLE", 0, "[%s](%d) parseError %d\n", __FUNCTION__, __LINE__, parseError);
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

void BluetoothGattProfileService::connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	std::string address = convertToLower(requestObj["address"].asString());
	if (isDeviceConnected(adapterAddress, address))
	{
		uint16_t appId = getImpl<BluetoothGattProfile>(adapterAddress)->getAppId(address);
		uint16_t connectId = getImpl<BluetoothGattProfile>(adapterAddress)->getConnectId(address);

		if(appId > 0 && connectId > 0)
		{
			BT_INFO("BLE", 0, "[%s](%d) device %s already connected appId:%d connectId:%d\n", __FUNCTION__, __LINE__, address.c_str(), appId, connectId);
			bool subscribed = false;
			if (request.isSubscription())
			{
				auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
									std::bind(&BluetoothGattProfileService::handleConnectClientDisappeared, this, 0, 0, adapterAddress, address));

				mConnectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(address, watch));
				subscribed = true;
			}
			markDeviceAsConnected(adapterAddress,address);
			auto iterDevice = mConnectedDevices.find(appId);
			if(iterDevice != mConnectedDevices.end())
			{
				iterDevice->second->setAddress(address);
				iterDevice->second->setConnectId(connectId);
			}
			else
				mConnectedDevices.insert(std::pair<uint16_t, connectedDeviceInfo*>(appId, new connectedDeviceInfo(address, connectId)));

			pbnjson::JValue responseObj = pbnjson::Object();

			responseObj.put("subscribed", subscribed);
			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("address", address);
			responseObj.put("clientId", idToString(appId));

			LSUtils::postToClient(request, responseObj);
			return;
		}
	}

	BT_DEBUG("[%s](%d) getImpl->addApplication\n", __FUNCTION__, __LINE__);
	uint16_t appId = getImpl<BluetoothGattProfile>(adapterAddress)->addApplication(BluetoothUuid(std::to_string(nextClientId())), ApplicationType::CLIENT);
	if (appId == static_cast<uint16_t>(0))
	{
		BT_DEBUG("[%s](%d) add application failed\n", __FUNCTION__, __LINE__);
		LSUtils::respondWithError(request, BLUETOOTH_ERROR_FAIL, true);
		return;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	bool autoConnect = false;
	if(requestObj.hasKey("autoConnect"))
		autoConnect = requestObj["autoConnect"].asBool();

	auto isConnectedCallback = [this, requestMessage, appId, autoConnect, adapterAddress, address](BluetoothError connectedError, const BluetoothProperty &property) {
		LS::Message request(requestMessage);

		if (connectedError != BLUETOOTH_ERROR_NONE)
		{
			BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
			getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication(appId, ApplicationType::CLIENT);
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		bool connected = property.getValue<bool>();

		if (connected)
		{
			BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
			getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication(appId, ApplicationType::CLIENT);
			LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECTED);
			LSMessageUnref(request.get());
			return;
		}

		markDeviceAsConnecting(adapterAddress,address);
		notifyStatusSubscribers(adapterAddress, address, false);

		auto connectCallback = [this, requestMessage, appId, adapterAddress, address, autoConnect](BluetoothError error, uint16_t connectId) {
			LS::Message request(requestMessage);
			bool subscribed = false;

			if (error == BLUETOOTH_ERROR_UNSUPPORTED)
			{
				auto connectCallback = [this, requestMessage, appId, adapterAddress, address](BluetoothError connectError) {
					LS::Message request(requestMessage);
					bool subscribed = false;

					if (connectError != BLUETOOTH_ERROR_NONE)
					{
						BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
						getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication
											(appId, ApplicationType::CLIENT);
						LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
						LSMessageUnref(request.get());

						markDeviceAsNotConnecting(adapterAddress,address);
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
											std::bind(&BluetoothGattProfileService::handleConnectClientDisappeared, this, 0, 0, adapterAddress, address));

						mConnectWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(address, watch));
						subscribed = true;
					}
					markDeviceAsConnected(adapterAddress,address);
					mConnectedDevices.insert(std::pair<uint16_t, connectedDeviceInfo*>(appId, new connectedDeviceInfo(address, 0)));

					pbnjson::JValue responseObj = pbnjson::Object();

					responseObj.put("subscribed", subscribed);
					responseObj.put("returnValue", true);
					responseObj.put("adapterAddress", adapterAddress);
					responseObj.put("address", address);
					responseObj.put("clientId", idToString(appId));

					BT_INFO("BLE", 0, "[%s](%d) device %s connected appId:%d \n", __FUNCTION__, __LINE__, address.c_str(), appId);
					LSUtils::postToClient(request, responseObj);

					// We're done with sending out the first response to the client so
					// no use anymore for the message object
					LSMessageUnref(request.get());
				};
				getImpl<BluetoothProfile>(adapterAddress)->connect(address, connectCallback);
				return;
			}

			if (error != BLUETOOTH_ERROR_NONE)
			{
				BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
				getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication(appId, ApplicationType::CLIENT);
				LSUtils::respondWithError(request, BT_ERR_PROFILE_CONNECT_FAIL);
				LSMessageUnref(request.get());

				markDeviceAsNotConnecting(adapterAddress,address);
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
				LS::SubscriptionPoint *subscriptionPoint = 0;
				GattConnSubsInfo* connSubsPtr = 0;
				auto subscriptionsIter = mConnectSubsMap.find(adapterAddress);
				if (subscriptionsIter == mConnectSubsMap.end())
				{
					std::map<std::string, std::pair<LS::SubscriptionPoint*, GattConnSubsInfo*>> subscriptions;
					subscriptionPoint = new LS::SubscriptionPoint;
					subscriptionPoint->setServiceHandle(getManager());
					connSubsPtr = new GattConnSubsInfo;
					connSubsPtr->init();
					subscriptions[address] = {subscriptionPoint, connSubsPtr};
					mConnectSubsMap[adapterAddress] = subscriptions;
				}
				else
				{
					auto subscriptionIter = (subscriptionsIter->second).find(address);
					if (subscriptionIter == (subscriptionsIter->second).end())
					{
						std::map<std::string, std::pair<LS::SubscriptionPoint*, GattConnSubsInfo*>> subscriptions;
						subscriptionPoint = new LS::SubscriptionPoint;
						subscriptionPoint->setServiceHandle(getManager());
						connSubsPtr = new GattConnSubsInfo;
						connSubsPtr->init();
						(subscriptionsIter->second)[address] = {subscriptionPoint, connSubsPtr};
					}
					else
					{
						subscriptionPoint = subscriptionIter->second.first;
					}
				}

				subscriptionPoint->subscribe(request);
				subscribed = true;
			}
			markDeviceAsConnected(adapterAddress,address);
			mConnectedDevices.insert(std::pair<uint16_t, connectedDeviceInfo*>
					(appId, new connectedDeviceInfo(address, connectId)));

			BT_INFO("BLE", 0, "[%s](%d) device %s connected appId:%d connectId:%d\n",
						__FUNCTION__, __LINE__, address.c_str(), appId, connectId);

			pbnjson::JValue responseObj = pbnjson::Object();

			responseObj.put("subscribed", subscribed);
			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("address", address);
			responseObj.put("clientId", idToString(appId));

			LSUtils::postToClient(request, responseObj);

			std::unordered_map<std::string, uint16_t> connMap;
			connMap[address] = appId;
			mConnectedDevicesMap[adapterAddress] = connMap;

			// We're done with sending out the first response to the client so
			// no use anymore for the message object
			LSMessageUnref(request.get());
		};
		BT_DEBUG("[%s](%d) getImpl->connectGatt\n", __FUNCTION__, __LINE__);
		getImpl<BluetoothGattProfile>(adapterAddress)->connectGatt(appId, autoConnect, address, connectCallback);
	};

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return;
	}
	// Before we start to connect with the device we have to make sure
	// we're not already connected with it.
	impl->getProperty(address, BluetoothProperty::Type::CONNECTED, isConnectedCallback);
}

bool BluetoothGattProfileService::isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;
	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(clientId, string), PROP(adapterAddress, string)) REQUIRED_1(clientId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		if(!requestObj.hasKey("clientId"))
			LSUtils::respondWithError(request, BT_ERR_CLIENTID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	return true;
}

void BluetoothGattProfileService::disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress)
{
	BT_INFO("BLE", 0, "[%s](%d) called\n", __FUNCTION__, __LINE__);
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	uint16_t appId = idToInt(requestObj["clientId"].asString());
	uint16_t connectId = 0;
	std::string deviceAddress;
	if(!getConnectId(appId, connectId, deviceAddress, adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return;
	}

	auto disconnectCallback = [this, requestMessage, appId, adapterAddress, deviceAddress](BluetoothError error) {
		LS::Message request(requestMessage);

		if (error != BLUETOOTH_ERROR_NONE)
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_DISCONNECT_FAIL);
			LSMessageUnref(request.get());
			return;
		}

		BT_INFO("BLE", 0, "[%s](%d) device %s disconnected appId:%d\n", __FUNCTION__, __LINE__, deviceAddress.c_str(), appId);
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		LSUtils::postToClient(request, responseObj);

		if (mConnectedDevicesMap.find(adapterAddress) != mConnectedDevicesMap.end())
			mConnectedDevicesMap[adapterAddress].erase(deviceAddress);

		BT_DEBUG("[%s](%d) getImpl->removeApplication\n", __FUNCTION__, __LINE__);
		getImpl<BluetoothGattProfile>(adapterAddress)->removeApplication(appId, ApplicationType::CLIENT);
		removeConnectWatchForDevice(adapterAddress, deviceAddress, true, false);
		removeSubscriptionPoint(adapterAddress, deviceAddress);
		mConnectedDevices.erase(appId);
		markDeviceAsNotConnected(adapterAddress,deviceAddress);
		markDeviceAsNotConnecting(adapterAddress,deviceAddress);
		LSMessageUnref(request.get());
	};

	BT_DEBUG("[%s](%d) getImpl->disconnectGatt\n", __FUNCTION__, __LINE__);
	getImpl<BluetoothGattProfile>(adapterAddress)->disconnectGatt
		(appId, connectId, deviceAddress, disconnectCallback);
}
