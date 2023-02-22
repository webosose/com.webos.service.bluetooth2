// Copyright (c) 2014-2021 LG Electronics, Inc.
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

#include <inttypes.h>
#include "bluetoothftpprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "config.h"

using namespace std::placeholders;

BluetoothFtpProfileService::Transfer::~Transfer()
{
	if (watch)
		delete watch;
}

BluetoothFtpProfileService::BluetoothFtpProfileService(BluetoothManagerService *manager) :
	BluetoothProfileService(manager, "FTP", "00001106-0000-1000-8000-00805f9b34fb")
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		/*LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothFtpProfileService, listDirectory)
		LS_CATEGORY_CLASS_METHOD(BluetoothFtpProfileService, pullFile)
		LS_CATEGORY_CLASS_METHOD(BluetoothFtpProfileService, pushFile)*/
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/ftp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/ftp", this);
}

BluetoothFtpProfileService::~BluetoothFtpProfileService()
{
}

void BluetoothFtpProfileService::cancelTransfer(BluetoothFtpTransferId id, bool clientDisappeared)
{
	auto transferIter = mTransfers.find(id);
	if (transferIter == mTransfers.end())
		return;

	Transfer *transfer = transferIter->second;

        BT_DEBUG("Cancel FTP transfer %" PRIu64 " for device %s", id, transfer->deviceAddress.c_str());

	if (!mImpl && !getImpl<BluetoothFtpProfile>())
		return;

	// To block anybody else from deleting the transfer mark
	// it has canceled
	BT_DEBUG("Marking transfer %" PRIu64 " as canceled", id);
	transfer->canceled = true;
	transfer->clientDisappeared = clientDisappeared;

	auto cancelCallback = [this, transferIter, id, transfer](BluetoothError error) {
		BT_DEBUG("Successfully canceled bluetooth FTP transfer %" PRIu64, id);

		// Either that this time the client is invalid because he disappeared
		// (crashed, canceled call, ...) or he is still valid because the
		// transfer was canceled because of something else (FTP connection
		// dropped, ...)
		if (!transfer->clientDisappeared)
		{
			LS::Message message(transfer->watch->getMessage());
			notifyClientTransferCanceled(message, transfer->adapterAddress);
		}

		// NOTE: we don't have to care about the transfer itself here as the SIL
		// will be notified through the transfer action callback that it has failed
		// and we will bring down everything at that time. We only have to drop
		// the client watch here.
		mTransfers.erase(transferIter);
		delete transfer;
	};

	getImpl<BluetoothFtpProfile>()->cancelTransfer(id, cancelCallback);
}

void BluetoothFtpProfileService::createTransfer(BluetoothFtpTransferId id, const std::string &address, const std::string &adapterAddress, LSMessage *message)
{
	BT_DEBUG("Creating transfer %" PRIu64 " for device %s", id, address.c_str());

	Transfer *transfer = new Transfer;
	transfer->deviceAddress = address;
	transfer->adapterAddress = adapterAddress;

	auto transferClientDroppedCallback = [this, id]() {
		BT_DEBUG("Client for transfer %" PRIu64 " dropped", id);
		cancelTransfer(id, true);
	};

	transfer->watch = new LSUtils::ClientWatch(getManager()->get(), message,
	                                           transferClientDroppedCallback);

	mTransfers.insert(std::pair<BluetoothFtpTransferId, Transfer*>(id, transfer));
}

void BluetoothFtpProfileService::removeTransfer(LSMessage *message)
{
	std::string searchedToken(LSMessageGetUniqueToken(message));

	removeTransfer([searchedToken](Transfer *transfer) {
		std::string token(LSMessageGetUniqueToken(transfer->watch->getMessage()));
		return token == searchedToken;
	});
}

void BluetoothFtpProfileService::removeTransfer(const std::string &deviceAddress)
{
	removeTransfer([deviceAddress](Transfer *transfer) {
		return transfer->deviceAddress == deviceAddress;
	});
}

