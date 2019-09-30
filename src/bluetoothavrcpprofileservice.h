// Copyright (c) 2015-2019 LG Electronics, Inc.
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


#ifndef BLUETOOTHAVRCPPROFILESERVICE_H
#define BLUETOOTHAVRCPPROFILESERVICE_H

#include <string>
#include <map>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>

#include "bluetoothprofileservice.h"

#define DELETE_OBJ(del_obj) if(del_obj) { delete del_obj; del_obj = 0; }

namespace pbnjson
{
	class JValue;
}

namespace LS
{
	class Message;
	class ServerStatus;
}

namespace LSUtils
{
	class ClientWatch;
}

class BluetoothAvrcpProfileService : public BluetoothProfileService, BluetoothAvrcpStatusObserver
{
public:
	BluetoothAvrcpProfileService(BluetoothManagerService *manager);
	~BluetoothAvrcpProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);
	bool awaitMediaMetaDataRequest(LSMessage &message);
	bool supplyMediaMetaData(LSMessage &message);
	bool awaitMediaPlayStatusRequest(LSMessage &message);
	bool supplyMediaPlayStatus(LSMessage &message);
	bool sendPassThroughCommand(LSMessage &message);
	bool getMediaMetaData(LSMessage &message);
	bool getMediaPlayStatus(LSMessage &message);
	bool getPlayerApplicationSettings(LSMessage &message);
	bool setPlayerApplicationSettings(LSMessage &message);
	bool setAbsoluteVolume(LSMessage &message);
	bool getRemoteVolume(LSMessage &message);
	bool receivePassThroughCommand(LSMessage &message);
	bool getSupportedNotificationEvents(LSMessage &message);
	bool getRemoteFeatures(LSMessage &message);

	void mediaMetaDataRequested(BluetoothAvrcpRequestId requestId, const std::string &address);
	void mediaPlayStatusRequested(BluetoothAvrcpRequestId requestId, const std::string &address);
	void mediaDataReceived(const BluetoothMediaMetaData &metaData, const std::string &address);
	void mediaPlayStatusReceived(const BluetoothMediaPlayStatus &playStatus, const std::string &address);
	void volumeChanged(int volume, const std::string &address);
	void passThroughCommandReceived(BluetoothAvrcpPassThroughKeyCode keyCode, BluetoothAvrcpPassThroughKeyStatus keyStatus, const std::string &address);
	void passThroughCommandReceived(BluetoothAvrcpPassThroughKeyCode keyCode, BluetoothAvrcpPassThroughKeyStatus keyStatus,
									const std::string &adapterAddress,const std::string &address);
	void remoteFeaturesReceived(BluetoothAvrcpRemoteFeatures features, const std::string &address, const std::string &role);
	void supportedNotificationEventsReceived(const BluetoothAvrcpSupportedNotificationEventList &events, const std::string &address);

	/*
	 * This will be deprecated on implementation of remoteFeaturesReceived with role.
	 */
	void remoteFeaturesReceived(BluetoothAvrcpRemoteFeatures features, const std::string &address);

