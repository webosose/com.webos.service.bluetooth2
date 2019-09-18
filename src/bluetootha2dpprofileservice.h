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


#ifndef BLUETOOTHA2DPPROFILESERVICE_H
#define BLUETOOTHA2DPPROFILESERVICE_H

#include <string>
#include <map>

#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>

#include "bluetoothprofileservice.h"

#define DELETE_OBJ(del_obj) if(del_obj) { delete del_obj; del_obj = 0; }
#define AUDIO_PAHT_TIMEOUT 2

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

class AudioSocketInfo
{
public:
	std::string deviceAddress;
	std::string path;
	BluetoothA2dpAudioSocketType type;
	bool isIn;
} ;

class SbcConfigurationInfo
{
public:
	int32_t sampleFrequency;
	std::string channelMode;
	int32_t blockLength;
	int32_t subbands;
	std::string allocationMethod;
	int32_t minBitpool;
	int32_t maxBitpool;
};

class AptxConfigurationInfo
{
public:
	int32_t sampleFrequency;
	std::string channelMode;
};

class BluetoothA2dpProfileService : public BluetoothProfileService,
                                    public BluetoothA2dpStatusObserver
{
public:
	BluetoothA2dpProfileService(BluetoothManagerService *manager);
	~BluetoothA2dpProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);
	bool startStreaming(LSMessage &message);
	bool stopStreaming(LSMessage &message);
	bool getAudioPath(LSMessage &message);
	bool setSbcEncoderBitpool(LSMessage &message);
	bool getCodecConfiguration(LSMessage &message);

	void stateChanged(std::string address, BluetoothA2dpProfileState state);
	void stateChanged(const std::string adapterAddress, const std::string address, BluetoothA2dpProfileState state);
	void audioSocketCreated(const std::string &address, const std::string &path, BluetoothA2dpAudioSocketType type, bool isIn);
	void audioSocketDestroyed(const std::string &address, const std::string &path, BluetoothA2dpAudioSocketType type, bool isIn);
	void sbcConfiguraionChanged(const std::string &address, const BluetoothSbcConfiguration &sbcConfiguration);
	void aptxConfigurationChanged(const std::string &address, const BluetoothAptxConfiguration &aptxConfiguration);

protected:
	pbnjson::JValue buildGetStatusResp(bool connected, bool connecting, bool subscribed, bool returnValue,
                                              std::string adapterAddress, std::string deviceAddress);

private:
	void handleGetCodecConfigurationClientDisappeared(const std::string &adapterAddress, const std::string &address);
	void removeGetCodecConfiguraionForDevice(const std::string &address, bool disconnected, bool remoteDisconnect);

	void notifySubscribersAboutSbcConfiguration(std::string address);
	void notifySubscribersAboutAptxConfiguration(std::string address);

	int sbcSamplingFrequencyEnumToInteger(BluetoothSbcConfiguration::SampleFrequency sampleFrequency);
	std::string sbcChannelModeEnumToString(BluetoothSbcConfiguration::ChannelMode channelMode);
	int sbcBlockLengthEnumToInteger(BluetoothSbcConfiguration::BlockLength blockLength);
	int sbcSubbandsEnumToInteger(BluetoothSbcConfiguration::Subbands subbands);
	std::string sbcAllocationMethodEnumToString(BluetoothSbcConfiguration::AllocationMethod allocationMethod);
	int aptxSamplingFrequencyEnumToInteger(BluetoothAptxConfiguration::SampleFrequency sampleFrequency);
	std::string aptxChannelModeEnumToString(BluetoothAptxConfiguration::ChannelMode channelMode);

	bool isDevicePlaying(const std::string &adapterAddress, const std::string &address);
	void markDeviceAsPlaying(const std::string &adapterAddress, const std::string &address);
	void markDeviceAsNotPlaying(const std::string &adapterAddress, const std::string &address);

private:
	std::vector<std::string> mPlayingDevices;
	AudioSocketInfo *mAudioSocketInfo;
	SbcConfigurationInfo *mSbcConfigurationInfo;
	AptxConfigurationInfo *mAptxConfigurationInfo;
	std::unordered_map<std::string, LSUtils::ClientWatch*> mGetCodecConfigurationWatches;

	std::map<std::string, std::vector<std::string>> mPlayingDevicesForMultipleAdapters;
};

class A2DPAudioPathCheckTimeout {
public:
	BluetoothA2dpProfileService *serviceRef;
	LSMessage *requestMessage;
	std::string adapterAddress;
	std::string address;
};

#endif // BLUETOOTHA2DPPROFILESERVICE_H