void BluetoothFtpProfileService::removeTransfer(std::function<bool(Transfer*)> condition)
{
	for (auto iter = mTransfers.begin(); iter != mTransfers.end(); iter++)
	{
		Transfer *transfer = iter->second;

		if (condition(transfer))
		{
			removeTransfer(iter);
			break;
		}
	}
}

void BluetoothFtpProfileService::removeTransfer(BluetoothFtpTransferId id)
{
	auto transferIter = mTransfers.find(id);
	if (transferIter == mTransfers.end())
		return;

	removeTransfer(transferIter);
}

void BluetoothFtpProfileService::removeTransfer(std::map<BluetoothFtpTransferId,Transfer*>::iterator transferIter)
{
	Transfer *transfer = transferIter->second;
	BluetoothFtpTransferId id = transferIter->first;

	// Only remove transfer when we're not in the middle of
	// canceling it.
	if (transfer->canceled)
	{
		BT_DEBUG("Not removing transfer %" PRIu64 " yet as it is canceled already", id);
		return;
	}

	BT_DEBUG("Removing transfer %" PRIu64, id);

	mTransfers.erase(transferIter);
	delete transfer;
}

std::string elementTypeToStr(BluetoothFtpElement::Type type)
{
	std::string str = "unknown";

	switch (type)
	{
	case BluetoothFtpElement::Type::FOLDER:
		str = "directory";
		break;
	case BluetoothFtpElement::Type::FILE:
		str = "file";
		break;
	default:
		break;
	}

	return str;
}

static inline bool isFieldSet(uint8_t value, uint8_t field)
{
	return (value & field) == field;
}

static pbnjson::JValue buildPermissionObject(uint8_t permission)
{
	pbnjson::JValue permissionObj = pbnjson::Object();

	permissionObj.put("read", isFieldSet(permission, BluetoothFtpElement::Permission::READ));
	permissionObj.put("write", isFieldSet(permission, BluetoothFtpElement::Permission::WRITE));
	permissionObj.put("delete", isFieldSet(permission, BluetoothFtpElement::Permission::DELETE));

	return permissionObj;
}