private:
	class MediaRequest
	{
	public:
		MediaRequest() {};
		~MediaRequest() {};

		std::string requestId;
		std::string address;
	};

	bool prepareAwaitRequest(LS::Message &request, pbnjson::JValue &requestObj);
	void setMediaMetaDataRequestsAllowed(bool state);
	void setMediaPlayStatusRequestsAllowed(bool state);
	void assignRequestId(MediaRequest *request);
	void createMediaRequest(bool metaData, uint64_t requestId, const std::string &address);
	void deleteMediaRequestId(bool metaData, const std::string &requestIdStr);
	void deleteMediaRequest(bool metaData, const std::string &requestIdStr);
	BluetoothAvrcpRequestId findRequestId(bool metaData, const std::string &requestIdStr);
	uint64_t getRequestIndex(bool metaData, const std::string &requestIdStr);
	MediaRequest* findMediaRequest(bool metaData, const std::string &requestIdStr);
	void notifyConfirmationRequest(LS::Message &request, const std::string &requestId, const std::string &adapterAddress, bool success);
	void parseMediaMetaData(const pbnjson::JValue &dataObj, BluetoothMediaMetaData *data);
	void parseMediaPlayStatus(const pbnjson::JValue &dataObj, BluetoothMediaPlayStatus *status);
	void handleReceivePassThroughCommandClientDisappeared(const std::string &adapterAddress, const std::string &address);
	void removeReceivePassThroughCommandWatchForDevice(const std::string &address);
	void removeReceivePassThroughCommandWatchForDevice(const std::string &adapterAddress, const std::string &address);
	void handleGetSupportedNotificationEventsClientDisappeared(const std::string &adapterAddress, const std::string &address);
	void removeGetSupportedNotificationEventsWatchForDevice(const std::string &address);

	std::string mediaPlayStatusToString(BluetoothMediaPlayStatus::MediaPlayStatus status);
	BluetoothPlayerApplicationSettingsEqualizer equalizerStringToEnum(const std::string &equalizer);
	BluetoothPlayerApplicationSettingsRepeat repeatStringToEnum(const std::string &repeat);
	BluetoothPlayerApplicationSettingsShuffle shuffleStringToEnum(const std::string &shuffle);
	BluetoothPlayerApplicationSettingsScan scanStringToEnum(const std::string &scan);
	std::string equalizerEnumToString(BluetoothPlayerApplicationSettingsEqualizer equalizer);
	std::string repeatEnumToString(BluetoothPlayerApplicationSettingsRepeat repeat);
	std::string shuffleEnumToString(BluetoothPlayerApplicationSettingsShuffle shuffle);
	std::string scanEnumToString(BluetoothPlayerApplicationSettingsScan scan);
	void appendCurrentApplicationSettings(pbnjson::JValue &object);
	void notifySubscribersAboutApplicationSettings();
	void updateFromPlayerApplicationSettingsProperties(const BluetoothPlayerApplicationSettingsPropertiesList &properties);

	void handlePlayserApplicationSettingsPropertiesSet(BluetoothPlayerApplicationSettingsPropertiesList properties, LS::Message &request, std::string &adapterAddress, BluetoothError error);


private:
	std::string mEqualizer;
	std::string mRepeat;
	std::string mShuffle;
	std::string mScan;

	LSUtils::ClientWatch *mIncomingMediaMetaDataWatch;
	LSUtils::ClientWatch *mIncomingMediaPlayStatusWatch;
	bool mMediaMetaDataRequestsAllowed;
	bool mMediaPlayStatusRequestsAllowed;

	uint64_t mRequestIndex;
	uint32_t mNextRequestId;
	std::string mRemoteFeatures;
	std::string mCTRemoteFeatures;
	std::string mTGRemoteFeatures;
	std::string mRemoteFeaturesAddress;
	BluetoothMediaMetaData *mMediaMetaData;

	BluetoothAvrcpSupportedNotificationEventList mSupportedNotificationEvents;
	std::map<uint64_t, MediaRequest*> mMediaMetaDataRequests;
	std::map<uint64_t, MediaRequest*> mMediaPlayStatusRequests;
	std::map<uint64_t, BluetoothAvrcpRequestId> mMediaMetaDataRequestIds;
	std::map<uint64_t, BluetoothAvrcpRequestId> mMediaPlayStatusRequestIds;
	std::unordered_map<std::string, LSUtils::ClientWatch*> mReceivePassThroughCommandWatches;
	std::unordered_map<std::string, LSUtils::ClientWatch*> mGetSupportedNotificationEventsWatches;
	std::map<std::string, LS::SubscriptionPoint*> mGetRemoteVolumeSubscriptions;
	std::map<std::string, LS::SubscriptionPoint*> mGetMediaMetaDataSubscriptions;
	std::map<std::string, LS::SubscriptionPoint*> mGetMediaPlayStatusSubscriptions;

	LS::SubscriptionPoint mGetPlayerApplicationSettingsSubscriptions;
	std::map<std::string, std::map<std::string, LSUtils::ClientWatch*>> mReceivePassThroughCommandWatchesForMultipleAdapters;

};

#endif // BLUETOOTHAVRCPPROFILESERVICE_H
