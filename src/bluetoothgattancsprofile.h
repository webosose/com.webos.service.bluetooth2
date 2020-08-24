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


#ifndef BLUETOOTHGATTANCSPROFILE_H_
#define BLUETOOTHGATTANCSPROFILE_H_

#include "bluetoothgattprofileservice.h"
#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>

#define CONNECT_TIMEOUT 3
#define WRITE_TIMEOUT 1
#define ANCS_UUID                 "7905f431-b5ce-4e99-a40f-4b1e122d00d0"
#define NOTIFICATION_SOURCE_UUID  "9fbf120d-6301-42d9-8c58-25e699a21dbd"
#define CONTROL_POINT_UUID        "69d1d8f3-45e1-49a8-9821-9bbdfdaad9d9"
#define DATA_SOURCE_UUID          "22eac6e9-24d6-4bb5-be44-b36ace7c7bfb"

#define ANCS_STATUS_MIN_RESERVED_VALUE 3
#define ANCS_STATUS_MAX_RESERVED_VALUE 255

#define ANCS_FLAGS_MIN_RESERVED_VALUE 32
#define ANCS_FLAGS_MAX_RESERVED_VALUE 128

#define ANCS_CATEGORY_MIN_RESERVED_VALUE 12
#define ANCS_CATEGORY_MAX_RESERVED_VALUE 255

#define COMMAND_ID_GET_APP_ATTRIBUTES 1
#define COMMAND_ID_GET_NOTIFICATION_ATTRIBUTES 0
#define MESSAGE_TIMEOUT 30
#define MAX_UINT16 0xffff
#define MAX_CHAR 0xff

#define ANCS_RESERVED "Reserved"

#define COMMAND_ID_NOTIFICATION_ACTION 2
#define DELETE_OBJ(del_obj) if(del_obj) { delete del_obj; del_obj = 0; }
namespace LSUtils
{
        class ClientWatch;
}

class NotificationAttr {
public:
	uint8_t attrId;
	std::string value;
	bool found =false;
	NotificationAttr(uint8_t id) : attrId(id), found(false) {}
};

class NotificationIdQueryInfo
{
public:
	std::string deviceAddress;
	int32_t notificationId;
	std::vector <NotificationAttr> attrList;
	uint16_t readingAttr;
	uint16_t attrLenByte1;
	int remainingLen;
	time_t startTime;
	LSMessage *requestMessage;
} ;

class BluetoothGattAncsProfile: public BluetoothGattProfileService {

public:
	BluetoothGattAncsProfile(BluetoothManagerService *manager, BluetoothGattProfileService *btGattService);
	virtual ~BluetoothGattAncsProfile();
	void initialize(){};
	void initialize(BluetoothProfile *impl) { mImpl = impl;}


protected:
	virtual bool connectCallback(LSMessage *requestMessage, const std::string adapterAddress, std::string address, BluetoothError error);
	virtual void connectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	void disconnectCallback(LSMessage *requestMessage, const std::string adapterAddress, std::string address, bool quietDisconnect, BluetoothError error);
	void disconnectToStack(LS::Message &request, pbnjson::JValue &requestObj, const std::string &adapterAddress);
	pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
									std::string adapterAddress, std::string deviceAddress);

private:
	//LS2 methods
	bool advertise(LSMessage &message);
	bool awaitConnectionRequest(LSMessage &message);
	bool awaitNotifications(LSMessage &message);
	bool queryNotificationAttributes(LSMessage &message);
	bool performNotificationAction(LSMessage &message);
	bool queryAppAttributes(LSMessage &message);

	//internal methods
	bool isAncsServiceSupported(LSMessage *message, std::string adapterAddress, std::string address);

	void handleNotificationClientDisappeared(const std::string &adapterAddress, const std::string &address);

	//Observer callback
	void incomingLeConnectionRequest(const std::string &address, bool state);
	void characteristicValueChanged(const std::string &address, const BluetoothUuid &service, const BluetoothGattCharacteristic &characteristic, const std::string &adapterAddress);

private:
	LS::SubscriptionPoint mGetConnectionRequestSubscriptions;
	LS::SubscriptionPoint mQueryNotificationSubscription;
	std::unordered_map<std::string, LSUtils::ClientWatch*> mNotificationWatches;
	std::unordered_map<std::string, LS::SubscriptionPoint*> mAwaitNotificationSubscriptions;
	NotificationIdQueryInfo *mNotificationQueryInfo;
	BluetoothUuid mAncsUuid;
};

class  AncsServiceCheckTimeout{
public:
	BluetoothGattAncsProfile *serviceRef;
	LSMessage *requestMessage;
	std::string adapterAddress;
	std::string address;
	BluetoothGattCharacteristic characteristic;
};

#endif /* BLUETOOTHGATTANCSPROFILE_H_*/
