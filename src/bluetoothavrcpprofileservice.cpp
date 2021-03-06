// Copyright (c) 2015-2021 LG Electronics, Inc.
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


#include "bluetoothavrcpprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetoothmanageradapter.h"
#include "bluetoothdevice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "bluetoothclientwatch.h"
#include <cmath>

using namespace std::placeholders;

#define BLUETOOTH_PROFILE_AVRCP_MAX_REQUEST_ID 999

BluetoothAvrcpProfileService::BluetoothAvrcpProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "AVRCP", "0000110c-0000-1000-8000-00805f9b34fb",
        "0000110e-0000-1000-8000-00805f9b34fb"),
	 mEqualizer("off"),
	 mRepeat("off"),
	 mShuffle("off"),
	 mScan("off"),
	 mIncomingMediaMetaDataWatch(NULL),
	 mIncomingMediaPlayStatusWatch(NULL),
	 mMediaMetaDataRequestsAllowed(false),
	 mMediaPlayStatusRequestsAllowed(false),
	 mRequestIndex(0),
	 mNextRequestId(1),
	 mMediaMetaData(0)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, awaitMediaMetaDataRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, supplyMediaMetaData)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, awaitMediaPlayStatusRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, supplyMediaPlayStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, sendPassThroughCommand)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getMediaMetaData)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getMediaPlayStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getPlayerApplicationSettings)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, setPlayerApplicationSettings)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, setAbsoluteVolume)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getRemoteVolume)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, receivePassThroughCommand)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getPlayerInfo)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, notifyMediaPlayStatus)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, internal)
		LS_CATEGORY_METHOD(enable)
		LS_CATEGORY_METHOD(disable)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getSupportedNotificationEvents)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getRemoteFeatures)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, browse)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getCurrentFolder)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getNumberOfItems)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, getFolderItems)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, changePath)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, playItem)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, addToNowPlaying)
		LS_CATEGORY_CLASS_METHOD(BluetoothAvrcpProfileService, search)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/avrcp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/avrcp", this);

	manager->registerCategory("/avrcp/internal", LS_CATEGORY_TABLE_NAME(internal), NULL, NULL);
	manager->setCategoryData("/avrcp/internal", this);

	manager->registerCategory("/avrcp/browse", LS_CATEGORY_TABLE_NAME(browse), NULL, NULL);
	manager->setCategoryData("/avrcp/browse", this);

	mSupportedNotificationEvents.clear();
}