bool BluetoothFtpProfileService::listDirectory(LSMessage &message)
{
	BT_INFO("FTP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothFtpProfile>())
	{
			LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
			return true;
	}

	const std::string schema = STRICT_SCHEMA(PROPS_3(PROP(address, string), PROP(directoryPath, string),
								PROP(adapterAddress, string)) REQUIRED_2(address, directoryPath));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("directoryPath"))
			LSUtils::respondWithError(request, BT_ERR_DIRPATH_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress = requestObj["address"].asString();

	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
			LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
			return true;
	}

	std::string directoryPath = requestObj["directoryPath"].asString();
	if (!g_path_is_absolute(&directoryPath[0]))
	{
		LSUtils::respondWithError(request, BT_ERR_INVALID_DIRPATH);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	auto listFolderCallback = [requestMessage, deviceAddress, adapterAddress](BluetoothError error, const std::vector<BluetoothFtpElement> elements) {
			LS::Message request(requestMessage);

			if (error != BLUETOOTH_ERROR_NONE)
			{
				LSMessageUnref(request.get());
				LSUtils::respondWithError(request, BT_ERR_LIST_FOLDER_FAIL);
				return;
			}

			pbnjson::JValue contentsObj = pbnjson::Array();

			for (auto element : elements)
			{
					pbnjson::JValue elementObj = pbnjson::Object();

					elementObj.put("name", element.getName());
					elementObj.put("type", elementTypeToStr(element.getType()));

					if (element.isFieldSet(BluetoothFtpElement::Field::SIZE))
						elementObj.put("size", (int64_t) element.getSize());

					pbnjson::JValue permissionsObj = pbnjson::Object();

					if (element.isFieldSet(BluetoothFtpElement::Field::USER_PERMISSION))
						permissionsObj.put("user", buildPermissionObject(element.getUserPermission()));
					if (element.isFieldSet(BluetoothFtpElement::Field::GROUP_PERMISSION))
						permissionsObj.put("group", buildPermissionObject(element.getGroupPermission()));
					if (element.isFieldSet(BluetoothFtpElement::Field::OTHER_PERMISSION))
						permissionsObj.put("other", buildPermissionObject(element.getOtherPermission()));

					if (permissionsObj.objectSize() > 0)
						elementObj.put("permission", permissionsObj);

					if (element.isFieldSet(BluetoothFtpElement::Field::MODIFIED_TIME))
						elementObj.put("modified", (int64_t) element.getModifiedTime());
					if (element.isFieldSet(BluetoothFtpElement::Field::ACCESSED_TIME))
						elementObj.put("accessed", (int64_t) element.getAccessedTime());
					if (element.isFieldSet(BluetoothFtpElement::Field::CREATED_TIME))
						elementObj.put("created", (int64_t) element.getCreatedTime());

					contentsObj.append(elementObj);
			}

			pbnjson::JValue responseObj = pbnjson::Object();

			responseObj.put("returnValue", true);
			responseObj.put("adapterAddress", adapterAddress);
			responseObj.put("contents", contentsObj);

			LSUtils::postToClient(request, responseObj);
	};

	getImpl<BluetoothFtpProfile>()->listFolder(deviceAddress, directoryPath, listFolderCallback);

	return true;
}

BluetoothFtpProfileService::Transfer* BluetoothFtpProfileService::findTransfer(LSMessage *message)
{
	BT_INFO("FTP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	std::string messageToken(LSMessageGetUniqueToken(message));

	for (auto transferIter : mTransfers)
	{
		Transfer *transfer = transferIter.second;
		std::string transferToken(LSMessageGetUniqueToken((transfer->watch->getMessage())));

		if (messageToken == transferToken)
			return transfer;
	}

	return 0;
}

void BluetoothFtpProfileService::handleFileTransferUpdate(LSMessage *message, const std::string &adapterAddress, BluetoothError error, uint64_t bytesTransferred, bool finished)
{
	LS::Message request(message);

	if (error != BLUETOOTH_ERROR_NONE)
	{
		Transfer *transfer = findTransfer(message);
		if (transfer && !transfer->canceled)
		{
			removeTransfer(message);
			LSUtils::respondWithError(request, BT_ERR_FTP_PUSH_PULL_FAIL, true);
		}

		LSMessageUnref(request.get());

		return;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", !finished);
	responseObj.put("transferred", (int64_t) bytesTransferred);

	LSUtils::postToClient(request, responseObj);

	if (finished)
	{
		removeTransfer(message);
		LSMessageUnref(request.get());
	}
}

bool BluetoothFtpProfileService::prepareFileTransfer(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;

	if (!mImpl && !getImpl<BluetoothFtpProfile>())
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return false;
	}


	const std::string schema = STRICT_SCHEMA(PROPS_5(PROP(address, string), PROP(sourceFile, string),
                                              PROP(destinationFile, string), PROP_WITH_VAL_1(subscribe, boolean, true),
                                              PROP(adapterAddress, string)) REQUIRED_4(address, subscribe, sourceFile, destinationFile));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!request.isSubscription())
			LSUtils::respondWithError(request, BT_ERR_MTHD_NOT_SUBSCRIBED);

		else if (!requestObj.hasKey("sourceFile"))
			LSUtils::respondWithError(request, BT_ERR_SRCFILE_PARAM_MISSING);

		else if (!requestObj.hasKey("destinationFile"))
			LSUtils::respondWithError(request, BT_ERR_DESTFILE_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return false;
	}

	std::string deviceAddress = requestObj["address"].asString();

	if (!getManager()->isDeviceAvailable(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_DEVICE_NOT_AVAIL);
		return false;
	}

	if (!isDeviceConnected(deviceAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_NOT_CONNECTED);
		return false;
	}

	return true;
}

void BluetoothFtpProfileService::notifyClientTransferStarts(LS::Message &request, const std::string &adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", true);
	responseObj.put("transferred", (int64_t) 0);

	LSUtils::postToClient(request, responseObj);
}

void BluetoothFtpProfileService::notifyClientTransferCanceled(LS::Message &request, const std::string &adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", false);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", false);
	responseObj.put("transferred", (int64_t) 0);
	responseObj.put("errorText", retrieveErrorText(BT_ERR_FTP_TRANSFER_CANCELED));
	responseObj.put("errorCode", (int32_t)BT_ERR_FTP_TRANSFER_CANCELED);

	LSUtils::postToClient(request, responseObj);
}

