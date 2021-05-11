// Copyright (c) 2016-2021 LG Electronics, Inc.
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


#include "bluetoothhidprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "ls2utils.h"
#include "utils.h"
#include "logging.h"

BluetoothHidProfileService::BluetoothHidProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "HID", "00000011-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
	LS_CREATE_CATEGORY_END

	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, internal)
		LS_CATEGORY_CLASS_METHOD(BluetoothHidProfileService, getReport)
		LS_CATEGORY_CLASS_METHOD(BluetoothHidProfileService, setReport)
		LS_CATEGORY_CLASS_METHOD(BluetoothHidProfileService, sendData)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/hid", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/hid", this);

	manager->registerCategory("/hid/internal", LS_CATEGORY_TABLE_NAME(internal), NULL, NULL);
	manager->setCategoryData("/hid/internal", this);
}

BluetoothHidProfileService::~BluetoothHidProfileService()
{
}

void BluetoothHidProfileService::initialize()
{
	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothHidProfile>()->registerObserver(this);
}

HidReportType reportTypeStringToEnum(const std::string str)
{
	if (str == "input")
		return HidReportType::HID_REPORT_INPUT;
	else if (str == "output")
		return HidReportType::HID_REPORT_OUTPUT;
	else
		return HidReportType::HID_REPORT_FEATURE;
}

bool BluetoothHidProfileService::getReport(LSMessage &message)
{
	BT_INFO("HID", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHidProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(adapterAddress, string),
								PROP(reportType, string), PROP(reportId, integer), PROP(reportSize, integer))
								REQUIRED_3(address, reportType, reportId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_HID_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("reportType"))
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_TYPE_PARAM_MISSING);
		else if (!requestObj.hasKey("reportId"))
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_ID_PARAM_MISSING);
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

	HidReportType reportType = HidReportType::HID_REPORT_INPUT;
	if (requestObj.hasKey("reportType"))
	{
		std::string reportTypeStr = requestObj["reportType"].asString();

		if (reportTypeStr != "input" && reportTypeStr != "output" && reportTypeStr != "feature")
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_TYPE_INVALID_VALUE_PARAM);

		reportType = reportTypeStringToEnum(reportTypeStr);
	}

	uint8_t reportId = (uint8_t)requestObj["reportId"].asNumber<int32_t>();

	uint16_t reportSize = 0;
	if (requestObj.hasKey("reportSize"))
		reportSize = requestObj["reportSize"].asNumber<int32_t>();

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto getReportCallback  = [requestMessage, adapterAddress, deviceAddress](BluetoothError error, const uint8_t *data, size_t size) {
			BT_INFO("HID", 0, "Return of getReport is %d", error);

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

			if (!deviceAddress.empty())
				responseObj.put("address", deviceAddress);

			pbnjson::JValue reportDataObj = pbnjson::Array();

			for (size_t i = 0; i < size; i ++)
				reportDataObj.append((int32_t)data[i]);

			responseObj.put("reportData", reportDataObj);

			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
	};
	BT_INFO("HID", 0, "Service calls SIL API : getReport");
	getImpl<BluetoothHidProfile>()->getReport(deviceAddress, reportType, reportId, reportSize, getReportCallback);

	return true;
}

bool BluetoothHidProfileService::setReport(LSMessage &message)
{
	BT_INFO("HID", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHidProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(adapterAddress, string),
								PROP(reportType, string), ARRAY(reportData, integer)) REQUIRED_3(address, reportType, reportData));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_HID_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("reportType"))
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_TYPE_PARAM_MISSING);
		else if (!requestObj.hasKey("reportData"))
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_DATA_PARAM_MISSING);
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

	HidReportType reportType = HidReportType::HID_REPORT_INPUT;
	if (requestObj.hasKey("reportType"))
	{
		std::string reportTypeStr = requestObj["reportType"].asString();

		if (reportTypeStr != "input" && reportTypeStr != "output" && reportTypeStr != "feature")
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_TYPE_INVALID_VALUE_PARAM);

		reportType = reportTypeStringToEnum(reportTypeStr);
	}

	auto reportDataObjArray = requestObj["reportData"];
	uint16_t reportSize = reportDataObjArray.arraySize();
	uint8_t reportData[reportSize];

	for (int n = 0; n < reportDataObjArray.arraySize(); n++)
	{
		reportData[n] = (uint8_t)reportDataObjArray[n].asNumber<int32_t>();
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto setReportCallback  = [requestMessage, adapterAddress, deviceAddress](BluetoothError error) {

			BT_INFO("HID", 0, "Return of setReport is %d", error);
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

			if (!deviceAddress.empty())
				responseObj.put("address", deviceAddress);

			LSUtils::postToClient(requestMessage, responseObj);
			LSMessageUnref(requestMessage);
	};

	BT_INFO("HID", 0, "Service calls SIL API : setReport");
	getImpl<BluetoothHidProfile>()->setReport(deviceAddress,reportType, reportData, reportSize, setReportCallback);

	return true;
}

bool BluetoothHidProfileService::sendData(LSMessage &message)
{
	BT_INFO("HID", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothHidProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(adapterAddress, string),
								ARRAY(reportData, integer)) REQUIRED_2(address, reportData));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_HID_DEVICE_ADDRESS_PARAM_MISSING);
		else if (!requestObj.hasKey("reportData"))
			LSUtils::respondWithError(request, BT_ERR_HID_REPORT_DATA_PARAM_MISSING);
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

	auto dataObjArray = requestObj["reportData"];
	uint16_t dataSize = dataObjArray.arraySize();
	uint8_t data[dataSize];

	for (int n = 0; n < dataObjArray.arraySize(); n++)
	{
		data[n] = (uint8_t)dataObjArray[n].asNumber<int32_t>();
	}

	BT_INFO("HID", 0, "Service calls SIL API : sendData");
	BluetoothError error = getImpl<BluetoothHidProfile>()->sendData(deviceAddress, data, dataSize);
	BT_INFO("HID", 0, "Return of sendData is %d", error);

	pbnjson::JValue responseObj = pbnjson::Object();
	if (BLUETOOTH_ERROR_NONE != error)
	{
		appendErrorResponse(responseObj, error);
		LSUtils::postToClient(request, responseObj);
		return true;
	}

	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("address", deviceAddress);

	LSUtils::postToClient(request, responseObj);

	return true;
}
