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


#include "bluetootha2dpprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "logging.h"
#include "clientwatch.h"

BluetoothA2dpProfileService::BluetoothA2dpProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "A2DP", "0000110a-0000-1000-8000-00805f9b34fb",
        "0000110b-0000-1000-8000-00805f9b34fb"),
        mAudioSocketInfo(0),
        mSbcConfigurationInfo(0),
        mAptxConfigurationInfo(0)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, internal)
		LS_CATEGORY_CLASS_METHOD(BluetoothA2dpProfileService, startStreaming)
		LS_CATEGORY_CLASS_METHOD(BluetoothA2dpProfileService, stopStreaming)
		LS_CATEGORY_CLASS_METHOD(BluetoothA2dpProfileService, getAudioPath)
		LS_CATEGORY_CLASS_METHOD(BluetoothA2dpProfileService, setSbcEncoderBitpool)
		LS_CATEGORY_CLASS_METHOD(BluetoothA2dpProfileService, getCodecConfiguration)
		LS_CATEGORY_METHOD(enable)
		LS_CATEGORY_METHOD(disable)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/a2dp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/a2dp", this);

	manager->registerCategory("/a2dp/internal", LS_CATEGORY_TABLE_NAME(internal), NULL, NULL);
	manager->setCategoryData("/a2dp/internal", this);
}

BluetoothA2dpProfileService::~BluetoothA2dpProfileService()
{
}

void BluetoothA2dpProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothA2dpProfile>()->registerObserver(this);
}

void BluetoothA2dpProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothA2dpProfile>(adapterAddress)->registerObserver(this);
}

void BluetoothA2dpProfileService::stateChanged(std::string address, BluetoothA2dpProfileState state)
{
	BT_INFO("A2DP", 0, "stateChanged : address %s, state %d", address.c_str(), state);

	auto device = getManager()->findDevice(address);
	if (!device)
		return;

	if (!isDeviceConnected(address))
		return;

	auto deviceIterator = std::find(mPlayingDevices.begin(), mPlayingDevices.end(), address);

	if (PLAYING == state && deviceIterator == mPlayingDevices.end())
		mPlayingDevices.push_back(address);
	else if (NOT_PLAYING == state && deviceIterator != mPlayingDevices.end())
		mPlayingDevices.erase(deviceIterator);
	else
		return;

	notifyStatusSubscribers(getManager()->getAddress(), address, true);
}

void BluetoothA2dpProfileService::stateChanged(const std::string adapterAddress, const std::string address, BluetoothA2dpProfileState state)
{
	BT_INFO("A2DP", 0, "stateChanged : address %s, state %d", address.c_str(), state);

	if (!isDeviceConnected(adapterAddress, address))
		return;

	if (PLAYING == state && !isDevicePlaying(adapterAddress, address))
		markDeviceAsPlaying(adapterAddress, address);
	else if (NOT_PLAYING == state && isDevicePlaying(adapterAddress, address))
		markDeviceAsNotPlaying(adapterAddress, address);
	else
		return;

	notifyStatusSubscribers(adapterAddress, address, true);
}

void BluetoothA2dpProfileService::audioSocketCreated(const std::string &address, const std::string &path, BluetoothA2dpAudioSocketType type, bool isIn)
{
	BT_INFO("A2DP", 0, "audioSocketCreated : path %s", path.c_str());

	auto device = getManager()->findDevice(address);
	if (!device)
		return;

	if (!isDeviceConnected(address))
		return;

	if (mAudioSocketInfo)
		DELETE_OBJ(mAudioSocketInfo);

	mAudioSocketInfo = new AudioSocketInfo();
	mAudioSocketInfo->deviceAddress = address;
	mAudioSocketInfo->path = path;
	mAudioSocketInfo->type = type;
	mAudioSocketInfo->isIn = isIn;
}

void BluetoothA2dpProfileService::audioSocketDestroyed(const std::string &address, const std::string &path, BluetoothA2dpAudioSocketType type, bool isIn)
{
	BT_INFO("A2DP", 0, "audioSocketDestroyed : path %s", path.c_str());

	if (mAudioSocketInfo)
		DELETE_OBJ(mAudioSocketInfo);

	if (mSbcConfigurationInfo)
		DELETE_OBJ(mSbcConfigurationInfo);

	if (mAptxConfigurationInfo)
		DELETE_OBJ(mAptxConfigurationInfo);
}

