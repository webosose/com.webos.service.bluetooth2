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


#ifndef BLUETOOTHPBAPPROFILESERVICE_H
#define BLUETOOTHPBAPPROFILESERVICE_H

#include <string>
#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>
#include "bluetoothprofileservice.h"

class BluetoothPbapProfileService : public BluetoothProfileService,
                                    public BluetoothPbapStatusObserver
{
public:
	BluetoothPbapProfileService(BluetoothManagerService *manager);
	~BluetoothPbapProfileService();

	bool awaitAccessRequest(LSMessage &message);
	bool acceptAccessRequest(LSMessage &message);
	bool rejectAccessRequest(LSMessage &message);
	bool setPhoneBook(LSMessage &message);
	bool prepareSetPhoneBook(LS::Message &request, pbnjson::JValue &requestObj);
	bool getSize(LSMessage &message);
	bool prepareGetSize(LS::Message &request, pbnjson::JValue &requestObj);
	bool vCardListing(LSMessage &message);
	bool prepareVCardListing(LS::Message &request, pbnjson::JValue &requestObj);
	bool getPhoneBookProperties(LSMessage &message);
	bool prepareGetPhoneBookProperties(LS::Message &request, pbnjson::JValue &requestObj);
	bool getvCardFilters(LSMessage &message);
	bool prepareGetvCardFilters(LS::Message &request, pbnjson::JValue &requestObj);
	bool pullvCard(LSMessage &message);
	bool preparePullVcard(LS::Message &request, pbnjson::JValue &requestObj);
	void accessRequested(BluetoothPbapAccessRequestId accessRequestId, const std::string &address, const std::string &deviceName);
	void initialize();
	void initialize(const std::string &adapterAddress);
	void propertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPropertiesList properties);
	void propertiesChanged(const std::string &address, BluetoothPropertiesList properties);

private:
	class AccessRequest
	{
	public:
		AccessRequest() {};
		~AccessRequest() {};

		std::string requestId;
		std::string address;
		std::string name;
	};

	std::map<uint64_t, AccessRequest *> mAccessRequests;
	std::map<uint64_t, BluetoothPbapAccessRequestId> mAccessRequestIds;

	void setAccessRequestsAllowed(bool state);
	void notifyConfirmationRequest(LS::Message &request, const std::string &adapterAddress, bool success);
	void notifySetPhoneBookRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, bool success);
	void notifyGetSizeRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, uint16_t size, bool success);
	void notifyGetvCardFiltersRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, std::list<std::string> filters, bool success);
	pbnjson::JValue createJsonFilterList(std::list<std::string> filters);
	void createAccessRequest(BluetoothPbapAccessRequestId accessRequestId, const std::string &address, const std::string &deviceName);
	void notifyVCardListingRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, BluetoothPbapVCardList &list, bool success);
	pbnjson::JValue createJsonVCardListing(BluetoothPbapVCardList &list);
	void notifySubscribersAboutPropertiesChange(const std::string &adapterAddress, const std::string &address);
	void notifyGetPhoneBookPropertiesRequest (LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, bool subscribed, bool success);
	void notifyPullVcardRequest(LS::Message &request, BluetoothError error, const std::string &adapterAddress, const std::string &address, const std::string &destinationFile, bool success);
	void appendCurrentProperties(pbnjson::JValue &object);
	void assignAccessRequestId(AccessRequest *accessRequest);
	void notifyAccessRequestConfirmation(uint64_t requestId);
	void deleteAccessRequestId(const std::string &requestIdStr);
	void deleteAccessRequest(const std::string &requestId);
	void updateFromPbapProperties(BluetoothPbapApplicationParameters &applicationParams);
	void profilePropertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPbapApplicationParameters &properties);

	bool notifyAccessRequestListenerDropped();
	bool prepareConfirmationRequest(LS::Message &request, pbnjson::JValue &requestObj, bool accept);

	BluetoothPbapAccessRequestId findAccessRequestId(const std::string &requestIdStr);
	AccessRequest *findRequest(const std::string &requestIdStr);
	uint64_t getAccessRequestId(const std::string &requestIdStr);

	void setFolderObject(const std::string &object)
	{ this->mFolderObject = object; }
	std::string getFolderObject() const { return mFolderObject; }
	void setFolderRepository(const std::string &repository)
	{ this->mFolderRepository = repository; }
	std::string getFolderRepository() const { return mFolderRepository; }
	void initializePbapApplicationParameters();
	std::string buildStorageDirPath(const std::string &path, const std::string &address);

private:
	LSUtils::ClientWatch *mIncomingAccessRequestWatch;
	bool mAccessRequestsAllowed;
	uint64_t mRequestIndex;
	uint32_t mNextRequestId;
	LS::SubscriptionPoint mGetPropertiesSubscriptions;
	std::string mFolderObject;
	std::string mFolderRepository;
	BluetoothPbapApplicationParameters pbapApplicationParameters;

};

#endif // BLUETOOTHPBAPPROFILESERVICE_H