bool BluetoothFtpProfileService::pullFile(LSMessage &message)
{
	BT_INFO("FTP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareFileTransfer(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress =  requestObj["address"].asString();
	std::string sourceFile = requestObj["sourceFile"].asString();

	// Every outgoing file is coming from /media/internal and that is
	// also the root path from the ls2 API perspective.
	std::string destinationFile = buildStorageDirPath(requestObj["destinationFile"].asString());

	if (!checkPathExists(destinationFile)) {
		std::string errorMessage = "Supplied destination path ";
		errorMessage += destinationFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_DESTPATH_INVALID);
		return true;
	}

	if (!g_path_is_absolute(&sourceFile[0]))
	{
		LSUtils::respondWithError(request, BT_ERR_INVALID_SRCFILE_PATH);
		return true;
	}

	BT_DEBUG("Pulling file %s from %s to %s",
	         sourceFile.c_str(), deviceAddress.c_str(), destinationFile.c_str());

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothFtpTransferId transferId = BLUETOOTH_FTP_TRANSFER_ID_INVALID;

	notifyClientTransferStarts(request, adapterAddress);

	transferId = getImpl<BluetoothFtpProfile>()->pullFile(deviceAddress, sourceFile, destinationFile,
						std::bind(&BluetoothFtpProfileService::handleFileTransferUpdate, this, requestMessage, adapterAddress, _1, _2, _3));

	createTransfer(transferId, deviceAddress, adapterAddress, requestMessage);

	return true;
}

std::string BluetoothFtpProfileService::buildStorageDirPath(const std::string &path)
{
	std::string result = WEBOS_MOUNTABLESTORAGEDIR;
	result += "/";
	result += path;
	return result;
}

bool BluetoothFtpProfileService::pushFile(LSMessage &message)
{
	BT_INFO("FTP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareFileTransfer(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress =  requestObj["address"].asString();
	std::string destinationFile = requestObj["destinationFile"].asString();

	// Every outgoing file is coming from /media/internal and that is
	// also the root path from the ls2 API perspective.
	std::string sourceFile = buildStorageDirPath(requestObj["sourceFile"].asString());

	if (!checkFileIsValid(sourceFile)) {
		std::string errorMessage = "Supplied file ";
		errorMessage += sourceFile;
		errorMessage += " does not exist or is invalid";
		LSUtils::respondWithError(request, errorMessage, BT_ERR_SRCFILE_INVALID);
		return true;
	}

	if (!g_path_is_absolute(&destinationFile[0]))
	{
		LSUtils::respondWithError(request, BT_ERR_INVALID_DESTFILE_PATH);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothFtpTransferId transferId = BLUETOOTH_FTP_TRANSFER_ID_INVALID;

	notifyClientTransferStarts(request, adapterAddress);

	transferId = getImpl<BluetoothFtpProfile>()->pushFile(deviceAddress, sourceFile, destinationFile,
						std::bind(&BluetoothFtpProfileService::handleFileTransferUpdate, this, requestMessage, adapterAddress, _1, _2, _3));

	createTransfer(transferId, deviceAddress, adapterAddress, requestMessage);

	return true;
}