void BluetoothA2dpProfileService::sbcConfiguraionChanged(const std::string &address, const BluetoothSbcConfiguration &sbcConfiguration)
{
	BT_INFO("A2DP", 0, "sbcConfiguraionChanged : address %s", address.c_str());

	auto device = getManager()->findDevice(address);
	if (!device)
		return;

	if (mSbcConfigurationInfo)
		DELETE_OBJ(mSbcConfigurationInfo);

	mSbcConfigurationInfo = new SbcConfigurationInfo();
	mSbcConfigurationInfo->sampleFrequency= (int32_t)sbcSamplingFrequencyEnumToInteger(sbcConfiguration.getSampleFrequency());
	mSbcConfigurationInfo->channelMode = sbcChannelModeEnumToString(sbcConfiguration.getChannelMode());
	mSbcConfigurationInfo->blockLength = (int32_t)sbcBlockLengthEnumToInteger(sbcConfiguration.getBlockLength());
	mSbcConfigurationInfo->subbands = (int32_t)sbcSubbandsEnumToInteger(sbcConfiguration.getSubbands());
	mSbcConfigurationInfo->allocationMethod = sbcAllocationMethodEnumToString(sbcConfiguration.getAllocationMethod());
	mSbcConfigurationInfo->minBitpool = (int32_t)sbcConfiguration.getMinBitpool();
	mSbcConfigurationInfo->maxBitpool = (int32_t)sbcConfiguration.getMaxBitpool();

	notifySubscribersAboutSbcConfiguration(address);
}

void BluetoothA2dpProfileService::aptxConfigurationChanged(const std::string &address, const BluetoothAptxConfiguration &aptxConfiguration)
{
	BT_INFO("A2DP", 0, "aptxConfigurationChanged : address %s", address.c_str());

	if (mAptxConfigurationInfo)
		DELETE_OBJ(mAptxConfigurationInfo);

	mAptxConfigurationInfo = new AptxConfigurationInfo();
	mAptxConfigurationInfo->sampleFrequency = (int32_t)aptxSamplingFrequencyEnumToInteger(aptxConfiguration.getSampleFrequency());
	mAptxConfigurationInfo->channelMode = aptxChannelModeEnumToString(aptxConfiguration.getChannelMode());

	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());

	notifySubscribersAboutAptxConfiguration(address);
}

void BluetoothA2dpProfileService::notifySubscribersAboutSbcConfiguration(std::string address)
{
	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());

	pbnjson::JValue sbcConfigurationObj = pbnjson::Object();
	sbcConfigurationObj.put("sampleFrequency", mSbcConfigurationInfo->sampleFrequency);
	sbcConfigurationObj.put("channelMode", mSbcConfigurationInfo->channelMode);
	sbcConfigurationObj.put("blockLength", mSbcConfigurationInfo->blockLength);
	sbcConfigurationObj.put("subbands", mSbcConfigurationInfo->subbands);
	sbcConfigurationObj.put("allocationMethod", mSbcConfigurationInfo->allocationMethod);
	sbcConfigurationObj.put("minBitpool", mSbcConfigurationInfo->minBitpool);
	sbcConfigurationObj.put("maxBitpool", mSbcConfigurationInfo->maxBitpool);

	object.put("sbcConfiguration", sbcConfigurationObj);

	auto watchIter = mGetCodecConfigurationWatches.find(address);
	if (watchIter == mGetCodecConfigurationWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	LSUtils::postToClient(watch->getMessage(), object);
}

void BluetoothA2dpProfileService::notifySubscribersAboutAptxConfiguration(std::string address)
{
	pbnjson::JValue object = pbnjson::Object();

	object.put("returnValue", true);
	object.put("subscribed", true);
	object.put("address", address);
	object.put("adapterAddress", getManager()->getAddress());

	pbnjson::JValue aptxConfigurationObj = pbnjson::Object();
	aptxConfigurationObj.put("sampleFrequency", mAptxConfigurationInfo->sampleFrequency);
	aptxConfigurationObj.put("channelMode", mAptxConfigurationInfo->channelMode);

	object.put("aptxConfiguration", aptxConfigurationObj);

	auto watchIter = mGetCodecConfigurationWatches.find(address);
	if (watchIter == mGetCodecConfigurationWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	LSUtils::postToClient(watch->getMessage(), object);
}

pbnjson::JValue BluetoothA2dpProfileService::buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
                                                                        std::string adapterAddress, std::string deviceAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	appendCommonProfileStatus(responseObj, connected, connecting, subscribed,
                                  returnValue, adapterAddress, deviceAddress);

	bool playing = false;

	if (!connected)
		markDeviceAsNotPlaying(adapterAddress, deviceAddress);

	if (isDevicePlaying(adapterAddress, deviceAddress))
		playing = true;

	responseObj.put("playing", playing);

	return responseObj;
}