BluetoothAvrcpProfileService::~BluetoothAvrcpProfileService()
{
	if (NULL != mIncomingMediaMetaDataWatch)
	{
		delete mIncomingMediaMetaDataWatch;
		mIncomingMediaMetaDataWatch = NULL;
	}

	if (NULL != mIncomingMediaPlayStatusWatch)
	{
		delete mIncomingMediaPlayStatusWatch;
		mIncomingMediaPlayStatusWatch = NULL;
	}

	for (auto itMap = mReceivePassThroughCommandWatches.begin(); itMap != mReceivePassThroughCommandWatches.end(); itMap++)
	{
		LSUtils::ClientWatch *clientWatch = itMap->second;
		if (NULL == clientWatch)
			continue;

		delete clientWatch;
	}
	mReceivePassThroughCommandWatches.clear();

	for (auto itMap = mGetSupportedNotificationEventsWatches.begin(); itMap != mGetSupportedNotificationEventsWatches.end(); itMap++)
	{
		LSUtils::ClientWatch *clientWatch = itMap->second;
		if (NULL == clientWatch)
			continue;

		delete clientWatch;
	}
	mGetSupportedNotificationEventsWatches.clear();

	for (auto iter = mNotificationEventsWatchesForMultipleAdapters.begin();
		 iter != mNotificationEventsWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mNotificationEventsWatchesForMultipleAdapters.clear();

	for (auto iter = mGetMediaMetaDataWatchesForMultipleAdapters.begin();
		 iter != mGetMediaMetaDataWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mGetMediaMetaDataWatchesForMultipleAdapters.clear();

	for (auto iter = mMediaPlayStatusWatchesForMultipleAdapters.begin();
		 iter != mMediaPlayStatusWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mMediaPlayStatusWatchesForMultipleAdapters.clear();

	for (auto iter = mPlayerApplicationSettingsWatchesForMultipleAdapters.begin();
		 iter != mPlayerApplicationSettingsWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mPlayerApplicationSettingsWatchesForMultipleAdapters.clear();

	for (auto iter = mReceivePassThroughCommandWatchesForMultipleAdapters.begin();
		 iter != mReceivePassThroughCommandWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mReceivePassThroughCommandWatchesForMultipleAdapters.clear();

	for (auto iter = mGetRemoteVolumeWatchesForMultipleAdapters.begin();
		 iter != mGetRemoteVolumeWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mGetRemoteVolumeWatchesForMultipleAdapters.clear();

	for (auto iter = mGetConnectedDevicesRemoteVolumeWatchesForMultipleAdapters.begin();
		 iter != mGetConnectedDevicesRemoteVolumeWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mGetConnectedDevicesRemoteVolumeWatchesForMultipleAdapters.clear();

	for (auto iter = mGetPlayerInfoWatchesForMultipleAdapters.begin();
		 iter != mGetPlayerInfoWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mGetPlayerInfoWatchesForMultipleAdapters.clear();

	for (auto iter = mGetCurrentFolderWatchesForMultipleAdapters.begin();
		 iter != mGetCurrentFolderWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mGetCurrentFolderWatchesForMultipleAdapters.clear();

	for (auto iter = mIncomingMediaPlayStatusWatchesForMultipleAdapters.begin();
		 iter != mIncomingMediaPlayStatusWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mIncomingMediaPlayStatusWatchesForMultipleAdapters.clear();

	for (auto iter = mIncomingMediaMetaDataWatchesForMultipleAdapters.begin();
		 iter != mIncomingMediaMetaDataWatchesForMultipleAdapters.end(); iter++)
	{
		delete (*iter);
	}
	mIncomingMediaMetaDataWatchesForMultipleAdapters.clear();
}

void BluetoothAvrcpProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothAvrcpProfile>()->registerObserver(this);
}

void BluetoothAvrcpProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothAvrcpProfile>(adapterAddress)->registerObserver(this);
}

void BluetoothAvrcpProfileService::propertiesChanged(const std::string &adapterAddress, const std::string &address, BluetoothPropertiesList properties)
{
	BluetoothProfileService::propertiesChanged(adapterAddress, address, properties);

	bool connected = false;

	for (auto prop : properties)
	{
		switch (prop.getType())
		{
			case BluetoothProperty::Type::CONNECTED:
				connected = prop.getValue<bool>();
				break;

			default:
				break;
		}
	}

	if(!connected)
	{
		auto it = mRemoteVolumes.find(address);
		if (it != mRemoteVolumes.end())
			mRemoteVolumes.erase(it);
		/* Clear the remote features supported for the device */
		clearRemoteFeatures(adapterAddress, address);
		clearPlayerInfo(adapterAddress, address);
		clearCurrentFolder(adapterAddress, address);
		clearPlayStatus(adapterAddress, address);
	}
}

void BluetoothAvrcpProfileService::clearRemoteFeatures(const std::string &adapterAddress, const std::string &address)
{
	std::vector<std::string> *remoteFeatures = NULL;
	remoteFeatures = findRemoteFeatures(adapterAddress, address, "CT");
	if (remoteFeatures)
	{
		remoteFeatures->clear();
		remoteFeatures = NULL;
	}
	remoteFeatures = findRemoteFeatures(adapterAddress, address, "TG");
	if (remoteFeatures)
	{
		remoteFeatures->clear();
		remoteFeatures = NULL;
	}
}

void BluetoothAvrcpProfileService::clearPlayerInfo(
	const std::string &adapterAddress, const std::string &address)
{
	auto playerInfoListIter = mPlayerInfoListForMultipleAdapters.find(adapterAddress);
	if (playerInfoListIter != mPlayerInfoListForMultipleAdapters.end())
	{
		(playerInfoListIter->second).erase(address);

		if ((playerInfoListIter->second).empty())
		{
			mPlayerInfoListForMultipleAdapters.erase(adapterAddress);
		}
	}
}

void BluetoothAvrcpProfileService::clearCurrentFolder(
	const std::string &adapterAddress, const std::string &address)
{
	auto listIter = mCurrentFolderForMultipleAdapters.find(adapterAddress);
	if (listIter != mCurrentFolderForMultipleAdapters.end())
	{
		(listIter->second).erase(address);

		if ((listIter->second).empty())
		{
			mCurrentFolderForMultipleAdapters.erase(adapterAddress);
		}
	}

}

void BluetoothAvrcpProfileService::clearPlayStatus(
	const std::string &adapterAddress, const std::string &address)
{
	auto listIter = mPlayStatusForMultipleAdapters.find(adapterAddress);
	if (listIter != mPlayStatusForMultipleAdapters.end())
	{
		(listIter->second).erase(address);

		if ((listIter->second).empty())
		{
			mPlayStatusForMultipleAdapters.erase(adapterAddress);
		}
	}
}

bool BluetoothAvrcpProfileService::awaitMediaMetaDataRequest(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareAwaitRequest(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothClientWatch *watch = getMediaRequestWatch(
		mIncomingMediaMetaDataWatchesForMultipleAdapters, adapterAddress);

	if (watch)
	{
		LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
		return true;
	}

	bool retVal = addClientWatch(request, &mIncomingMediaMetaDataWatchesForMultipleAdapters,
			adapterAddress, "");
	static_cast<void>(retVal);

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::supplyMediaMetaData(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(requestId, string),
		OBJECT(metaData, OBJSCHEMA_7(PROP(title, string), PROP(artist, string), PROP(album, string), PROP(genre, string),
			PROP(mediaNumber, integer), PROP(totalMediaCount, integer), PROP(duration, integer))),
		PROP(adapterAddress, string))
		REQUIRED_2(requestId, metaData));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("requestId"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_REQUESTID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothAvrcpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string requestIdStr = requestObj["requestId"].asString();
	MediaRequest *mediaRequest = findMediaRequest(true, requestIdStr, adapterAddress);
	BluetoothAvrcpRequestId requestId = findRequestId(true, requestIdStr, adapterAddress);
	if (!mediaRequest || BLUETOOTH_AVRCP_REQUEST_ID_INVALID == requestId)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_REQUESTID_NOT_EXIST);
		return true;
	}

	auto metaDataObj = requestObj["metaData"];
	BluetoothMediaMetaData metaData;
	parseMediaMetaData(metaDataObj, &metaData);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto requestCallback = [this, requestIdStr, requestMessage, adapterAddress](BluetoothError error)
	{
		LS::Message request(requestMessage);
		if (BLUETOOTH_ERROR_NONE != error)
			notifyConfirmationRequest(request, requestIdStr, adapterAddress, false);
		else
			notifyConfirmationRequest(request, requestIdStr, adapterAddress, true);
	};

	BT_INFO("AVRCP", 0, "Service calls SIL API : supplyMediaMetaData");
	getImpl<BluetoothAvrcpProfile>(adapterAddress)->supplyMediaMetaData(requestId, metaData, requestCallback);
	deleteMediaRequest(true, requestIdStr, adapterAddress);
	deleteMediaRequestId(true, requestIdStr, adapterAddress);

	return true;
}

BluetoothClientWatch *BluetoothAvrcpProfileService::getMediaRequestWatch(
	std::list<BluetoothClientWatch *> &clientWatches, const std::string &adapterAddress)
{
	for (auto watch : clientWatches)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			return watch;
		}
	}
	return NULL;
}

bool BluetoothAvrcpProfileService::awaitMediaPlayStatusRequest(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareAwaitRequest(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothClientWatch *watch = getMediaRequestWatch(
		mIncomingMediaPlayStatusWatchesForMultipleAdapters, adapterAddress);

	if (watch)
	{
		LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
		return true;
	}

	bool retVal = addClientWatch(request, &mIncomingMediaPlayStatusWatchesForMultipleAdapters,
			adapterAddress, "");
	static_cast<void>(retVal);

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::supplyMediaPlayStatus(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(requestId, string),
		OBJECT(playbackStatus, OBJSCHEMA_3(PROP(duration, integer), PROP(position, integer), PROP(status, string))),
		PROP(adapterAddress, string))
		REQUIRED_2(requestId, playbackStatus));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("requestId"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_REQUESTID_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto watch = getMediaRequestWatch(
			mIncomingMediaPlayStatusWatchesForMultipleAdapters, adapterAddress);
	if (!watch)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_REQUEST_NOT_ALLOWED);
		return true;
	}

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && getImpl<BluetoothAvrcpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string requestIdStr = requestObj["requestId"].asString();
	MediaRequest *mediaRequest = findMediaRequest(false, requestIdStr, adapterAddress);
	BluetoothAvrcpRequestId requestId = findRequestId(false, requestIdStr, adapterAddress);
	if (!mediaRequest || BLUETOOTH_AVRCP_REQUEST_ID_INVALID == requestId)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_REQUESTID_NOT_EXIST);
		return true;
	}

	auto playStatusObj = requestObj["playbackStatus"];
	BluetoothMediaPlayStatus playStatus;
	parseMediaPlayStatus(playStatusObj, &playStatus);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto requestCallback = [this, requestIdStr, requestMessage, adapterAddress](BluetoothError error)
	{
		BT_INFO("AVRCP", 0, "Return of supplyMediaPlayStatus is %d", error);

		LS::Message request(requestMessage);
		if (BLUETOOTH_ERROR_NONE != error)
			notifyConfirmationRequest(request, requestIdStr, adapterAddress, false);
		else
			notifyConfirmationRequest(request, requestIdStr, adapterAddress, true);
	};

	BT_INFO("AVRCP", 0, "Service calls SIL API : supplyMediaPlayStatus");
	getImpl<BluetoothAvrcpProfile>(adapterAddress)->supplyMediaPlayStatus(requestId, playStatus, requestCallback);
	deleteMediaRequest(false, requestIdStr, adapterAddress);
	deleteMediaRequestId(false, requestIdStr, adapterAddress);

	return true;
}

BluetoothAvrcpPassThroughKeyCode passThroughKeyCodeStringToKeyCode(const std::string str)
{
	BluetoothAvrcpPassThroughKeyCode keyCode = BluetoothAvrcpPassThroughKeyCode::KEY_CODE_UNKNOWN;

	if (str == "play")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PLAY;

	if (str == "pause")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PAUSE;

	if (str == "stop")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_STOP;

	if (str == "next")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_NEXT;

	if (str == "previous")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PREVIOUS;

	if (str == "fastForward")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_FAST_FORWARD;

	if (str == "rewind")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_REWIND;

	if (str == "volumeUp")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_VOLUME_UP;

	if (str == "volumeDown")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_VOLUME_DOWN;

	if (str == "mute")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_MUTE;

	if (str == "power")
		return BluetoothAvrcpPassThroughKeyCode::KEY_CODE_POWER;

	return keyCode;
}

std::string passThroughKeyCodeEnumToString(BluetoothAvrcpPassThroughKeyCode keycode)
{
	switch (keycode)
	{
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PLAY:
			return "play";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PAUSE:
			return "pause";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_STOP:
			return "stop";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_NEXT:
			return "next";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_PREVIOUS:
			return "previous";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_FAST_FORWARD:
			return "fastForward";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_REWIND:
			return "rewind";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_VOLUME_UP:
			return "volumeUp";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_VOLUME_DOWN:
			return "volumeDown";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_MUTE:
			return "mute";
		case BluetoothAvrcpPassThroughKeyCode::KEY_CODE_POWER:
			return "power";
		default:
			return "unknown";
	}
}

std::string passThroughKeyStatusEnumToString(BluetoothAvrcpPassThroughKeyStatus keyStatus)
{
	switch (keyStatus)
	{
		case BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_PRESSED:
			return "pressed";
		case BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_RELEASED:
			return "released";
		default:
			return "unknown";
	}
}

std::string remoteFeaturesEnumToString(BluetoothAvrcpRemoteFeatures remoteFeatures)
{
	switch (remoteFeatures)
	{
		case BluetoothAvrcpRemoteFeatures::FEATURE_NONE:
			return "none";
		case BluetoothAvrcpRemoteFeatures::FEATURE_METADATA:
			return "metaData";
		case BluetoothAvrcpRemoteFeatures::FEATURE_ABSOLUTE_VOLUME:
			return "absoluteVolume";
		case BluetoothAvrcpRemoteFeatures::FEATURE_BROWSE:
			return "browse";
		default:
			return "unknown";
	}
}

BluetoothAvrcpPassThroughKeyStatus passThroughKeyStatusStringToKeyStatus(const std::string str)
{
	BluetoothAvrcpPassThroughKeyStatus keyStatus = BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_UNKNOWN;

	if (str == "pressed")
		return BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_PRESSED;

	if (str == "released")
		return BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_RELEASED;

	return keyStatus;
}

bool BluetoothAvrcpProfileService::sendPassThroughCommand(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string),
		PROP(keyCode, string), PROP(keyStatus, string),PROP(adapterAddress, string))
		REQUIRED_3(address, keyCode, keyStatus));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("keyCode"))
				LSUtils::respondWithError(request, BT_ERR_AVRCP_KEY_CODE_PARAM_MISSING);
		else if (!requestObj.hasKey("keyStatus"))
				LSUtils::respondWithError(request, BT_ERR_AVRCP_KEY_STATUS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	std::string keyCodeStr;
	BluetoothAvrcpPassThroughKeyCode keyCode = BluetoothAvrcpPassThroughKeyCode::KEY_CODE_UNKNOWN;
	if (requestObj.hasKey("keyCode"))
	{
		keyCodeStr = requestObj["keyCode"].asString();
		keyCode = passThroughKeyCodeStringToKeyCode(keyCodeStr);
		if (keyCode == BluetoothAvrcpPassThroughKeyCode::KEY_CODE_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_KEY_CODE_INVALID_VALUE_PARAM);
			return true;
		}
	}

	std::string keyStatusStr;
	BluetoothAvrcpPassThroughKeyStatus keyStatus = BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_UNKNOWN;
	if (requestObj.hasKey("keyStatus"))
	{
		keyStatusStr = requestObj["keyStatus"].asString();
		keyStatus = passThroughKeyStatusStringToKeyStatus(keyStatusStr);
		if (keyStatus == BluetoothAvrcpPassThroughKeyStatus::KEY_STATUS_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_KEY_CODE_INVALID_VALUE_PARAM);
			return true;
		}
	}

	BT_INFO("AVRCP", 0, "Service calls SIL API : sendPassThroughCommand");
	BluetoothError error = getImpl<BluetoothAvrcpProfile>(adapterAddress)->
		sendPassThroughCommand(deviceAddress, keyCode, keyStatus);
	BT_INFO("AVRCP", 0, "Return of sendPassThroughCommand is %d", error);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_SEND_PASS_THROUGH_COMMAND_FAILED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::getMediaMetaData(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	bool subscribed =  false;

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mGetMediaMetaDataWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}

		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	if (mMediaMetaData)
	{
		pbnjson::JValue metaDataObj = pbnjson::Object();
		metaDataObj.put("title",  mMediaMetaData->getTitle());
		metaDataObj.put("artist", mMediaMetaData->getArtist());
		metaDataObj.put("album",  mMediaMetaData->getAlbum());
		metaDataObj.put("genre",  mMediaMetaData->getGenre());
		metaDataObj.put("trackNumber", (int32_t)mMediaMetaData->getTrackNumber());
		metaDataObj.put("trackCount", (int32_t)mMediaMetaData->getTrackCount());
		metaDataObj.put("duration", (int32_t)mMediaMetaData->getDuration());

		responseObj.put("metaData", metaDataObj);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}

void BluetoothAvrcpProfileService::appendCurrentApplicationSettings(pbnjson::JValue &object)
{
	object.put("equalizer", mEqualizer);
	object.put("repeat", mRepeat);
	object.put("shuffle", mShuffle);
	object.put("scan", mScan);
}

bool BluetoothAvrcpProfileService::getPlayerApplicationSettings(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;
	bool subscribed = false;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("address"))
                        LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mPlayerApplicationSettingsWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
		subscribed = true;
	}

	appendCurrentApplicationSettings(responseObj);

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);


	LSUtils::postToClient(request, responseObj);
	return true;
}

