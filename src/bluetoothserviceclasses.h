// Copyright (c) 2014-2018 LG Electronics, Inc.
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


#ifndef BLUETOOTHPROFILES_H
#define BLUETOOTHPROFILES_H

#include <string>
#include <unordered_map>

class BluetoothServiceClassInfo
{
public:
	BluetoothServiceClassInfo(const std::string &mnemonic, const std::string &category = "") :
		mnemonic(mnemonic),
		methodCategory(category)
	{
	}

	BluetoothServiceClassInfo(const BluetoothServiceClassInfo& other) :
		mnemonic(other.getMnemonic()),
		methodCategory(other.getMethodCategory())
	{
	}

	std::string getMnemonic() const { return mnemonic; }
	std::string getMethodCategory() const { return methodCategory; }

	bool isSupported() { return !methodCategory.empty(); }

private:
	/**
	 * @brief Mnemonic for the service class.
	 */
	std::string mnemonic;

	/**
	 * @brief LS2 method category used for the service class.
	 *
	 *        If no category is assigned yet then the service class isn't
	 *        supported by the API yet.
	 */
	std::string methodCategory;
};

/**
 * All Bluetooth service classes that we currently know about.
 *
 * Each service class has a unique UUID and a corresponding LS2 category name.
 * If a role doesn't have a category name assigned in this map then we do not
 * yet support it in our service implementation.
 *
 * The mnemonic for a service class is formed using the following scheme:
 *   <profile-abbrev>[-<role-abbrev>]
 * where <profile-abbrev> is taken the first column shown on
 * https://developer.bluetooth.org/TechnologyOverview/Pages/Profiles.aspx
 * and the optional <role-abbrev> is taken from the profile's specification.
 *
 * The list is not complete and should be extended when service classes are
 * found to be missing.
 **/
const std::unordered_map<std::string, BluetoothServiceClassInfo> allServiceClasses = {
	{ "00001203-0000-1000-8000-00805f9b34fb", { "GAVDP" } },
	{ "00001108-0000-1000-8000-00805f9b34fb", { "HSP-HS" } },
	{ "00001112-0000-1000-8000-00805f9b34fb", { "HSP-AG" } },
	{ "0000111e-0000-1000-8000-00805f9b34fb", { "HFP-HF", "/hfp" } },
	{ "0000111f-0000-1000-8000-00805f9b34fb", { "HFP-AG", "/hfp" } },
	{ "0000110d-0000-1000-8000-00805f9b34fb", { "A2DP" } },
	{ "0000110a-0000-1000-8000-00805f9b34fb", { "A2DP-source", "/a2dp" } },
	{ "0000110b-0000-1000-8000-00805f9b34fb", { "A2DP-sink", "/a2dp" } },
	{ "0000110e-0000-1000-8000-00805f9b34fb", { "AVRCP-remote", "/avrcp" } },
	{ "0000110c-0000-1000-8000-00805f9b34fb", { "AVRCP-target", "/avrcp" } },
	{ "00001115-0000-1000-8000-00805f9b34fb", { "PANU", "/pan" } },
	{ "00001116-0000-1000-8000-00805f9b34fb", { "NAP", "/pan" } },
	{ "00001117-0000-1000-8000-00805f9b34fb", { "GN" } },
	{ "0000000f-0000-1000-8000-00805f9b34fb", { "BNEP" } },
	{ "00002a50-0000-1000-8000-00805f9b34fb", { "PNPID" } },
	{ "0000180a-0000-1000-8000-00805f9b34fb", { "DI" } },
	{ "00001801-0000-1000-8000-00805f9b34fb", { "GATT", "/gatt" } },
	{ "00001802-0000-1000-8000-00805f9b34fb", { "IAS" } },
	{ "00001803-0000-1000-8000-00805f9b34fb", { "LLS" } },
	{ "00001804-0000-1000-8000-00805f9b34fb", { "TPS" } },
	{ "0000180f-0000-1000-8000-00805f9b34fb", { "BAS" } },
	{ "00001813-0000-1000-8000-00805f9b34fb", { "SCPP" } },
	{ "0000112d-0000-1000-8000-00805f9b34fb", { "SAP" } },
	{ "00000003-0000-1000-8000-00805f9b34fb", { "RFCOMM" } },
	{ "00001400-0000-1000-8000-00805f9b34fb", { "HDP" } },
	{ "00001401-0000-1000-8000-00805f9b34fb", { "HDP-source" } },
	{ "00001402-0000-1000-8000-00805f9b34fb", { "HDP-sink" } },
	{ "00001124-0000-1000-8000-00805f9b34fb", { "HID" } },
	{ "00000011-0000-1000-8000-00805f9b34fb", { "HID-host", "/hid" } },
	{ "00001103-0000-1000-8000-00805f9b34fb", { "DUN" } },
	{ "00001800-0000-1000-8000-00805f9b34fb", { "GAP" } },
	{ "00001200-0000-1000-8000-00805f9b34fb", { "PNP" } },
	{ "00001101-0000-1000-8000-00805f9b34fb", { "SPP", "/spp" } },
	{ "00001104-0000-1000-8000-00805f9b34fb", { "SYNC" } },
	{ "00001105-0000-1000-8000-00805f9b34fb", { "OPP", "/opp" } },
	{ "00001106-0000-1000-8000-00805f9b34fb", { "FTP", "/ftp" } },
	{ "0000112e-0000-1000-8000-00805f9b34fb", { "PCE" } },
	{ "0000112f-0000-1000-8000-00805f9b34fb", { "PSE" } },
	{ "00001130-0000-1000-8000-00805f9b34fb", { "PBAP", "/pbap" } },
	{ "00001132-0000-1000-8000-00805f9b34fb", { "MAS", "/map" } },
	{ "00001133-0000-1000-8000-00805f9b34fb", { "MNS", "/map" } },
	{ "00001134-0000-1000-8000-00805f9b34fb", { "MAP", "/map" } },
	{ "00001827-0000-1000-8000-00805f9b34fb", { "MESH", "/mesh"} }
};

#endif // BLUETOOTHPROFILES_H