bool BluetoothA2dpProfileService::startStreaming(LSMessage &message)
{
	BT_INFO("A2DP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

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
	if (requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return true;
	}

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothA2dpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
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


	BT_INFO("A2DP", 0, "Service called SIL API : startStreaming");
	BluetoothError error = getImpl<BluetoothA2dpProfile>(adapterAddress)->startStreaming(deviceAddress);
	BT_INFO("A2DP", 0, "Return of startStreaming is %d", error);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, BT_ERR_A2DP_START_STREAMING_FAILED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothA2dpProfileService::stopStreaming(LSMessage &message)
{
	BT_INFO("A2DP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

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
	if (requestObj.hasKey("adapterAddress"))
		adapterAddress = requestObj["adapterAddress"].asString();
	else
		adapterAddress = getManager()->getAddress();

	auto adapter = getManager()->getAdapter(adapterAddress);
	if (!adapter)
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_NOT_AVAILABLE);
		return true;
	}

	BluetoothProfile *impl = findImpl(adapterAddress);
	if (!impl && !getImpl<BluetoothA2dpProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
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

	BT_INFO("A2DP", 0, "Service calls SIL API : stopStreaming");
	BluetoothError error = getImpl<BluetoothA2dpProfile>(adapterAddress)->stopStreaming(deviceAddress);
	BT_INFO("A2DP", 0, "Return of stopStreaming is %d", error);

	if (BLUETOOTH_ERROR_NONE != error)
	{
		LSUtils::respondWithError(request, BT_ERR_A2DP_STOP_STREAMING_FAILED);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothA2dpProfileService::getAudioPath(LSMessage &message)
{
	BT_INFO("A2DP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothA2dpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

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
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	if (!mAudioSocketInfo)
	{
		LSUtils::respondWithError(request, BT_ERR_A2DP_GET_SOCKET_PATH_FAILED);
	}
	else
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", getManager()->getAddress());
		responseObj.put("returnValue", true);
		responseObj.put("address", mAudioSocketInfo->deviceAddress);
		responseObj.put("path", mAudioSocketInfo->path);

		if (mAudioSocketInfo->type == BluetoothA2dpAudioSocketType::TCP)
			responseObj.put("type", "tcp");
		else if (mAudioSocketInfo->type == BluetoothA2dpAudioSocketType::UDP)
			responseObj.put("type", "udp");
		else
			LSUtils::respondWithError(request, BT_ERR_A2DP_GET_SOCKET_PATH_FAILED);

		responseObj.put("direction", mAudioSocketInfo->isIn);

		LSUtils::postToClient(request, responseObj);
	}

	return true;
}

bool BluetoothA2dpProfileService::setSbcEncoderBitpool(LSMessage &message)
{
	BT_INFO("A2DP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothA2dpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string), PROP(bitpool, integer)) REQUIRED_2(address, bitpool));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_A2DP_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("bitpool"))
			LSUtils::respondWithError(request, BT_ERR_A2DP_SBC_ENCODER_BITPOOL_MISSING);
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
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	uint8_t bitpool = 0;
	if (requestObj.hasKey("bitpool"))
		bitpool = (uint8_t) requestObj["bitpool"].asNumber<int32_t>();

	BT_INFO("A2DP", 0, "Service calls SIL API : setSbcEncoderBitpool");
	BluetoothError error = getImpl<BluetoothA2dpProfile>()->setSbcEncoderBitpool(deviceAddress, bitpool);
	BT_INFO("A2DP", 0, "Return of setSbcEncoderBitpool is %d", error);

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE == error)
	{
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("address", deviceAddress);
	}
	else
	{
		appendErrorResponse(responseObj, error);
	}

	LSUtils::postToClient(request, responseObj);

	return true;
}
bool BluetoothA2dpProfileService::getCodecConfiguration(LSMessage &message)
{
	BT_INFO("A2DP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothA2dpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

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
		deviceAddress = requestObj["address"].asString();
		if (!getManager()->isDeviceAvailable(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
		}

		if (!isDeviceConnected(deviceAddress))
		{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
			return true;
		}
	}

	bool subscribed =  false;

	if (request.isSubscription())
	{
		auto watch = new LSUtils::ClientWatch(getManager()->get(), request.get(),
		                    std::bind(&BluetoothA2dpProfileService::handleGetCodecConfigurationClientDisappeared, this, adapterAddress, deviceAddress));

		mGetCodecConfigurationWatches.insert(std::pair<std::string, LSUtils::ClientWatch*>(deviceAddress, watch));
		subscribed = true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", subscribed);
	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	if (mSbcConfigurationInfo)
		notifySubscribersAboutSbcConfiguration(deviceAddress);
	else if(mAptxConfigurationInfo)
		notifySubscribersAboutAptxConfiguration(deviceAddress);

	return true;
}

void BluetoothA2dpProfileService::handleGetCodecConfigurationClientDisappeared(const std::string &adapterAddress, const std::string &address)
{
	auto watchIter = mGetCodecConfigurationWatches.find(address);
	if (watchIter == mGetCodecConfigurationWatches.end())
		return;

	if (!getImpl<BluetoothA2dpProfile>())
		return;

	removeGetCodecConfiguraionForDevice(address, true, false);

}

void BluetoothA2dpProfileService::removeGetCodecConfiguraionForDevice(const std::string &address, bool disconnected, bool remoteDisconnect)
{
	auto watchIter = mGetCodecConfigurationWatches.find(address);
	if (watchIter == mGetCodecConfigurationWatches.end())
		return;

	LSUtils::ClientWatch *watch = watchIter->second;

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("subscribed", false);
	responseObj.put("returnValue", !disconnected);
	if (disconnected)
	{
		if (remoteDisconnect)
			responseObj.put("disconnectByRemote", true);
		else
			responseObj.put("disconnectByRemote", false);
	}
	responseObj.put("adapterAddress", getManager()->getAddress());

	LSUtils::postToClient(watch->getMessage(), responseObj);

	mGetCodecConfigurationWatches.erase(watchIter);
	delete watch;
}

int BluetoothA2dpProfileService::sbcSamplingFrequencyEnumToInteger(BluetoothSbcConfiguration::SampleFrequency sampleFrequency)
{
	if (BluetoothSbcConfiguration::SampleFrequency::SAMPLE_FREQUENCY_16000 == sampleFrequency)
		return 16000;
	else if(BluetoothSbcConfiguration::SampleFrequency::SAMPLE_FREQUENCY_32000 == sampleFrequency)
		return 32000;
	else if(BluetoothSbcConfiguration::SampleFrequency::SAMPLE_FREQUENCY_44100 == sampleFrequency)
		return 44100;
	else if(BluetoothSbcConfiguration::SampleFrequency::SAMPLE_FREQUENCY_48000 == sampleFrequency)
		return 48000;
	else
		return 0;
}

std::string BluetoothA2dpProfileService::sbcChannelModeEnumToString(BluetoothSbcConfiguration::ChannelMode channelMode)
{
	if (BluetoothSbcConfiguration::ChannelMode::CHANNEL_MODE_MONO == channelMode)
		return "mono";
	else if (BluetoothSbcConfiguration::ChannelMode::CHANNEL_MODE_DUAL_CHANNEL == channelMode)
		return "dualChannel";
	else if (BluetoothSbcConfiguration::ChannelMode::CHANNEL_MODE_STEREO == channelMode)
		return "stereo";
	else if (BluetoothSbcConfiguration::ChannelMode::CHANNEL_MODE_JOINT_STEREO == channelMode)
		return "jointStereo";
	else
		return "unknown";
}

int BluetoothA2dpProfileService::sbcBlockLengthEnumToInteger(BluetoothSbcConfiguration::BlockLength blockLength)
{
	if (BluetoothSbcConfiguration::BlockLength::BLOCK_LENGTH_4 == blockLength)
		return 4;
	else if (BluetoothSbcConfiguration::BlockLength::BLOCK_LENGTH_8 == blockLength)
		return 8;
	else if (BluetoothSbcConfiguration::BlockLength::BLOCK_LENGTH_12 == blockLength)
		return 12;
	else if (BluetoothSbcConfiguration::BlockLength::BLOCK_LENGTH_16 == blockLength)
		return 16;
	else
		return 0;
}

int BluetoothA2dpProfileService::sbcSubbandsEnumToInteger(BluetoothSbcConfiguration::Subbands subbands)
{
	if (BluetoothSbcConfiguration::Subbands::SUBBANDS_4 == subbands)
		return 4;
	else if (BluetoothSbcConfiguration::Subbands::SUBBANDS_8 == subbands)
		return 8;
	else
		return 0;
}

std::string BluetoothA2dpProfileService::sbcAllocationMethodEnumToString(BluetoothSbcConfiguration::AllocationMethod allocationMethod)
{
	if (BluetoothSbcConfiguration::AllocationMethod::ALLOCATION_METHOD_SNR == allocationMethod)
		return "snr";
	else if (BluetoothSbcConfiguration::AllocationMethod::ALLOCATION_METHOD_LOUDNESS == allocationMethod)
		return "loudness";
	else
		return "unknown";
}

int BluetoothA2dpProfileService::aptxSamplingFrequencyEnumToInteger(BluetoothAptxConfiguration::SampleFrequency sampleFrequency)
{
	if (BluetoothAptxConfiguration::SampleFrequency::SAMPLE_FREQUENCY_16000 == sampleFrequency)
		return 16000;
	else if(BluetoothAptxConfiguration::SampleFrequency::SAMPLE_FREQUENCY_32000 == sampleFrequency)
		return 32000;
	else if(BluetoothAptxConfiguration::SampleFrequency::SAMPLE_FREQUENCY_44100 == sampleFrequency)
		return 44100;
	else if(BluetoothAptxConfiguration::SampleFrequency::SAMPLE_FREQUENCY_48000 == sampleFrequency)
		return 48000;
	else
		return 0;
}

std::string BluetoothA2dpProfileService::aptxChannelModeEnumToString(BluetoothAptxConfiguration::ChannelMode channelMode)
{
	if (BluetoothAptxConfiguration::ChannelMode::CHANNEL_MODE_MONO == channelMode)
		return "mono";
	else if (BluetoothAptxConfiguration::ChannelMode::CHANNEL_MODE_STEREO == channelMode)
		return "stereo";
	else
		return "unknown";
}

bool BluetoothA2dpProfileService::isDevicePlaying(const std::string &adapterAddress, const std::string &address)
{
	auto playingDevicesiter = mPlayingDevicesForMultipleAdapters.find(adapterAddress);
	if (playingDevicesiter == mPlayingDevicesForMultipleAdapters.end())
		return false;

	return (std::find((playingDevicesiter->second).begin(), (playingDevicesiter->second).end(), address) != (playingDevicesiter->second).end());
}

void BluetoothA2dpProfileService::markDeviceAsPlaying(const std::string &adapterAddress, const std::string &address)
{
	auto playingDevicesiter = mPlayingDevicesForMultipleAdapters.find(adapterAddress);
	if (playingDevicesiter == mPlayingDevicesForMultipleAdapters.end())
	{
		std::vector<std::string> playingDevices;
		playingDevices.push_back(address);

		mPlayingDevicesForMultipleAdapters.insert(std::pair<std::string, std::vector<std::string>>(adapterAddress, playingDevices));
		return;
	}

	if (!isDevicePlaying(adapterAddress, address))
		(playingDevicesiter->second).push_back(address);
}

void BluetoothA2dpProfileService::markDeviceAsNotPlaying(const std::string &adapterAddress, const std::string &address)
{
	auto playingDevicesiter = mPlayingDevicesForMultipleAdapters.find(adapterAddress);
	if (playingDevicesiter == mPlayingDevicesForMultipleAdapters.end())
		return;

	auto deviceIter = std::find((playingDevicesiter->second).begin(), (playingDevicesiter->second).end(), address);

	if (deviceIter == (playingDevicesiter->second).end())
		return;

	(playingDevicesiter->second).erase(deviceIter);
}