void BluetoothAvrcpProfileService::playerApplicationSettingsReceived(const BluetoothPlayerApplicationSettingsPropertiesList& properties,
	const std::string& adapterAddress, const std::string& address)
{
	bool changed = false;

	for (auto prop : properties)
	{
		switch (prop.getType())
		{
		case BluetoothPlayerApplicationSettingsProperty::Type::EQUALIZER:
			mEqualizer = equalizerEnumToString(prop.getValue<BluetoothPlayerApplicationSettingsEqualizer>());
			changed = true;
			break;
		case BluetoothPlayerApplicationSettingsProperty::Type::REPEAT:
			mRepeat = repeatEnumToString(prop.getValue<BluetoothPlayerApplicationSettingsRepeat>());
			changed = true;
			break;
		case BluetoothPlayerApplicationSettingsProperty::Type::SHUFFLE:
			mShuffle = shuffleEnumToString(prop.getValue<BluetoothPlayerApplicationSettingsShuffle>());
			changed = true;
			break;
		case BluetoothPlayerApplicationSettingsProperty::Type::SCAN:
			mScan = scanEnumToString(prop.getValue<BluetoothPlayerApplicationSettingsScan>());
			changed = true;
			break;
		default:
			break;
		}
	}

	if (changed)
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		appendCurrentApplicationSettings(responseObj);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", address);

		for (auto watch : mPlayerApplicationSettingsWatchesForMultipleAdapters)
		{
			if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
				convertToLower(address) == convertToLower(watch->getDeviceAddress()))
			{
				LSUtils::postToClient(watch->getMessage(), responseObj);
			}
		}
	}
}

void BluetoothAvrcpProfileService::handlePlayserApplicationSettingsPropertiesSet(BluetoothPlayerApplicationSettingsPropertiesList properties, LS::Message &request, std::string &adapterAddress, std::string &address, BluetoothError error)
{
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", address);

	LSUtils::postToClient(request, responseObj);
}

bool BluetoothAvrcpProfileService::setPlayerApplicationSettings(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	BluetoothPlayerApplicationSettingsPropertiesList propertiesToChange;
	int parseError = 0;

	const std::string schema =  STRICT_SCHEMA(PROPS_6(
                                    PROP(adapterAddress, string), PROP(address, string), PROP(equalizer, string),
                                    PROP(repeat, string), PROP(shuffle, string),
                                    PROP(scan, string)));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError == JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);
		else
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = convertToLower(requestObj["address"].asString());

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}
	if (!mImpl && !getImpl<BluetoothAvrcpProfile>(adapterAddress))
        {
                LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
                return true;
        }



	if (requestObj.hasKey("equalizer"))
	{
		std::string equalizerTo = requestObj["equalizer"].asString();
		BluetoothPlayerApplicationSettingsEqualizer equalizer;

		equalizer = equalizerStringToEnum(equalizerTo);
		if (equalizer == BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_EQUALIZER_INVALID_VALUE_PARAM);
			return true;
		}
		else
		{
			if (mEqualizer != equalizerTo)
			{
				propertiesToChange.push_back(BluetoothPlayerApplicationSettingsProperty(BluetoothPlayerApplicationSettingsProperty::Type::EQUALIZER, equalizer));
			}
		}
	}

	if (requestObj.hasKey("repeat"))
	{
		std::string repeatTo = requestObj["repeat"].asString();
		BluetoothPlayerApplicationSettingsRepeat repeat;

		repeat = repeatStringToEnum(repeatTo);
		if (repeat == BluetoothPlayerApplicationSettingsRepeat::REPEAT_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_REPEAT_INVALID_VALUE_PARAM);
			return true;
		}
		else
		{
			if (mRepeat != repeatTo)
			{
				propertiesToChange.push_back(BluetoothPlayerApplicationSettingsProperty(BluetoothPlayerApplicationSettingsProperty::Type::REPEAT, repeat));
			}
		}
	}

	if (requestObj.hasKey("shuffle"))
	{
		std::string shuffleTo = requestObj["shuffle"].asString();
		BluetoothPlayerApplicationSettingsShuffle shuffle;

		shuffle = shuffleStringToEnum(shuffleTo);
		if (shuffle == BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_SHUFFLE_INVALID_VALUE_PARAM);
			return true;
		}
		else
		{
			if (mShuffle != shuffleTo)
			{
				propertiesToChange.push_back(BluetoothPlayerApplicationSettingsProperty(BluetoothPlayerApplicationSettingsProperty::Type::SHUFFLE, shuffle));
			}
		}
	}

	if (requestObj.hasKey("scan"))
	{
		std::string scanTo = requestObj["scan"].asString();
		BluetoothPlayerApplicationSettingsScan scan;

		scan = scanStringToEnum(scanTo);
		if (scan == BluetoothPlayerApplicationSettingsScan::SCAN_UNKNOWN)
		{
			LSUtils::respondWithError(request, BT_ERR_AVRCP_SCAN_INVALID_VALUE_PARAM);
			return true;
		}
		else
		{
			if (mScan != scanTo)
			{
				propertiesToChange.push_back(BluetoothPlayerApplicationSettingsProperty(BluetoothPlayerApplicationSettingsProperty::Type::SCAN, scan));
			}
		}
	}

	// if we don't have any properties to set we can just respond to the caller
	if (propertiesToChange.size() == 0)
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);

		LSUtils::postToClient(request, responseObj);
	}
	else
	{
		BT_INFO("AVRCP", 0, "Service calls SIL API : setPlayerApplicationSettingsProperties");
		getImpl<BluetoothAvrcpProfile>(adapterAddress)->
			setPlayerApplicationSettingsProperties(propertiesToChange,
					std::bind(&BluetoothAvrcpProfileService::handlePlayserApplicationSettingsPropertiesSet,
						this, propertiesToChange, request, adapterAddress, deviceAddress, _1));
	}

	return true;
}

bool BluetoothAvrcpProfileService::setAbsoluteVolume(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	BluetoothPlayerApplicationSettingsPropertiesList propertiesToChange;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string),
		PROP(volume, integer), PROP(adapterAddress, string))
		REQUIRED_2(address, volume));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("volume"))
				LSUtils::respondWithError(request, BT_ERR_AVRCP_VOLUME_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return true;
	}

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();
		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	int32_t volume = 0;
	if (requestObj.hasKey("volume"))
	{
		volume = requestObj["volume"].asNumber<int32_t>();

		if (volume < 0 || volume > 100)
		{
				LSUtils::respondWithError(request, retrieveErrorText(BT_ERR_AVRCP_VOLUME_INVALID_VALUE_PARAM) + std::to_string(volume), BT_ERR_AVRCP_VOLUME_INVALID_VALUE_PARAM);
				return true;
		}
	}

	//convert volume from percentage to volume level
	volume = (int32_t)std::round((volume/100.0) * 127);
	BluetoothError error = getImpl<BluetoothAvrcpProfile>(adapterAddress)->setAbsoluteVolume(deviceAddress, volume);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_SET_ABSOLUTE_VOLUME_FAILED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::getRemoteVolume(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

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
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothAvrcpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = convertToLower(requestObj["address"].asString());

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	bool subscribed = false;

	if (requestObj.hasKey("address"))
	{
		BT_DEBUG("AVRCP: Address argument is given for getRemoteVolume : [%s : %d]", __FUNCTION__, __LINE__);
		if (request.isSubscription())
		{
			bool retVal = addClientWatch(request, &mGetRemoteVolumeWatchesForMultipleAdapters,
				adapterAddress, deviceAddress);
			if (!retVal)
			{
				LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
				return true;
			}

			subscribed = true;
		}

		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("subscribed", subscribed);
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);


		if (isDeviceConnected(adapterAddress, deviceAddress))
		{
			if (mRemoteVolumes.find(deviceAddress) != mRemoteVolumes.end())
				responseObj.put("volume", mRemoteVolumes[deviceAddress]);
		}

		LSUtils::postToClient(request, responseObj);
	}
	else
	{
		BT_DEBUG("AVRCP: Address argument is not given for getRemoteVolume : [%s : %d]", __FUNCTION__, __LINE__);

		bool isAnyDeviceConnected = false;

		if (request.isSubscription())
		{
			bool retVal = addClientWatch(request, &mGetConnectedDevicesRemoteVolumeWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
			if (!retVal)
			{
				LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
				return true;
			}

			subscribed = true;
		}

		pbnjson::JValue connectedDeviceResponseObj = pbnjson::Object();

		auto adapterInfo = getManager()->findAdapterInfo(adapterAddress);
		for (auto deviceIter : adapterInfo->getDevices())
		{
			auto device = deviceIter.second;

			if (isDeviceConnected(adapterAddress, device->getAddress()))
			{
				BT_DEBUG("AVRCP: Found devices with AVRCP connected : [%s : %d] [%s]", __FUNCTION__, __LINE__, device->getName().c_str());
				connectedDeviceResponseObj.put("returnValue", true);
				connectedDeviceResponseObj.put("subscribed", subscribed);
				connectedDeviceResponseObj.put("adapterAddress", adapterAddress);
				connectedDeviceResponseObj.put("address", device->getAddress());
				connectedDeviceResponseObj.put("volume", mRemoteVolumes[device->getAddress()]);
				LSUtils::postToClient(request, connectedDeviceResponseObj);
				isAnyDeviceConnected = true;
			}
		}

		// No device is connected.
		if (!isAnyDeviceConnected)
		{
			BT_DEBUG("AVRCP: There is no AVRCP connected device : [%s : %d]", __FUNCTION__, __LINE__);
			LSUtils::respondWithError(request, BT_ERR_AVRCP_NO_CONNECTED_DEVICES);
			return true;
		}
	}

	return true;
}

bool BluetoothAvrcpProfileService::getMediaPlayStatus(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}
	bool subscribed =  false;

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mMediaPlayStatusWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	auto adapterIter = mPlayStatusForMultipleAdapters.find(adapterAddress);
	if (adapterIter != mPlayStatusForMultipleAdapters.end())
	{
		auto deviceIter = (adapterIter->second).find(deviceAddress);
		if (deviceIter != (adapterIter->second).end())
		{
			BluetoothMediaPlayStatus playStatus = deviceIter->second;
			pbnjson::JValue playStatusObj = pbnjson::Object();
			playStatusObj.put("duration", (int32_t)playStatus.getDuration());
			playStatusObj.put("position", (int32_t)playStatus.getPosition());
			playStatusObj.put("status", mediaPlayStatusToString(playStatus.getStatus()));

			responseObj.put("playbackStatus", playStatusObj);
		}
	}

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::receivePassThroughCommand(LSMessage& message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
		PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

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
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;


	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return true;
	}

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = requestObj["address"].asString();

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	bool subscribed = false;

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mReceivePassThroughCommandWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::addClientWatch(LS::Message& request, std::list<BluetoothClientWatch*>* clientWatch,
	std::string adapterAddress, std::string deviceAddress)
{
	const char* senderName = LSMessageGetApplicationID(request.get());
	if (senderName == NULL)
	{
		senderName = LSMessageGetSenderServiceName(request.get());
		if (senderName == NULL)
		{
			return false;
		}
	}
	auto watch = new BluetoothClientWatch(getManager()->get(), request.get(),
		std::bind(&BluetoothAvrcpProfileService::handleClientDisappeared,
			this, clientWatch, senderName), adapterAddress, deviceAddress);
	clientWatch->push_back(watch);
	return true;
}

