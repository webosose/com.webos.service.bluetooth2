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


#include "bluetoothpanprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "logging.h"

BluetoothPanProfileService::BluetoothPanProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "PAN", "00001115-0000-1000-8000-00805f9b34fb",
        "00001116-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		/*LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothPanProfileService, setTethering)*/
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/pan", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/pan", this);
}

BluetoothPanProfileService::~BluetoothPanProfileService()
{
}

/**
Enable or disable NAP for Bluetooth tethering

@par Parameters

Name | Required | Type | Description
-----|--------|------|----------
tethering | Yes | Boolean | true or false to enable or disable NAP
adapterAddress | No | String | Address of the adapter executing this method

@par Returns(Call)

Name | Required | Type | Description
-----|--------|------|----------
returnValue | Yes | Boolean | Value is true if the channel was successfully created, false otherwise.
adapterAddress | Yes | String | Address of the adapter executing this method
errorText | No | String | errorText contains the error text if the method fails. The method will return errorText only if it fails.
errorCode | No | Number | errorCode contains the error code if the method fails. The method will return errorCode only if it fails.

@par Returns(Subscription)

Not applicable
*/
bool BluetoothPanProfileService::setTethering(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothPanProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(tethering, boolean), PROP(adapterAddress, string)) REQUIRED_1(tethering));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);
		else if (!requestObj.hasKey("tethering"))
			LSUtils::respondWithError(request, BT_ERR_PAN_TETHERING_PARAM_MISSING);
		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	bool enable = requestObj["tethering"].asBool();

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto setTetheringCallback = [requestMessage, adapterAddress](BluetoothError error) {

		LS::Message request(requestMessage);
		if (error != BLUETOOTH_ERROR_NONE)
		{
			if (BLUETOOTH_ERROR_TETHERING_ALREADY_ENABLED == error || BLUETOOTH_ERROR_TETHERING_ALREADY_DISABLED == error)
				LSUtils::respondWithError(request, error);
			else
				LSUtils::respondWithError(request, BT_ERR_PAN_SET_TETHERING_FAILED);
			LSMessageUnref(request.get());
			return;
		}

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		LSUtils::postToClient(request, responseObj);

		LSMessageUnref(request.get());
	};

	getImpl<BluetoothPanProfile>()->setTethering(enable, setTetheringCallback);

	return true;
}
