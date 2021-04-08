// Copyright (c) 2014-2020 LG Electronics, Inc.
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


#ifndef BLUETOOTH_ERRORS_H_
#define BLUETOOTH_ERRORS_H_

#include <bluetooth-sil-api.h>
#include <pbnjson.hpp>

enum BluetoothErrorCode
{
	BT_ERR_ADAPTER_NOT_AVAILABLE = 101,
	BT_ERR_MSG_PARSE_FAIL = 102,
	BT_ERR_MTHD_NOT_SUBSCRIBED = 103,
	BT_ERR_ALLOW_ONE_SUBSCRIBE = 104,
	BT_ERR_ADDR_PARAM_MISSING = 105,
	BT_ERR_DEVICE_NOT_AVAIL = 106,
	BT_ERR_PAIRING_CANCELED = 107,
	BT_ERR_NO_PAIRING = 108,
	BT_ERR_DISCOVERY_TO_NEG_VALUE = 109,
	BT_ERR_DISCOVERABLE_TO_NEG_VALUE = 110,
	BT_ERR_PAIRABLE_TO_NEG_VALUE = 111,
	BT_ERR_POWER_STATE_CHANGE_FAIL = 112,
	BT_ERR_ADAPTER_PROPERTY_FAIL = 113,
	BT_ERR_START_DISC_ADAPTER_OFF_ERR = 114,
	BT_ERR_START_DISC_FAIL = 115,
	BT_ERR_DISC_STOP_ADAPTER_OFF_ERR = 116,
	BT_ERR_STOP_DISC_FAIL = 117,
	BT_ERR_PAIRING_IN_PROG = 118,
	BT_ERR_PASSKEY_PARAM_MISSING = 119,
	BT_ERR_PIN_PARAM_MISSING = 120,
	BT_ERR_ACCEPT_PARAM_MISSING = 121,
	BT_ERR_UNPAIR_FAIL = 122,
	BT_ERR_PAIRABLE_FAIL = 123,
	BT_ERR_PAIRING_CANCEL_TO = 124,
	BT_ERR_INCOMING_PAIR_DEV_UNAVAIL = 125,
	BT_ERR_PAIRABLE_TO = 126,
	BT_ERR_PROFILE_UNAVAIL = 127,
	BT_ERR_DEV_CONNECTING = 128,
	BT_ERR_DEV_NOT_PAIRED = 129,
	BT_ERR_PROFILE_CONNECT_FAIL = 130,
	BT_ERR_PROFILE_CONNECTED = 131,
	BT_ERR_PROFILE_DISCONNECT_FAIL = 132,
	BT_ERR_PROFILE_STATE_ERR = 133,
	BT_ERR_DIRPATH_PARAM_MISSING = 134,
	BT_ERR_LIST_FOLDER_FAIL = 135,
	BT_ERR_PROFILE_NOT_CONNECTED = 136,
	BT_ERR_SRCFILE_PARAM_MISSING = 137,
	BT_ERR_DESTFILE_PARAM_MISSING = 138,
	BT_ERR_DESTPATH_INVALID = 139,
	BT_ERR_FTP_PUSH_PULL_FAIL = 140,
	BT_ERR_FTP_TRANSFER_CANCELED = 141,
	BT_ERR_SRCFILE_INVALID = 142,
	BT_ERR_BAD_JSON = 143,
	BT_ERR_SCHEMA_VALIDATION_FAIL = 144,
	BT_ERR_INVALID_DIRPATH = 145,
	BT_ERR_INVALID_SRCFILE_PATH = 146,
	BT_ERR_INVALID_DESTFILE_PATH = 147,
	BT_ERR_NO_PROP_CHANGE = 148,
	BT_ERR_DEVICE_PROPERTY_FAIL = 149,
	BT_ERR_OPP_CONNECT_FAIL = 150,
	BT_ERR_OPP_CONNECTED = 151,
	BT_ERR_OPP_TRANSFER_CANCELED = 152,
	BT_ERR_OPP_PUSH_PULL_FAIL = 153,
	BT_ERR_OPP_NOT_CONNECTED = 154,
	BT_ERR_OPP_TRANSFER_NOT_ALLOWED = 155,
	BT_ERR_OPP_REQUESTID_PARAM_MISSING = 156,
	BT_ERR_OPP_STATE_ERR = 157,
	BT_ERR_OPP_REQUESTID_NOT_EXIST = 158,
	BT_ERR_OPP_ALREADY_ACCEPT_FILE = 159,
	BT_ERR_OPP_TRANSFERID_NOT_EXIST = 160,
	BT_ERR_ADAPTER_TURNED_OFF = 161,
	BT_ERR_INVALID_ADAPTER_ADDRESS = 162,
	BT_ERR_GATT_SERVICE_NAME_PARAM_MISSING = 163,
	BT_ERR_GATT_CHARACTERISTIC_PARAM_MISSING = 164,
	BT_ERR_GATT_CHARACTERISTC_VALUE_PARAM_MISSING = 165,
	BT_ERR_GATT_CHARACTERISTICS_PARAM_MISSING = 166,
	BT_ERR_GATT_DESCRIPTOR_INFO_PARAM_MISSING = 167,
	BT_ERR_GATT_SERVICE_DISCOVERY_FAIL = 168,
	BT_ERR_GATT_DISCOVERY_INVALID_PARAM = 169,
	BT_ERR_GATT_ADD_SERVICE_FAIL = 170,
	BT_ERR_GATT_REMOVE_SERVICE_FAIL = 171,
	BT_ERR_GATT_WRITE_CHARACTERISTIC_FAIL = 172,
	BT_ERR_GATT_INVALID_CHARACTERISTIC = 173,
	BT_ERR_GATT_READ_CHARACTERISTIC_FAIL = 174,
	BT_ERR_GATT_CHARACTERISTC_INVALID_VALUE_PARAM = 175,
	BT_ERR_GATT_MONITOR_CHARACTERISTIC_FAIL = 176,
	BT_ERR_GATT_INVALID_SERVICE = 177,
	BT_ERR_A2DP_START_STREAMING_FAILED = 178,
	BT_ERR_A2DP_STOP_STREAMING_FAILED = 179,
	BT_ERR_A2DP_DEVICE_ADDRESS_PARAM_MISSING = 180,
	BT_ERR_PBAP_REQUESTID_PARAM_MISSING = 181,
	BT_ERR_PBAP_ACCESS_NOT_ALLOWED = 182,
	BT_ERR_PBAP_REQUESTID_NOT_EXIST = 183,
	BT_ERR_PBAP_ACCESS_REQUEST_NOT_EXIST = 184,
	BT_ERR_PBAP_STATE_ERR = 185,
	BT_ERR_AVRCP_REQUESTID_PARAM_MISSING = 186,
	BT_ERR_AVRCP_REQUEST_NOT_ALLOWED = 187,
	BT_ERR_AVRCP_REQUESTID_NOT_EXIST = 188,
	BT_ERR_AVRCP_STATE_ERR = 189,
	BT_ERR_GATT_DESCRIPTORS_PARAM_MISSING = 190,
	BT_ERR_GATT_INVALID_DESCRIPTOR = 191,
	BT_ERR_GATT_READ_DESCRIPTORS_FAIL = 192,
	BT_ERR_GATT_DESCRIPTOR_PARAM_MISSING = 193,
	BT_ERR_GATT_DESCRIPTOR_VALUE_PARAM_MISSING = 194,
	BT_ERR_GATT_DESCRIPTOR_INVALID_VALUE_PARAM = 195,
	BT_ERR_GATT_WRITE_DESCRIPTOR_FAIL = 196,
	BT_ERR_NO_PAIRING_FOR_REQUESTED_ADDRESS = 197,
	BT_ERR_HFP_OPEN_SCO_FAILED = 198,
	BT_ERR_HFP_CLOSE_SCO_FAILED = 199,
	BT_ERR_HFP_RESULT_CODE_PARAM_MISSING = 200,
	BT_ERR_HFP_WRITE_RESULT_CODE_FAILED = 201,
	BT_ERR_HFP_WRITE_RING_RESULT_CODE_FAILED = 202,
	BT_ERR_SPP_UUID_PARAM_MISSING = 203,
	BT_ERR_SPP_CHANNELID_PARAM_MISSING = 204,
	BT_ERR_SPP_NAME_PARAM_MISSING = 205,
	BT_ERR_SPP_DATA_PARAM_MISSING = 206,
	BT_ERR_SPP_SIZE_PARAM_MISSING = 207,
	BT_ERR_SPP_WRITE_DATA_FAILED = 208,
	BT_ERR_SPP_CHANNELID_NOT_AVAILABLE = 209,
	BT_ERR_SPP_SIZE_NOT_AVAILABLE = 210,
	BT_ERR_SPP_TIMEOUT_NOT_AVAILABLE = 211,
	BT_ERR_SPP_PERMISSION_DENIED = 212,
	BT_ERR_BLE_ADV_CONFIG_FAIL = 213,
	BT_ERR_BLE_ADV_CONFIG_DATA_PARAM_MISSING = 215,
	BT_ERR_BLE_ADV_CONFIG_EXCESS_DATA_PARAM = 216,
	BT_ERR_BLE_ADV_ALREADY_ADVERTISING = 217,
	BT_ERR_BLE_ADV_SERVICE_DATA_FAIL = 218,
	BT_ERR_BLE_ADV_UUID_FAIL = 219,
	BT_ERR_SPP_APPID_PARAM_MISSING = 220,
	BT_ERR_HFP_ALLOW_ONE_SUBSCRIBE_PER_DEVICE = 221,
	BT_ERR_PAN_SET_TETHERING_FAILED = 222,
	BT_ERR_PAN_TETHERING_PARAM_MISSING = 223,
	BT_ERR_SPP_CREATE_CHANNEL_FAILED = 224,
	BT_ERR_HFP_ATCMD_MISSING = 225,
	BT_ERR_HFP_TYPE_MISSING = 226,
	BT_ERR_HFP_SEND_AT_FAIL = 227,
	BT_ERR_ANCS_NOTIFICATIONID_PARAM_MISSING = 228,
	BT_ERR_ANCS_ACTIONID_PARAM_MISSING = 229,
	BT_ERR_ANCS_NOTIF_ACTION_NOT_ALLOWED = 230,
	BT_ERR_ANCS_NOTIFICATION_ACTION_FAIL = 231,
	BT_ERR_ANCS_ATTRIBUTELIST_PARAM_MISSING = 232,
	BT_ERR_ANCS_ATTRIBUTE_PARAM_INVAL = 233,
	BT_ERR_ALLOW_ONE_ANCS_QUERY= 234,
	BT_ERR_HID_DATA_PARAM_MISSING = 235,
	BT_ERR_HID_DATA_PARAM_INVALID = 236,
	BT_ERR_AVRCP_DEVICE_ADDRESS_PARAM_MISSING = 237,
	BT_ERR_AVRCP_KEY_CODE_PARAM_MISSING = 238,
	BT_ERR_AVRCP_KEY_STATUS_PARAM_MISSING = 239,
	BT_ERR_AVRCP_KEY_CODE_INVALID_VALUE_PARAM = 240,
	BT_ERR_AVRCP_KEY_STATUS_INVALID_VALUE_PARAM = 241,
	BT_ERR_AVRCP_SEND_PASS_THROUGH_COMMAND_FAILED = 242,
	BT_ERR_AVRCP_EQUALIZER_INVALID_VALUE_PARAM = 243,
	BT_ERR_AVRCP_REPEAT_INVALID_VALUE_PARAM = 244,
	BT_ERR_AVRCP_SHUFFLE_INVALID_VALUE_PARAM = 245,
	BT_ERR_AVRCP_SCAN_INVALID_VALUE_PARAM = 246,
	BT_ERR_A2DP_GET_SOCKET_PATH_FAILED = 247,
	BT_ERR_PROFILE_ENABLED = 248,
	BT_ERR_PROFILE_NOT_ENABLED = 249,
	BT_ERR_PROFILE_ENABLE_FAIL = 250,
	BT_ERR_PROFILE_DISABLE_FAIL = 251,
	BT_ERR_AVRCP_VOLUME_PARAM_MISSING = 252,
	BT_ERR_AVRCP_VOLUME_INVALID_VALUE_PARAM = 253,
	BT_ERR_AVRCP_SET_ABSOLUTE_VOLUME_FAILED = 254,
	BT_ERR_WOBLE_SET_WOBLE_PARAM_MISSING = 255,
	BT_ERR_WOBLE_SET_WOBLE_TRIGGER_DEVICES_PARAM_MISSING = 256,
	BT_ERR_BLE_ADV_NO_MORE_ADVERTISER = 257,
	BT_ERR_A2DP_SBC_ENCODER_BITPOOL_MISSING = 258,
	BT_ERR_HID_DEVICE_ADDRESS_PARAM_MISSING = 259,
	BT_ERR_HID_REPORT_ID_PARAM_MISSING = 260,
	BT_ERR_HID_REPORT_TYPE_PARAM_MISSING = 261,
	BT_ERR_HID_REPORT_TYPE_INVALID_VALUE_PARAM = 262,
	BT_ERR_HID_REPORT_DATA_PARAM_MISSING = 263,
	BT_ERR_STACK_TRACE_STATE_CHANGE_FAIL = 264,
	BT_ERR_SNOOP_TRACE_STATE_CHANGE_FAIL = 265,
	BT_ERR_STACK_TRACE_LEVEL_CHANGE_FAIL = 266,
	BT_ERR_TRACE_OVERWRITE_CHANGE_FAIL = 267,
	BT_ERR_STACK_LOG_PATH_CHANGE_FAIL = 268,
	BT_ERR_SNOOP_LOG_PATH_CHANGE_FAIL = 269,
	BT_ERR_GATT_CHARACTERISTC_WRITE_TYPE_PARAM_MISSING = 270,
	BT_ERR_KEEP_ALIVE_INTERVAL_CHANGE_FAIL = 271,
	BT_ERR_GATT_HANDLES_PARAM_MISSING = 272,
	BT_ERR_GATT_HANDLE_PARAM_MISSING = 273,
	BT_ERR_APPID_PARAM_MISSING = 274,
	BT_ERR_CONNID_PARAM_MISSING = 275,
	BT_ERR_MESSAGE_OWNER_MISSING = 276,
	BT_ERR_GATT_SERVER_NAME_PARAM_MISSING = 277,
	BT_ERR_GATT_APPLICATION_ID_PARAM_MISSING = 278,
	BT_ERR_GATT_REMOVE_SERVER_FAIL = 279,
	BT_ERR_GATT_ADVERTISERID_PARAM_MISSING = 280,
	BT_ERR_GATT_SERVERID_PARAM_MISSING = 281,
	BT_ERR_GATT_READ_DESCRIPTOR_FAIL = 282,
	BT_ERR_CLIENTID_PARAM_MISSING = 283,
	BT_ERR_BLE_ADV_EXCEED_SIZE_LIMIT = 284,
	BT_ERR_GATT_INSTANCE_ID_NOT_SUPPORTED = 285,
	BT_ERR_API_NOT_SUPPORTED_BY_STACK = 286,
	BT_ERR_DELAY_REPORTING_ALREADY_ENABLED = 287,
	BT_ERR_DELAY_REPORTING_ALREADY_DISABLED = 288,
	BT_ERR_DELAY_REPORTING_DISABLED = 289,
	BT_ERR_PBAP_OBJECT_PARAM_MISSING = 290,
	BT_ERR_PBAP_REPOSITORY_PARAM_MISSING = 291,
	BT_ERR_NOT_NOT_SUPPORTED_BY_REMOTE_DEVICE = 292,
	BT_ERR_PBAP_VCARD_HANDLE_PARAM_MISSING = 293,
	BT_ERR_PBAP_FILTER_PARAM_MISSING = 294,
	BT_ERR_AVRCP_NO_CONNECTED_DEVICES = 295,
	BT_ERR_MAP_INSTANCE_NOT_EXIST = 296,
	BT_ERR_MAP_SESSION_ID_NOT_EXIST = 297,
	BT_ERR_MAP_SESSION_ID_PARAM_MISSING = 298,
	BT_ERR_MAP_FOLDER_PARAM_MISSING = 299,
	BT_ERR_AVRCP_START_INDEX_PARAM_MISSING = 300,
	BT_ERR_AVRCP_END_INDEX_PARAM_MISSING = 301,
	BT_ERR_AVRCP_ITEM_PATH_PARAM_MISSING = 302,
	BT_ERR_AVRCP_SEARCH_STRING_PARAM_MISSING = 303,
	BT_ERR_MAP_HANDLE_PARAM_MISSING = 304,
	BT_ERR_MAP_INSTANCE_ALREADY_CONNECTED = 305,
	BT_ERR_MAP_STATUS_INDICATOR_PARAM_MISSING = 306,
	BT_ERR_MAP_STATUS_VALUE_PARAM_MISSING = 307,
	BT_ERR_AVRCP_PLAYBACK_STATUS_PARAM_MISSING = 308,
	BT_ERR_MESH_NETINDEX_PARAM_MISSING = 309,
	BT_ERR_MESH_APPINDEX_PARAM_MISSING = 310,
	BT_ERR_MESH_DEST_ADDRESS_PARAM_MISSING = 311,
	BT_ERR_MESH_SRC_ADDRESS_PARAM_MISSING = 312,
	BT_ERR_MESH_BEARER_PARAM_MISSING = 313,
	BT_ERR_MESH_UUID_PARAM_MISSING = 314,
	BT_ERR_MESH_DATA_PARAM_MISSING = 315,
	BT_ERR_MESH_MODELID_PARAM_MISSING = 316,
	BT_ERR_MESH_TTL_PARAM_MISSING = 317,
	BT_ERR_MESH_GATT_PROXY_STATE_PARAM_MISSING = 318,
	BT_ERR_MESH_HB_PUB_STATUS_PARAM_MISSING = 319,
	BT_ERR_MESH_PUB_STATUS_PARAM_MISSING = 320,
	BT_ERR_MESH_NODE_IDENTITY_PARAM_MISSING = 321,
	BT_ERR_MESH_RELAY_STATUS_PARAM_MISSING = 322,
	BT_ERR_MESH_NUMBER_PARAM_MISSING = 323,
	BT_ERR_MESH_OOB_DATA_PARAM_MISSING = 324,
	BT_ERR_MESH_ONOFF_PARAM_MISSING = 325,
	BT_ERR_MESH_ADAPTER_NOT_AUTHORIZED = 326,
	BT_ERR_MESH_NETWORK_NOT_CREATED = 327
};

void appendErrorResponse(pbnjson::JValue &obj, BluetoothError errorCode);
const std::string retrieveErrorText(BluetoothErrorCode errorCode);
const std::string retrieveErrorCodeText(BluetoothError errorCode);

#endif //BLUETOOTH_ERRORS_H_