bool BluetoothAvrcpProfileService::getSupportedNotificationEvents(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
                                                PROP_WITH_VAL_1(subscribe, boolean, true))REQUIRED_1(subscribe));

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
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = convertToLower(requestObj["address"].asString());

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	if (!mImpl && !getImpl<BluetoothAvrcpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	bool subscribed = false;

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mNotificationEventsWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	pbnjson::JValue supportedNotificationEventsObj = pbnjson::Array();
	for (auto supportedNotificationEvent : mSupportedNotificationEvents)
	{
		supportedNotificationEventsObj.append((int32_t)supportedNotificationEvent);
	}
	responseObj.put("supportedNotificationEvents", supportedNotificationEventsObj);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::getRemoteFeatures(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(address, string), PROP(adapterAddress, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_A2DP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	if (requestObj.hasKey("address"))
	{
		deviceAddress = convertToLower(requestObj["address"].asString());

		if (!isDeviceConnected(adapterAddress, deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("returnValue", true);
	responseObj.put("address", deviceAddress);

	pbnjson::JValue remoteFeatureListObjCT = pbnjson::Array();
	pbnjson::JValue remoteFeatureListObjTG= pbnjson::Array();
	pbnjson::JValue remoteFeatureListObj= pbnjson::Array();

	std::vector<std::string> *remoteFeaturesCT =
		findRemoteFeatures(adapterAddress, deviceAddress, "CT");
	if (remoteFeaturesCT)
	{
		for (auto featuresIter : *remoteFeaturesCT)
		{
			pbnjson::JValue remoteFeatureObj = pbnjson::Object();
			remoteFeatureObj.put("remoteFeature", featuresIter);
			remoteFeatureObj.put("role", "CT");
			remoteFeatureListObj.append(remoteFeatureObj);
		}
	}

	std::vector<std::string> *remoteFeaturesTG =
		findRemoteFeatures(adapterAddress, deviceAddress, "TG");
	if (remoteFeaturesTG)
	{
		for (auto featuresIter : *remoteFeaturesTG)
		{
			pbnjson::JValue remoteFeatureObj = pbnjson::Object();
			remoteFeatureObj.put("remoteFeature", featuresIter);
			remoteFeatureObj.put("role", "TG");
			remoteFeatureListObj.append(remoteFeatureObj);
		}
	}
	responseObj.put("remoteFeatures", remoteFeatureListObj);

	LSUtils::postToClient(request, responseObj);

	return true;
}

void BluetoothAvrcpProfileService::remoteFeaturesReceived(BluetoothAvrcpRemoteFeatures features,
		const std::string &adapterAddress, const std::string &address, const std::string &role)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);
	std::map<std::string, std::map<std::string, std::vector<std::string>>> *remoteFeaturesSubscription;

	/* when remote device is TG */
	if ("CT" == role)
	{
		remoteFeaturesSubscription = &mTGRemoteFeturesForMultipleAdapters;
	}
	/* when remote device is CT*/
	else
	{
		remoteFeaturesSubscription = &mCTRemoteFeturesForMultipleAdapters;
	}
	auto remoteFeaturesIter = remoteFeaturesSubscription->find(adapterAddress);
        if (remoteFeaturesIter == remoteFeaturesSubscription->end())
        {
                std::map<std::string, std::vector<std::string>> remoteFeatures;
                std::vector<std::string> featureString;
                featureString.push_back(remoteFeaturesEnumToString(features));
                remoteFeatures.insert(std::pair<std::string, std::vector<std::string>>(address, featureString));
                remoteFeaturesSubscription->insert(std::pair<std::string,
                                std::map<std::string, std::vector<std::string>>>(adapterAddress, remoteFeatures));
        }
        else
        {
                auto remoteFeatureIter = (remoteFeaturesIter->second).find(address);
                if (remoteFeatureIter == (remoteFeaturesIter->second).end())
                {
                        std::map<std::string, std::vector<std::string>> remoteFeatures;
                        std::vector<std::string> featureString;
                        featureString.push_back(remoteFeaturesEnumToString(features));
                        (remoteFeaturesIter->second).insert(std::pair<std::string, std::vector<std::string>>(address, featureString));
                }
                else
                {

                        (remoteFeatureIter->second).push_back(remoteFeaturesEnumToString(features));
                }
        }

}

std::vector<std::string>* BluetoothAvrcpProfileService::findRemoteFeatures(const std::string &adapterAddress, const std::string &address,
	std::string role)
{
	std::map<std::string, std::map<std::string, std::vector<std::string>>> *remoteFeatures;
	std::vector<std::string> *features = NULL;
	if ("CT" == role)
	{
		remoteFeatures = &mCTRemoteFeturesForMultipleAdapters;
	}
	else
	{
		remoteFeatures = &mTGRemoteFeturesForMultipleAdapters;
	}
	auto remoteFeaturesIter = remoteFeatures->find(adapterAddress);
	if (remoteFeaturesIter != remoteFeatures->end())
	{
		auto remoteFeatureIter = (remoteFeaturesIter->second).find(address);
		if (remoteFeatureIter != (remoteFeaturesIter->second).end())
		{
			features = &remoteFeatureIter->second;
		}
	}
	return features;
}

void BluetoothAvrcpProfileService::removeClientWatch(std::list<BluetoothClientWatch*> *clientWatch, const std::string& senderName)
{
	auto watch = clientWatch->begin();
	while (watch != clientWatch->end())
	{

		const char* senderNameWatch = LSMessageGetApplicationID((*watch)->getMessage());
		if (senderNameWatch == NULL)
		{
			senderNameWatch = LSMessageGetSenderServiceName((*watch)->getMessage());
			if (senderNameWatch == NULL)
			{
				return;
			}
		}

		if (senderName == senderNameWatch)
		{
			auto watchToRemove = watch;
			delete (*watchToRemove);
			watch = clientWatch->erase(watch);
		}
		else
		{
			++watch;
		}
	}
}

void BluetoothAvrcpProfileService::removeReceivePassThroughCommandWatchForDevice(const std::string &address)
{
	auto watchIter = mReceivePassThroughCommandWatches.find(address);
	if (watchIter == mReceivePassThroughCommandWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", false);

	responseObj.put("adapterAddress", getManager()->getAddress());
	responseObj.put("address", address);

	LSUtils::postToClient(watch->getMessage(), responseObj);

	mReceivePassThroughCommandWatches.erase(watchIter);
	delete watch;
}

void BluetoothAvrcpProfileService::handleClientDisappeared(std::list<BluetoothClientWatch*>* clientWatch, const std::string senderName)
{
	removeClientWatch(clientWatch, senderName);
}

void BluetoothAvrcpProfileService::removeGetSupportedNotificationEventsWatchForDevice(const std::string &address)
{
	auto watchIter = mGetSupportedNotificationEventsWatches.find(address);
	if (watchIter == mGetSupportedNotificationEventsWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", false);

	responseObj.put("adapterAddress", getManager()->getAddress());
	responseObj.put("address", address);

	LSUtils::postToClient(watch->getMessage(), responseObj);

	mGetSupportedNotificationEventsWatches.erase(watchIter);
	delete watch;
}

void BluetoothAvrcpProfileService::mediaMetaDataRequested(BluetoothAvrcpRequestId requestId, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	createMediaRequest(true, requestId, address);
}

void BluetoothAvrcpProfileService::mediaMetaDataRequested(
	BluetoothAvrcpRequestId requestId, const std::string &adapterAddress,
	const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called multiAdapter: [%s : %d]", __FUNCTION__, __LINE__);

	createMediaRequest(true, requestId, adapterAddress, address);
}

void BluetoothAvrcpProfileService::mediaPlayStatusRequested(BluetoothAvrcpRequestId requestId, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	createMediaRequest(false, requestId, address);
}

void BluetoothAvrcpProfileService::mediaPlayStatusRequested(
	BluetoothAvrcpRequestId requestId, const std::string &adapterAddress,
	const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called multiAdapter: [%s : %d]", __FUNCTION__, __LINE__);

	createMediaRequest(false, requestId, adapterAddress, address);
}

void BluetoothAvrcpProfileService::mediaDataReceived(const BluetoothMediaMetaData& metaData, const std::string& address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	/*
	 * To avoid first lost media meta data is mMediaMetaData reason for being.
	 * If there is the subscription, we do not need to store mMediaMetaData.
	 */

	if (mMediaMetaData)
		DELETE_OBJ(mMediaMetaData);

	mMediaMetaData = new BluetoothMediaMetaData();
	if (NULL == mMediaMetaData)
		return;

	mMediaMetaData->setTitle(metaData.getTitle());
	mMediaMetaData->setArtist(metaData.getArtist());
	mMediaMetaData->setAlbum(metaData.getAlbum());
	mMediaMetaData->setGenre(metaData.getGenre());
	mMediaMetaData->setTrackNumber(metaData.getTrackNumber());
	mMediaMetaData->setTrackCount(metaData.getTrackCount());
	mMediaMetaData->setDuration(metaData.getDuration());

	auto subscriptionIter = mGetMediaMetaDataSubscriptions.find(address);
	if (subscriptionIter == mGetMediaMetaDataSubscriptions.end())
		return;

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());


	pbnjson::JValue metaDataObj = pbnjson::Object();
	metaDataObj.put("title", metaData.getTitle());
	metaDataObj.put("artist", metaData.getArtist());
	metaDataObj.put("album", metaData.getAlbum());
	metaDataObj.put("genre", metaData.getGenre());
	metaDataObj.put("trackNumber", (int32_t)metaData.getTrackNumber());
	metaDataObj.put("trackCount", (int32_t)metaData.getTrackCount());
	metaDataObj.put("duration", (int32_t)metaData.getDuration());

	object.put("metaData", metaDataObj);

	LS::SubscriptionPoint* subscriptionPoint = subscriptionIter->second;
	LSUtils::postToSubscriptionPoint(subscriptionPoint, object);

}

void BluetoothAvrcpProfileService::mediaPlayStatusReceived(const BluetoothMediaPlayStatus& playStatus, const std::string& address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	auto subscriptionIter = mGetMediaPlayStatusSubscriptions.find(address);
	if (subscriptionIter == mGetMediaPlayStatusSubscriptions.end())
		return;

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());

	pbnjson::JValue playStatusObj = pbnjson::Object();
	playStatusObj.put("duration", (int32_t)playStatus.getDuration());
	playStatusObj.put("position", (int32_t)playStatus.getPosition());
	playStatusObj.put("status", mediaPlayStatusToString(playStatus.getStatus()));

	object.put("playbackStatus", playStatusObj);

	LS::SubscriptionPoint* subscriptionPoint = subscriptionIter->second;
	LSUtils::postToSubscriptionPoint(subscriptionPoint, object);
}

void BluetoothAvrcpProfileService::mediaDataReceived(const BluetoothMediaMetaData &metaData, const std::string  &adapterAddress,
	const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	/*
	 * To avoid first lost media meta data is mMediaMetaData reason for being.
	 * If there is the subscription, we do not need to store mMediaMetaData.
	 */

	if (mMediaMetaData)
		DELETE_OBJ(mMediaMetaData);

	mMediaMetaData = new BluetoothMediaMetaData();
	if (NULL == mMediaMetaData)
		return;

	mMediaMetaData->setTitle(metaData.getTitle());
	mMediaMetaData->setArtist(metaData.getArtist());
	mMediaMetaData->setAlbum(metaData.getAlbum());
	mMediaMetaData->setGenre(metaData.getGenre());
	mMediaMetaData->setTrackNumber(metaData.getTrackNumber());
	mMediaMetaData->setTrackCount(metaData.getTrackCount());
	mMediaMetaData->setDuration(metaData.getDuration());

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", adapterAddress);


	pbnjson::JValue metaDataObj = pbnjson::Object();
	metaDataObj.put("title", metaData.getTitle());
	metaDataObj.put("artist", metaData.getArtist());
	metaDataObj.put("album", metaData.getAlbum());
	metaDataObj.put("genre", metaData.getGenre());
	metaDataObj.put("trackNumber", (int32_t)metaData.getTrackNumber());
	metaDataObj.put("trackCount", (int32_t)metaData.getTrackCount());
	metaDataObj.put("duration", (int32_t)metaData.getDuration());

	object.put("metaData", metaDataObj);

	for (auto watch : mGetMediaMetaDataWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothAvrcpProfileService::mediaPlayStatusReceived(const BluetoothMediaPlayStatus &playStatus, const std::string &adapterAddress,
	const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	auto adapterIter = mPlayStatusForMultipleAdapters.find(adapterAddress);
	if (adapterIter == mPlayStatusForMultipleAdapters.end())
	{
		std::map<std::string, BluetoothMediaPlayStatus> playStatusDevice;
		playStatusDevice.insert(
			std::pair<std::string, BluetoothMediaPlayStatus>(address, playStatus));
		mPlayStatusForMultipleAdapters.insert(
			std::pair<std::string,
					  std::map<std::string, BluetoothMediaPlayStatus> >(adapterAddress, playStatusDevice));
	}
	else
	{
		auto deviceIter = (adapterIter->second).find(address);
		if (deviceIter == (adapterIter->second).end())
		{
			(adapterIter->second).insert(std::pair<std::string, BluetoothMediaPlayStatus>(address, playStatus));
		}
		else
		{
			deviceIter->second = playStatus;
		}
	}
	for (auto watch : mMediaPlayStatusWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			pbnjson::JValue object = pbnjson::Object();

			object.put("returnValue", true);
			object.put("subscribed", true);
			object.put("address", address);
			object.put("adapterAddress", adapterAddress);

			pbnjson::JValue playStatusObj = pbnjson::Object();
			playStatusObj.put("duration", (int32_t)playStatus.getDuration());
			playStatusObj.put("position", (int32_t)playStatus.getPosition());
			playStatusObj.put("status", mediaPlayStatusToString(playStatus.getStatus()));

			object.put("playbackStatus", playStatusObj);
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothAvrcpProfileService::volumeChanged(int volume, const std::string &adapterAddress ,const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	//Convert to percentage as per API documentation
	volume = std::round((volume/127.0) * 100);

	if (mRemoteVolumes.find(address) != mRemoteVolumes.end())
		mRemoteVolumes[address] = volume;
	else
		mRemoteVolumes.insert(std::pair<std::string, int>(address, volume));

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", adapterAddress);
	object.put("volume", volume);

	for (auto watch : mGetRemoteVolumeWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}

	for (auto watch : mGetConnectedDevicesRemoteVolumeWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothAvrcpProfileService::passThroughCommandReceived(BluetoothAvrcpPassThroughKeyCode keyCode, BluetoothAvrcpPassThroughKeyStatus keyStatus, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());
	object.put("keyCode", passThroughKeyCodeEnumToString(keyCode));
	object.put("keyStatus", passThroughKeyStatusEnumToString(keyStatus));

	auto watchIter = mReceivePassThroughCommandWatches.find(address);
	if (watchIter == mReceivePassThroughCommandWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	LSUtils::postToClient(watch->getMessage(), object);
}

void BluetoothAvrcpProfileService::passThroughCommandReceived(BluetoothAvrcpPassThroughKeyCode keyCode, BluetoothAvrcpPassThroughKeyStatus keyStatus,
															  const std::string &adapterAddress, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", adapterAddress);
	object.put("keyCode", passThroughKeyCodeEnumToString(keyCode));
	object.put("keyStatus", passThroughKeyStatusEnumToString(keyStatus));

	for (auto watch : mReceivePassThroughCommandWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

/*
 * This will be deprecated on implementation of remoteFeaturesReceived with role.
 */
void BluetoothAvrcpProfileService::remoteFeaturesReceived(BluetoothAvrcpRemoteFeatures features, const std::string &address)
{

}

void BluetoothAvrcpProfileService::remoteFeaturesReceived(BluetoothAvrcpRemoteFeatures features, const std::string &address, const std::string &role)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	// AVRCP supports just single connection in webos-bluetooth-service. If multiple connections are supported,
	// mRemoteFeature should be changed from std::string to std::unordered_map with the remote address.
	mRemoteFeatures = remoteFeaturesEnumToString(features);

	if (mRemoteFeaturesAddress.compare(address) != 0)
	{
		mCTRemoteFeatures = "";
		mTGRemoteFeatures = "";
		mRemoteFeaturesAddress = address;
	}

	if (role.compare("CT") == 0)
		mCTRemoteFeatures = remoteFeaturesEnumToString(features);

	if (role.compare("TG") == 0)
		mTGRemoteFeatures = remoteFeaturesEnumToString(features);
}

void BluetoothAvrcpProfileService::supportedNotificationEventsReceived(const BluetoothAvrcpSupportedNotificationEventList& events,
	const std::string& adapterAddress, const std::string& address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	mSupportedNotificationEvents.clear();
	mSupportedNotificationEvents.assign(events.begin(), events.end());

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", adapterAddress);

	pbnjson::JValue supportedNotificationEventsObj = pbnjson::Array();
	for (auto supportedNotificationEvent : mSupportedNotificationEvents)
	{
		supportedNotificationEventsObj.append((int32_t)supportedNotificationEvent);
	}
	object.put("supportedNotificationEvents", supportedNotificationEventsObj);

	for (auto watch : mNotificationEventsWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

void BluetoothAvrcpProfileService::supportedNotificationEventsReceived(const BluetoothAvrcpSupportedNotificationEventList &events, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	mSupportedNotificationEvents.clear();
	mSupportedNotificationEvents.assign(events.begin(), events.end());

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());

	pbnjson::JValue supportedNotificationEventsObj = pbnjson::Array();
	for (auto supportedNotificationEvent : mSupportedNotificationEvents)
	{
		supportedNotificationEventsObj.append((int32_t)supportedNotificationEvent);
	}
	object.put("supportedNotificationEvents", supportedNotificationEventsObj);

	auto watchIter = mGetSupportedNotificationEventsWatches.find(address);
	if (watchIter == mGetSupportedNotificationEventsWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	LSUtils::postToClient(watch->getMessage(), object);
}

bool BluetoothAvrcpProfileService::prepareAwaitRequest(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP_WITH_VAL_1(subscribe, boolean, true),
		PROP(adapterAddress, string)) REQUIRED_1(subscribe));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return false;

	if (!getManager()->getPowered(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_TURNED_OFF);
		return false;
	}

	return true;
}

void BluetoothAvrcpProfileService::setMediaMetaDataRequestsAllowed(bool state)
{
	if (!state && mIncomingMediaMetaDataWatch)
	{
		delete mIncomingMediaMetaDataWatch;
		mIncomingMediaMetaDataWatch = NULL;
	}

	mMediaMetaDataRequestsAllowed = state;
}

void BluetoothAvrcpProfileService::setMediaPlayStatusRequestsAllowed(bool state)
{
	if (!state && mIncomingMediaPlayStatusWatch)
	{
		delete mIncomingMediaPlayStatusWatch;
		mIncomingMediaPlayStatusWatch = NULL;
	}

	mMediaPlayStatusRequestsAllowed = state;
}

void BluetoothAvrcpProfileService::assignRequestId(MediaRequest *request)
{
	// Make a new requestID convert to string
	std::string nextRequestIdStr = std::to_string(mNextRequestId);
	auto padStr = [](std::string &str, const size_t num, const char paddingChar)
	{
		if (num > str.size())
			str.insert(0, num - str.size(), paddingChar);
	};
	padStr(nextRequestIdStr, 3, '0');
	mNextRequestId++;

	request->requestId = nextRequestIdStr;
}

void BluetoothAvrcpProfileService::createMediaRequest(
	bool metaData, BluetoothAvrcpRequestId requestId, const std::string &adapterAddress,
	const std::string &address)
{
	BluetoothClientWatch *watch;
	if (metaData)
	{
		watch = getMediaRequestWatch(mIncomingMediaMetaDataWatchesForMultipleAdapters,
									 adapterAddress);
	}
	else
	{
		watch = getMediaRequestWatch(
			mIncomingMediaPlayStatusWatchesForMultipleAdapters, adapterAddress);
	}

	if (!watch)
		return;
	MediaRequest *request = new MediaRequest();
	request->address = address;

	if (mNextRequestId > BLUETOOTH_PROFILE_AVRCP_MAX_REQUEST_ID)
		mNextRequestId = 1;

	assignRequestId(request);

    std::map<std::string, MediaRequest*> mediaRequest;
	mediaRequest.insert(std::pair<std::string, MediaRequest*> (adapterAddress, request));
	std::map<std::string, BluetoothAvrcpRequestId> mediaRequestId;
	mediaRequestId.insert(std::pair<std::string, BluetoothAvrcpRequestId> (adapterAddress, requestId));
	if (metaData)
	{
		mMediaMetaDataRequestsMultiAdapters.insert(
			std::pair<uint64_t, std::map<std::string, MediaRequest *>>(mRequestIndex, mediaRequest));
		mMediaMetaDataRequestIdsMultiAdapters.insert(
			std::pair<uint64_t, std::map<std::string, BluetoothAvrcpRequestId>>(mRequestIndex, mediaRequestId));
	}
	else
	{
		mMediaPlayStatusRequestsMultiAdapters.insert(
			std::pair<uint64_t, std::map<std::string, MediaRequest *>>(mRequestIndex, mediaRequest));
		mMediaPlayStatusRequestIdsMultiAdapters.insert(
			std::pair<uint64_t, std::map<std::string, BluetoothAvrcpRequestId>>(mRequestIndex, mediaRequestId));
	}
	mRequestIndex++;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("address", address);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("requestId",  request->requestId);
	LSUtils::postToClient(watch->getMessage(), responseObj);
}

void BluetoothAvrcpProfileService::createMediaRequest(bool metaData, uint64_t requestId, const std::string &address)
{
	if (metaData)
	{
		if (!mMediaMetaDataRequestsAllowed)
			return;
	}
	else
	{
		if (!mMediaPlayStatusRequestsAllowed)
			return;
	}

	MediaRequest *request = new MediaRequest();
	request->address = address;

	if (mNextRequestId > BLUETOOTH_PROFILE_AVRCP_MAX_REQUEST_ID)
		mNextRequestId = 1;

	assignRequestId(request);

	if (metaData)
	{
		mMediaMetaDataRequests.insert(std::pair<uint64_t, MediaRequest*>(mRequestIndex, request));
		mMediaMetaDataRequestIds.insert(std::pair<uint64_t, BluetoothAvrcpRequestId>(mRequestIndex, requestId));
	}
	else
	{
		mMediaPlayStatusRequests.insert(std::pair<uint64_t, MediaRequest*>(mRequestIndex, request));
		mMediaPlayStatusRequestIds.insert(std::pair<uint64_t, BluetoothAvrcpRequestId>(mRequestIndex, requestId));
	}
	mRequestIndex++;

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);
	responseObj.put("address", address);
	responseObj.put("adapterAddress", getManager()->getAddress());
	responseObj.put("requestId", request->requestId);
	if (metaData)
		LSUtils::postToClient(mIncomingMediaMetaDataWatch->getMessage(), responseObj);
	else
		LSUtils::postToClient(mIncomingMediaPlayStatusWatch->getMessage(), responseObj);
}

void BluetoothAvrcpProfileService::deleteMediaRequestId(bool metaData, const std::string &requestIdStr)
{
	uint64_t requestIndex = getRequestIndex(metaData, requestIdStr);
	if (metaData)
	{
		auto idIter = mMediaMetaDataRequestIds.find(requestIndex);
		if (idIter != mMediaMetaDataRequestIds.end())
			mMediaMetaDataRequestIds.erase(idIter);
	}
	else
	{
		auto idIter = mMediaPlayStatusRequestIds.find(requestIndex);
		if (idIter != mMediaPlayStatusRequestIds.end())
			mMediaPlayStatusRequestIds.erase(idIter);
	}
}

void BluetoothAvrcpProfileService::deleteMediaRequestId(
	bool metaData, const std::string &requestIdStr, const std::string &adapterAddress)
{
	std::map<uint64_t, std::map<std::string, BluetoothAvrcpRequestId> > *requests;
	if (metaData)
	{
		requests = &mMediaMetaDataRequestIdsMultiAdapters;
	}
	else
	{
		requests = &mMediaPlayStatusRequestIdsMultiAdapters;
	}
	uint64_t requestIndex = getRequestIndex(metaData, requestIdStr, adapterAddress);
	auto idIter = requests->find(requestIndex);
	if (idIter != requests->end())
		requests->erase(idIter);
}

void BluetoothAvrcpProfileService::deleteMediaRequest(bool metaData, const std::string &requestIdStr)
{
	std::map<uint64_t, MediaRequest*> *mapRequests;
	if (metaData)
		mapRequests = &mMediaMetaDataRequests;
	else
		mapRequests = &mMediaPlayStatusRequests;

	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		MediaRequest *request = propIter->second;
		if (NULL == request)
			continue;

		if (request->requestId == requestIdStr)
		{
			delete request;
			mapRequests->erase(propIter);
			break;
		}
	}
}

void BluetoothAvrcpProfileService::deleteMediaRequest(bool metaData, const std::string &requestIdStr,
													  const std::string &adapterAddress)
{
	std::map<uint64_t, std::map<std::string, MediaRequest*> > *mapRequests;
	if (metaData)
	{
		mapRequests = &mMediaMetaDataRequestsMultiAdapters;
	}
	else
		mapRequests = &mMediaPlayStatusRequestsMultiAdapters;

	std::map<std::string, MediaRequest*> mediaRequestMap;
	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		mediaRequestMap = propIter->second;
		auto requestAdapter = mediaRequestMap.find(adapterAddress);
		if (requestAdapter != mediaRequestMap.end())
		{
			MediaRequest *request = requestAdapter->second;
			if (NULL == request)
				continue;

			if (request->requestId == requestIdStr)
			{
				delete request;
				mapRequests->erase(propIter);
				break;
			}
		}
	}
}

BluetoothAvrcpRequestId BluetoothAvrcpProfileService::findRequestId(bool metaData, const std::string &requestIdStr)
{
	BluetoothAvrcpRequestId requestId = BLUETOOTH_AVRCP_REQUEST_ID_INVALID;
	uint64_t requestIndex = getRequestIndex(metaData, requestIdStr);
	if (metaData)
	{
		auto idIter = mMediaMetaDataRequestIds.find(requestIndex);
		if (idIter != mMediaMetaDataRequestIds.end())
			requestId = idIter->second;
	}
	else
	{
		auto idIter = mMediaPlayStatusRequestIds.find(requestIndex);
		if (idIter != mMediaPlayStatusRequestIds.end())
			requestId = idIter->second;
	}

	return requestId;
}

BluetoothAvrcpRequestId BluetoothAvrcpProfileService::findRequestId(
	bool metaData, const std::string &requestIdStr,
	const std::string &adapterAddress)
{
	BT_INFO("AVRCP", 0, "findRequestId: %s==%s", adapterAddress.c_str(), requestIdStr.c_str());
	BluetoothAvrcpRequestId requestId = BLUETOOTH_AVRCP_REQUEST_ID_INVALID;
	std::map<uint64_t, std::map<std::string, BluetoothAvrcpRequestId> > *requests;
	uint64_t requestIndex = getRequestIndex(metaData, requestIdStr, adapterAddress);
	if (metaData)
	{
		requests = &mMediaMetaDataRequestIdsMultiAdapters;
	}
	else
	{
		requests = &mMediaPlayStatusRequestIdsMultiAdapters;
	}

	auto idIter = requests->find(requestIndex);
	if (idIter != requests->end())
	{
		auto requestIdMap = idIter->second.find(adapterAddress);
		if (requestIdMap != idIter->second.end())
			requestId = requestIdMap->second;
	}
	return requestId;
}

uint64_t BluetoothAvrcpProfileService::getRequestIndex(bool metaData, const std::string &requestIdStr)
{
	uint64_t requestIndex = 0;
	std::map<uint64_t, MediaRequest*> *mapRequests;
	if (metaData)
		mapRequests = &mMediaMetaDataRequests;
	else
		mapRequests = &mMediaPlayStatusRequests;

	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		MediaRequest *request = propIter->second;
		if (request->requestId == requestIdStr)
		{
			requestIndex = (int64_t) propIter->first;
			break;
		}
	}

	return requestIndex;
}

uint64_t BluetoothAvrcpProfileService::getRequestIndex(
	bool metaData, const std::string &requestIdStr, const std::string &adapterAddress)
{
	uint64_t requestIndex = 0;
	std::map<uint64_t, std::map<std::string, MediaRequest*> > *mapRequests;
	if (metaData)
	{
		mapRequests = &mMediaMetaDataRequestsMultiAdapters;
	}
	else
		mapRequests = &mMediaPlayStatusRequestsMultiAdapters;

	std::map<std::string, MediaRequest*> mediaRequestMap;
	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		mediaRequestMap = propIter->second;
		auto requestMap = mediaRequestMap.find(adapterAddress);
		if (requestMap != mediaRequestMap.end())
		{
			MediaRequest *request = requestMap->second;
			if (NULL == request)
				continue;

			if (request->requestId == requestIdStr)
			{
				requestIndex = (int64_t)propIter->first;
				break;
			}
		}
	}
	return requestIndex;
}

BluetoothAvrcpProfileService::MediaRequest* BluetoothAvrcpProfileService::findMediaRequest(bool metaData, const std::string &requestIdStr)
{
	MediaRequest *mediaRequest = NULL;
	std::map<uint64_t, MediaRequest*> *mapRequests;
	if (metaData)
		mapRequests = &mMediaMetaDataRequests;
	else
		mapRequests = &mMediaPlayStatusRequests;

	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		MediaRequest *request = propIter->second;
		if (NULL == request)
			continue;

		if (request->requestId == requestIdStr)
		{
			mediaRequest = request;
			break;
		}
	}

	return mediaRequest;
}

BluetoothAvrcpProfileService::MediaRequest *BluetoothAvrcpProfileService::findMediaRequest(
	bool metaData, const std::string &requestIdStr, const std::string &adapterAddress)
{
	MediaRequest *mediaRequest = NULL;
	std::map<uint64_t, std::map<std::string, MediaRequest*> > *mapRequests;
	if (metaData)
	{
		mapRequests = &mMediaMetaDataRequestsMultiAdapters;
	}
	else
		mapRequests = &mMediaPlayStatusRequestsMultiAdapters;

	std::map<std::string, MediaRequest*> mediaRequestMap;
	for (auto propIter = mapRequests->begin(); propIter != mapRequests->end(); ++propIter)
	{
		mediaRequestMap = propIter->second;
		auto requestMap = mediaRequestMap.find(adapterAddress);
		if (requestMap != mediaRequestMap.end())
		{
			MediaRequest *request = requestMap->second;
			if (NULL == request)
				continue;

			if (request->requestId == requestIdStr)
			{
				mediaRequest = request;
				break;
			}
		}
	}

	return mediaRequest;
}

void BluetoothAvrcpProfileService::notifyConfirmationRequest(LS::Message &request, const std::string &requestId, const std::string &adapterAddress, bool success)
{
	pbnjson::JValue responseObj = pbnjson::Object();
	if (!success)
	{
		LSUtils::respondWithError(request, BT_ERR_AVRCP_STATE_ERR);
	}

	responseObj.put("returnValue", success);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("requestId", requestId);
	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

void BluetoothAvrcpProfileService::parseMediaMetaData(const pbnjson::JValue &dataObj, BluetoothMediaMetaData *data)
{
	data->setTitle(dataObj["title"].asString());
	data->setArtist(dataObj["artist"].asString());
	data->setAlbum(dataObj["album"].asString());
	data->setGenre(dataObj["genre"].asString());
	data->setTrackNumber(dataObj["trackNumber"].asNumber<int64_t>());
	data->setTrackCount(dataObj["trackCount"].asNumber<int64_t>());
	data->setDuration(dataObj["duration"].asNumber<int64_t>());
}

void BluetoothAvrcpProfileService::parseMediaPlayStatus(const pbnjson::JValue &dataObj, BluetoothMediaPlayStatus *status)
{
	status->setDuration(dataObj["duration"].asNumber<int64_t>());
	status->setPosition(dataObj["position"].asNumber<int64_t>());
	std::string statusStr = dataObj["status"].asString();
	if ("stopped" == statusStr)
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_STOPPED);
	else if ("playing" == statusStr)
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_PLAYING);
	else if ("paused" == statusStr)
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_PAUSED);
	else if ("fwd_seek" == statusStr)
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_FWD_SEEK);
	else if ("rev_seek" == statusStr)
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_REV_SEEK);
	else
		status->setStatus(BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_ERROR);
}

std::string BluetoothAvrcpProfileService::mediaPlayStatusToString(BluetoothMediaPlayStatus::MediaPlayStatus status)
{
	if (status == BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_STOPPED)
		return "stopped";
	else if (status == BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_PLAYING)
		return "playing";
	else if (status == BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_PAUSED)
		return "paused";
	else if (status == BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_FWD_SEEK)
		return "fwd_seek";
	else if (status == BluetoothMediaPlayStatus::MediaPlayStatus::MEDIA_PLAYSTATUS_REV_SEEK)
		return "rev_seek";
	else
		return "unknown_status";
}

BluetoothPlayerApplicationSettingsEqualizer BluetoothAvrcpProfileService::equalizerStringToEnum(const std::string &equalizer)
{
	if ("off" == equalizer)
		return BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_OFF;
	else if ("on" == equalizer)
		return BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_ON;
	else
		return BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_UNKNOWN;
}

BluetoothPlayerApplicationSettingsRepeat BluetoothAvrcpProfileService::repeatStringToEnum(const std::string &repeat)
{
	if ("off" == repeat)
		return BluetoothPlayerApplicationSettingsRepeat::REPEAT_OFF;
	else if ("singletrack" == repeat)
		return BluetoothPlayerApplicationSettingsRepeat::REPEAT_SINGLE_TRACK;
	else if ("alltrack" == repeat)
		return BluetoothPlayerApplicationSettingsRepeat::REPEAT_ALL_TRACKS;
	else if ("group" == repeat)
		return BluetoothPlayerApplicationSettingsRepeat::REPEAT_GROUP;
	else
		return BluetoothPlayerApplicationSettingsRepeat::REPEAT_UNKNOWN;
}

BluetoothPlayerApplicationSettingsShuffle BluetoothAvrcpProfileService::shuffleStringToEnum(const std::string &shuffle)
{
	if ("off" == shuffle)
		return BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_OFF;
	else if ("alltrack" == shuffle)
		return BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_ALL_TRACKS;
	else if ("group" == shuffle)
		return BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_GROUP;
	else
		return BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_UNKNOWN;
}

BluetoothPlayerApplicationSettingsScan BluetoothAvrcpProfileService::scanStringToEnum(const std::string &scan)
{
	if ("off" == scan)
		return BluetoothPlayerApplicationSettingsScan::SCAN_OFF;
	else if ("alltrack" == scan)
		return BluetoothPlayerApplicationSettingsScan::SCAN_ALL_TRACKS;
	else if ("group" == scan)
		return BluetoothPlayerApplicationSettingsScan::SCAN_GROUP;
	else
		return BluetoothPlayerApplicationSettingsScan::SCAN_UNKNOWN;
}

std::string BluetoothAvrcpProfileService::equalizerEnumToString(BluetoothPlayerApplicationSettingsEqualizer equalizer)
{
	if (BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_OFF == equalizer)
		return "off";
	else if (BluetoothPlayerApplicationSettingsEqualizer::EQUALIZER_ON == equalizer)
		return "on";
	else
		return "unknown";
}

std::string BluetoothAvrcpProfileService::repeatEnumToString(BluetoothPlayerApplicationSettingsRepeat repeat)
{
	if (BluetoothPlayerApplicationSettingsRepeat::REPEAT_OFF == repeat)
		return "off";
	else if (BluetoothPlayerApplicationSettingsRepeat::REPEAT_SINGLE_TRACK == repeat)
		return "singletrack";
	else if (BluetoothPlayerApplicationSettingsRepeat::REPEAT_ALL_TRACKS == repeat)
		return "alltrack";
	else if (BluetoothPlayerApplicationSettingsRepeat::REPEAT_GROUP == repeat)
		return "group";
	else
		return "unknown";
}

std::string BluetoothAvrcpProfileService::shuffleEnumToString(BluetoothPlayerApplicationSettingsShuffle shuffle)
{
	if (BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_OFF == shuffle)
		return "off";
	else if (BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_ALL_TRACKS == shuffle)
		return "alltrack";
	else if (BluetoothPlayerApplicationSettingsShuffle::SHUFFLE_GROUP == shuffle)
		return "group";
	else
		return "unknown";
}

std::string BluetoothAvrcpProfileService::scanEnumToString(BluetoothPlayerApplicationSettingsScan scan)
{
	if (BluetoothPlayerApplicationSettingsScan::SCAN_OFF == scan)
		return "off";
	else if (BluetoothPlayerApplicationSettingsScan::SCAN_ALL_TRACKS == scan)
		return "alltrack";
	else if (BluetoothPlayerApplicationSettingsScan::SCAN_GROUP == scan)
		return "group";
	else
		return "unknown";
}

bool BluetoothAvrcpProfileService::getPlayerInfo(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
				PROP(address, string), PROP_WITH_VAL_1(subscribe, boolean, true))
			REQUIRED_2(subscribe, address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}


	bool retVal = addClientWatch(request, &mGetPlayerInfoWatchesForMultipleAdapters,
			adapterAddress, deviceAddress);
	if (!retVal)
	{
		LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	auto playerInfoListIter = mPlayerInfoListForMultipleAdapters.find(adapterAddress);
	if (playerInfoListIter != mPlayerInfoListForMultipleAdapters.end())
	{
		auto playerInfoIter = (playerInfoListIter->second).find(deviceAddress);
		if (playerInfoIter != (playerInfoListIter->second).end())
		{
			if (!(playerInfoIter->second).empty())
			{
				pbnjson::JValue playerInfoList = pbnjson::Array();
				for (auto playerInfo = (playerInfoIter->second).begin();
					 playerInfo != (playerInfoIter->second).end(); ++playerInfo)
				{
					pbnjson::JValue playerInfoObj = pbnjson::Object();
					playerInfoObj.put("name", playerInfo->getName());
					playerInfoObj.put("type", playerTypeEnumToString(playerInfo->getType()));
					playerInfoObj.put("addressed", playerInfo->getAddressed());
					playerInfoObj.put("browsable", playerInfo->getBrowsable());
					playerInfoObj.put("searchable", playerInfo->getSearchable());
					playerInfoObj.put("playlistPath", playerInfo->getPlayListPath());
					playerInfoList.append(playerInfoObj);
				}
				responseObj.put("playerInfo", playerInfoList);
			}
		}
	}
	LSUtils::postToClient(request, responseObj);

	return true;
}

void BluetoothAvrcpProfileService::playerInfoReceived(
		const BluetothPlayerInfoList &playerInfoList,
		const std::string &adapterAddress, const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	auto playerInfoListIter = mPlayerInfoListForMultipleAdapters.find(adapterAddress);
	if (playerInfoListIter == mPlayerInfoListForMultipleAdapters.end())
	{
		std::map<std::string, BluetothPlayerInfoList> playerInfoListDevice;
		BluetothPlayerInfoList playerInfo;
		playerInfo.assign(playerInfoList.begin(), playerInfoList.end());
		playerInfoListDevice.insert(
			std::pair<std::string, BluetothPlayerInfoList>(address, playerInfo));
		mPlayerInfoListForMultipleAdapters.insert(
			std::pair<std::string,
					  std::map<std::string, BluetothPlayerInfoList>>(adapterAddress, playerInfoListDevice));
	}
	else
	{
		auto playerInfoIter = (playerInfoListIter->second).find(address);
		if (playerInfoIter == (playerInfoListIter->second).end())
		{
			BluetothPlayerInfoList playerInfo;
			playerInfo.assign(playerInfoList.begin(), playerInfoList.end());
			(playerInfoListIter->second).insert(
				std::pair<std::string, BluetothPlayerInfoList>(address, playerInfo));
		}
		else
		{
			(playerInfoIter->second).clear();
			(playerInfoIter->second).assign(playerInfoList.begin(), playerInfoList.end());
		}
	}

	pbnjson::JValue object = pbnjson::Object();
	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", adapterAddress);

	pbnjson::JValue playerInfoListObj = pbnjson::Array();
	for (auto playerInfo : playerInfoList)
	{
		pbnjson::JValue playerInfoObj = pbnjson::Object();
		playerInfoObj.put("name", playerInfo.getName());
		playerInfoObj.put("type", playerTypeEnumToString(playerInfo.getType()));
		playerInfoObj.put("addressed", playerInfo.getAddressed());
		playerInfoObj.put("browsable", playerInfo.getBrowsable());
		playerInfoObj.put("searchable", playerInfo.getSearchable());
		playerInfoObj.put("playlistPath", playerInfo.getPlayListPath());

		playerInfoListObj.append(playerInfoObj);
	}
	object.put("playerInfo", playerInfoListObj);

	for (auto watch : mGetPlayerInfoWatchesForMultipleAdapters)
	{
		if (convertToLower(adapterAddress) ==
				convertToLower(watch->getAdapterAddress()) &&
			convertToLower(address) == convertToLower(watch->getDeviceAddress()))
		{
			LSUtils::postToClient(watch->getMessage(), object);
		}
	}
}

std::string BluetoothAvrcpProfileService::playerTypeEnumToString(const BluetoothAvrcpPlayerType type)
{
	switch(type)
	{
		case BluetoothAvrcpPlayerType::PLAYER_TYPE_AUDIO:
			return "Audio";
		case BluetoothAvrcpPlayerType::PLAYER_TYPE_AUDIO_BROADCAST:
			return "Audio Broadcasting";
		case BluetoothAvrcpPlayerType::PLAYER_TYPE_VIDEO:
			return "Video";
		case BluetoothAvrcpPlayerType::PLAYER_TYPE_VIDEO_BROADCAST:
			return "Video Broadcasting";
		default:
			return "Audio";
	}
}

void BluetoothAvrcpProfileService::currentFolderReceived(
	const std::string currentFolder, const std::string &adapterAddress,
	const std::string &address)
{
	BT_INFO("AVRCP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	bool sendResponse = true;

	auto currentFolderIter = mCurrentFolderForMultipleAdapters.find(adapterAddress);
	if (currentFolderIter == mCurrentFolderForMultipleAdapters.end())
	{
		std::map<std::string, std::string> currentFolderDevice;
		currentFolderDevice.insert(
			std::pair<std::string, std::string>(address, currentFolder));
		mCurrentFolderForMultipleAdapters.insert(
			std::pair<std::string,
					  std::map<std::string, std::string>>(adapterAddress, currentFolderDevice));
	}
	else
	{
		auto currentFolderIterDevice = (currentFolderIter->second).find(address);
		if (currentFolderIterDevice == (currentFolderIter->second).end())
		{
			(currentFolderIter->second).insert(std::pair<std::string, std::string>(address, currentFolder));
		}
		else
		{
			if (currentFolderIterDevice->second != currentFolder)
				currentFolderIterDevice->second = currentFolder;
			else
				sendResponse = false;
		}
	}

	if (sendResponse)
	{
		for (auto watch : mGetCurrentFolderWatchesForMultipleAdapters)
		{
			if (convertToLower(adapterAddress) == convertToLower(watch->getAdapterAddress()) &&
				convertToLower(address) == convertToLower(watch->getDeviceAddress()))
			{
				pbnjson::JValue object = pbnjson::Object();

				object.put("returnValue", true);
				object.put("subscribed", true);
				object.put("address", address);
				object.put("adapterAddress", adapterAddress);
				object.put("folderName", currentFolder);
				LSUtils::postToClient(watch->getMessage(), object);
			}
		}
	}
}

bool BluetoothAvrcpProfileService::getCurrentFolder(LSMessage &message){

	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string), PROP(address, string),
				PROP(subscribe, boolean))REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	bool subscribed =  false;

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mGetCurrentFolderWatchesForMultipleAdapters,
				adapterAddress, deviceAddress);
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}

		subscribed = true;
	}

	std::string currentFolderName;

	auto currentFolderIter = mCurrentFolderForMultipleAdapters.find(adapterAddress);
	if (currentFolderIter != mCurrentFolderForMultipleAdapters.end())
	{
		auto currentFolderIterDevice = (currentFolderIter->second).find(deviceAddress);
		if (currentFolderIterDevice != (currentFolderIter->second).end())
		{
			currentFolderName = currentFolderIterDevice->second;
		}
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	responseObj.put("folderName", currentFolderName);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::getNumberOfItems(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(adapterAddress, string), PROP(address, string)) REQUIRED_1(address));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto numberOfItemsCallback = [requestMessage, adapterAddress, deviceAddress](
									 BluetoothError error, const uint32_t numberOfItems) {
		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(requestMessage, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		responseObj.put("returnValue", true);
		responseObj.put("numberOfItems", (int32_t)numberOfItems);

		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);
	};
	impl->getNumberOfItems(numberOfItemsCallback);

	return true;
}

bool BluetoothAvrcpProfileService::getFolderItems(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(adapterAddress, string),
													 PROP(address, string), PROP(startIndex, integer),
													 PROP(endIndex, integer)) REQUIRED_3(address, startIndex, endIndex));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("startIndex"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_START_INDEX_PARAM_MISSING);
		else if (!requestObj.hasKey("endIndex"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_END_INDEX_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}
	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto folderItemsCallback = [this, requestMessage, adapterAddress, deviceAddress](
								   BluetoothError error, const BluetoothFolderItemList &folderItems) {
		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(requestMessage, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();
		pbnjson::JValue itemArray = pbnjson::Array();

		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		responseObj.put("returnValue", true);
		for (auto item : folderItems)
		{
			pbnjson::JValue itemObj = pbnjson::Object();
			itemObj.put("name", item.getName());
			itemObj.put("path", item.getPath());
			itemObj.put("type", folderItemTypeEnumToString(item.getType()));
			itemObj.put("playable", item.getPlayable());
			if (BluetoothAvrcpItemType::ITEM_TYPE_FOLDER != item.getType())
			{
				pbnjson::JValue metadataObj = pbnjson::Object();
				BluetoothMediaMetaData mediaMetadata = item.getMetadata();

				metadataObj.put("title", mediaMetadata.getTitle());
				metadataObj.put("artist", mediaMetadata.getArtist());
				metadataObj.put("album", mediaMetadata.getAlbum());
				metadataObj.put("genre", mediaMetadata.getGenre());
				metadataObj.put("trackNumber", (int32_t)mediaMetadata.getTrackNumber());
				metadataObj.put("trackCount", (int32_t)mediaMetadata.getTrackCount());
				metadataObj.put("duration", (int32_t)mediaMetadata.getDuration());

				itemObj.put("metaData", metadataObj);
			}
			itemArray.append(itemObj);
		}
		responseObj.put("folderItems", itemArray);

		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);
	};
	impl->getFolderItems(requestObj["startIndex"].asNumber<int32_t>(),
						 requestObj["endIndex"].asNumber<int32_t>(), folderItemsCallback);

	return true;
}

std::string BluetoothAvrcpProfileService::folderItemTypeEnumToString(BluetoothAvrcpItemType type)
{
	switch(type)
	{
		case BluetoothAvrcpItemType::ITEM_TYPE_AUDIO:
		return "Audio";
		case BluetoothAvrcpItemType::ITEM_TYPE_VIDEO:
		return "Video";
		case BluetoothAvrcpItemType::ITEM_TYPE_FOLDER:
		return "Folder";
		default:
		return "Audio";
	}
}

bool BluetoothAvrcpProfileService::changePath(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(address, string), PROP(itemPath, string)) REQUIRED_2(address, itemPath));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("itemPath"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_ITEM_PATH_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}
	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}
	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	BluetoothError error = impl->changePath(requestObj["itemPath"].asString());
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::playItem(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(address, string), PROP(itemPath, string))
													 REQUIRED_2(address, itemPath));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("itemPath"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_ITEM_PATH_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}
	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}
	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	BluetoothError error = impl->playItem(requestObj["itemPath"].asString());
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::addToNowPlaying(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(address, string), PROP(itemPath, string)) REQUIRED_2(address, itemPath));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("itemPath"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_ITEM_PATH_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}
	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	BluetoothError error = impl->addToNowPlaying(requestObj["itemPath"].asString());
	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, error);
		return true;
	}
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);
	responseObj.put("returnValue", true);
	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothAvrcpProfileService::search(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(adapterAddress, string),
													 PROP(address, string), PROP(searchString, string)) REQUIRED_2(address, searchString));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("searchString"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_SEARCH_STRING_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}
	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);

	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}
	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);
	auto searchCallback = [requestMessage, adapterAddress, deviceAddress](
		BluetoothError error, const std::string searchListPath) {
		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(requestMessage, error);
			return;
		}
		pbnjson::JValue responseObj = pbnjson::Object();

		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		responseObj.put("returnValue", true);
		responseObj.put("searchResult", searchListPath);

		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);

	};

	impl->search(requestObj["searchString"].asString(), searchCallback);

	return true;
}

bool BluetoothAvrcpProfileService::notifyMediaPlayStatus(LSMessage &message)
{
	BT_INFO("AVRCP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string),
		OBJECT(playbackStatus, OBJSCHEMA_3(PROP(duration, integer), PROP(position, integer), PROP(status, string))),
		PROP(adapterAddress, string))
		REQUIRED_2(address, playbackStatus));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("playbackStatus"))
			LSUtils::respondWithError(request, BT_ERR_AVRCP_PLAYBACK_STATUS_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	BluetoothAvrcpProfile *impl = getImpl<BluetoothAvrcpProfile>(adapterAddress);
	if (!impl)
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string deviceAddress;
	deviceAddress = convertToLower(requestObj["address"].asString());

	if (!isDeviceConnected(adapterAddress, deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return true;
	}

	auto playStatusObj = requestObj["playbackStatus"];
	BluetoothMediaPlayStatus playStatus;
	parseMediaPlayStatus(playStatusObj, &playStatus);

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto requestCallback = [requestMessage, adapterAddress, deviceAddress](BluetoothError error)
	{
		BT_INFO("AVRCP", 0, "Return of notifyMediaPlayStatus is %d", error);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			LSUtils::respondWithError(requestMessage, error);
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
		LSUtils::postToClient(requestMessage, responseObj);
		LSMessageUnref(requestMessage);

	};

	BT_INFO("AVRCP", 0, "Service calls SIL API : notifyMediaPlayStatus");
	impl->notifyMediaPlayStatus(playStatus, requestCallback);

	return true;
}
