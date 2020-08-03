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


#include "bluetoothoppprofileservice.h"
#include "bluetoothmanagerservice.h"
#include "bluetootherrors.h"
#include "ls2utils.h"
#include "clientwatch.h"
#include "logging.h"
#include "utils.h"
#include "config.h"
#include "bluetoothclientwatch.h"

#define BLUETOOTH_PROFILE_OPP_MAX_REQUEST_ID 999

using namespace std::placeholders;

BluetoothOppProfileService::Transfer::~Transfer()
{
	if (watch)
	delete watch;
}


BluetoothOppProfileService::BluetoothOppProfileService(BluetoothManagerService *manager) :
        BluetoothProfileService(manager, "OPP", "00001105-0000-1000-8000-00805f9b34fb"),
        mRequestIndex(0),
        mNextRequestId(1)
{
	LS_CREATE_CATEGORY_BEGIN(BluetoothProfileService, base)
		LS_CATEGORY_METHOD(connect)
		LS_CATEGORY_METHOD(disconnect)
		LS_CATEGORY_METHOD(getStatus)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, pushFile)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, awaitTransferRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, acceptTransferRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, rejectTransferRequest)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, cancelTransfer)
		LS_CATEGORY_CLASS_METHOD(BluetoothOppProfileService, monitorTransfer)
	LS_CREATE_CATEGORY_END

	manager->registerCategory("/opp", LS_CATEGORY_TABLE_NAME(base), NULL, NULL);
	manager->setCategoryData("/opp", this);

}

BluetoothOppProfileService::~BluetoothOppProfileService()
{
}

void BluetoothOppProfileService::initialize()
{
	if (mImpl)
		return;

	BluetoothProfileService::initialize();

	if (mImpl)
		getImpl<BluetoothOppProfile>()->registerObserver(this);
}

void BluetoothOppProfileService::initialize(const std::string &adapterAddress)
{
	BluetoothProfileService::initialize(adapterAddress);

	if (findImpl(adapterAddress))
		getImpl<BluetoothOppProfile>(adapterAddress)->registerObserver(this);
}

bool BluetoothOppProfileService::isDevicePaired(const std::string &address)
{
	// NOTE: OPP connect method does not need to check the paired status of a device.
	// So, we make the overriden isDevicePaired func, return value only true.
	return true;
}

void BluetoothOppProfileService::cancelTransfer(BluetoothOppTransferId id, bool clientDisappeared)
{
	auto transferIter = mTransfers.find(id);
	if (transferIter == mTransfers.end())
		return;

	Transfer *transfer = transferIter->second;

	BT_DEBUG("Cancel OPP transfer %llu for device %s", id, transfer->deviceAddress.c_str());

	// To block anybody else from deleting the transfer mark
	// it has canceled
	BT_DEBUG("Marking transfer %llu as canceled", id);
	transfer->canceled = true;
	transfer->clientDisappeared = clientDisappeared;

	auto cancelCallback = [this, transferIter, id, transfer](BluetoothError error) {
		BT_DEBUG("Successfully canceled bluetooth OPP transfer %llu", id);

		// Either that this time the client is invalid because he disappeared
		// (crashed, canceled call, ...) or he is still valid because the
		// transfer was canceled because of something else (OPP connection
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

	getImpl<BluetoothOppProfile>()->cancelTransfer(id, cancelCallback);
}

void BluetoothOppProfileService::createTransfer(BluetoothOppTransferId id, const std::string &address, const std::string &adapterAddress, LSMessage *message)
{
	BT_DEBUG("Creating transfer %llu for device %s", id, address.c_str());

	Transfer *transfer = new Transfer;
	transfer->deviceAddress = address;
	transfer->adapterAddress = adapterAddress;

	auto transferClientDroppedCallback = [this, id]() {
		BT_DEBUG("Client for transfer %llu dropped", id);
		cancelTransfer(id, true);
	};

	transfer->watch = new LSUtils::ClientWatch(getManager()->get(), message,
                                                   transferClientDroppedCallback);

	mTransfers.insert(std::pair<BluetoothOppTransferId, Transfer*>(id, transfer));
}

void BluetoothOppProfileService::removeTransfer(LSMessage *message)
{
	std::string searchedToken(LSMessageGetUniqueToken(message));

	removeTransfer([searchedToken](Transfer *transfer) {
		std::string token(LSMessageGetUniqueToken(transfer->watch->getMessage()));
		return token == searchedToken;
	});
}

void BluetoothOppProfileService::removeTransfer(const std::string &deviceAddress)
{
	removeTransfer([deviceAddress](Transfer *transfer) {
		return transfer->deviceAddress == deviceAddress;
	});
}

void BluetoothOppProfileService::removeTransfer(std::function<bool(Transfer*)> condition)
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

void BluetoothOppProfileService::removeTransfer(BluetoothOppTransferId id)
{
	auto transferIter = mTransfers.find(id);
	if (transferIter == mTransfers.end())
		return;

	removeTransfer(transferIter);
}

void BluetoothOppProfileService::removeTransfer(std::map<BluetoothOppTransferId,Transfer*>::iterator transferIter)
{
	Transfer *transfer = transferIter->second;
	BluetoothOppTransferId id = transferIter->first;

	// Only remove transfer when we're not in the middle of
	// canceling it.
	if (transfer->canceled)
	{
		BT_DEBUG("Not removing transfer %llu yet as it is canceled already", id);
		return;
	}

	BT_DEBUG("Removing transfer %llu", id);

	mTransfers.erase(transferIter);
	delete transfer;
}

BluetoothOppProfileService::Transfer* BluetoothOppProfileService::findTransfer(LSMessage *message)
{
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

void BluetoothOppProfileService::handleFileTransferUpdate(LSMessage *message, const std::string &adapterAddress, BluetoothError error, uint64_t bytesTransferred, uint64_t totalSize, bool finished)
{
	LS::Message request(message);

	if (error != BLUETOOTH_ERROR_NONE)
	{
		Transfer *transfer = findTransfer(message);
		if (transfer && !transfer->canceled)
		{
			removeTransfer(message);
			LSUtils::respondWithError(request, BT_ERR_OPP_PUSH_PULL_FAIL, true);
		}

		LSMessageUnref(request.get());

		return;
	}

	if (request.isSubscription())
	{
		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("returnValue", true);
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("subscribed", !finished);
		responseObj.put("transferred", (int64_t) bytesTransferred);
		responseObj.put("size", (int64_t) totalSize);
		LSUtils::postToClient(request, responseObj);
	}

	if (finished)
	{
		removeTransfer(message);
		LSMessageUnref(request.get());
	}
}

bool BluetoothOppProfileService::prepareFileTransfer(LS::Message &request, pbnjson::JValue &requestObj)
{
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_4(PROP(address, string), PROP(sourceFile, string),
                                              PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string))
                                              REQUIRED_2(address, sourceFile));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("address"))
			LSUtils::respondWithError(request, BT_ERR_ADDR_PARAM_MISSING);

		else if (!requestObj.hasKey("sourceFile"))
			LSUtils::respondWithError(request, BT_ERR_SRCFILE_PARAM_MISSING);

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
		LSUtils::respondWithError(request, BT_ERR_OPP_NOT_CONNECTED);
		return false;
	}

        return true;
}

std::string BluetoothOppProfileService::buildStorageDirPath(const std::string &path)
{
	std::string result = WEBOS_MOUNTABLESTORAGEDIR;
	result += "/";
	result += path;
	return result;
}

void BluetoothOppProfileService::notifyClientTransferStarts(LS::Message &request, const std::string &adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", true);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", request.isSubscription());

	LSUtils::postToClient(request, responseObj);
}

void BluetoothOppProfileService::notifyClientTransferCanceled(LS::Message &request, const std::string &adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", false);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", false);
	responseObj.put("transferred", (int64_t) 0);
	responseObj.put("errorText", retrieveErrorText(BT_ERR_OPP_TRANSFER_CANCELED));
	responseObj.put("errorCode", (int32_t) BT_ERR_OPP_TRANSFER_CANCELED);

	LSUtils::postToClient(request, responseObj);
}

void BluetoothOppProfileService::notifyTransferStatus(const std::string &adapterAddress)
{
	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("returnValue", true);
	responseObj.put("subscribed", true);

	appendTransferStatus(responseObj);

	LS::SubscriptionPoint* subscriptionPoint;

	auto itr = mMonitorTransferSubscriptions.find(adapterAddress);
	if(itr != mMonitorTransferSubscriptions.end())
	{
		subscriptionPoint = itr->second;
		responseObj.put("adapterAddress", adapterAddress);
		responseObj.put("subscribed", true);
		responseObj.put("returnValue", true);

		LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);
	}
}

void BluetoothOppProfileService::appendTransferStatus(pbnjson::JValue &object)
{
	pbnjson::JValue transfersObj = pbnjson::Array();

	for (auto monitorIter : mTransferIds)
	{
		uint64_t requestListIndex = (int64_t) monitorIter.first;
		auto iterRequest = mPushRequests.find(requestListIndex);
		pbnjson::JValue responseObj = pbnjson::Object();
		if (iterRequest != mPushRequests.end())
		{
			PushRequest *requestVal = iterRequest->second;

			responseObj.put("adapterAddress", requestVal->adapterAddress);
			responseObj.put("requestId", requestVal->requestId);
			responseObj.put("address", requestVal->address);
			responseObj.put("name", requestVal->name);
			responseObj.put("fileName", requestVal->fileName);
			responseObj.put("fileSize", (int64_t) requestVal->fileSize);
			responseObj.put("transferred", (int64_t) requestVal->transferred);
		}

		transfersObj.append(responseObj);
	}

	object.put("transfers", transfersObj);
}

bool BluetoothOppProfileService::pushFile(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	if (!prepareFileTransfer(request, requestObj))
		return true;

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	std::string deviceAddress =  requestObj["address"].asString();

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

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothOppTransferId transferId = BLUETOOTH_OPP_TRANSFER_ID_INVALID;
	notifyClientTransferStarts(request, adapterAddress);

	transferId = getImpl<BluetoothOppProfile>(adapterAddress)->pushFile(deviceAddress, sourceFile,
		std::bind(&BluetoothOppProfileService::handleFileTransferUpdate, this, requestMessage, adapterAddress, _1, _2, _3, _4));

	createTransfer(transferId, deviceAddress, adapterAddress, requestMessage);

	return true;
}

bool BluetoothOppProfileService::awaitTransferRequest(LSMessage &message)
{
	BT_INFO("OPP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string)) REQUIRED_1(subscribe));

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

	if (!getManager()->getPowered(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_TURNED_OFF);
		return true;
	}

	for (auto incomingTransferWatch: mIncomingTransferWatchesForMultipleAdapters)
	{
		if(adapterAddress == incomingTransferWatch->getAdapterAddress())
		{
			LSUtils::respondWithError(request, BT_ERR_ALLOW_ONE_SUBSCRIBE);
			return true;
		}
	}

	if (request.isSubscription())
	{
		bool retVal = addClientWatch(request, &mIncomingTransferWatchesForMultipleAdapters,
			adapterAddress, "");
		if (!retVal)
		{
			LSUtils::respondWithError(request, BT_ERR_MESSAGE_OWNER_MISSING);
			return true;
		}
	}
	setTransferRequestsAllowed(adapterAddress, true);

	pbnjson::JValue responseObj = pbnjson::Object();

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);

	LSUtils::postToClient(request, responseObj);

	return true;
}

bool BluetoothOppProfileService::addClientWatch(LS::Message& request, std::list<BluetoothClientWatch*>* clientWatch,
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
		std::bind(&BluetoothOppProfileService::handleClientDisappeared,
			this, clientWatch, senderName), adapterAddress, deviceAddress);
	clientWatch->push_back(watch);
	return true;
}

void BluetoothOppProfileService::handleClientDisappeared(std::list<BluetoothClientWatch*>* clientWatch, const std::string senderName)
{
	removeClientWatch(clientWatch, senderName);
}

void BluetoothOppProfileService::removeClientWatch(std::list<BluetoothClientWatch*> *clientWatch, const std::string& senderName)
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
			setTransferRequestsAllowed((*watch)->getAdapterAddress(),false);
			delete (*watchToRemove);
			watch = clientWatch->erase(watch);
		}
		else
		{
			++watch;
		}
	}
}

bool BluetoothOppProfileService::monitorTransfer(LSMessage &message)
{
	BT_INFO("OPP", 0, "Luna API is called : [%s : %d]", __FUNCTION__, __LINE__);

	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP_WITH_VAL_1(subscribe, boolean, true), PROP(adapterAddress, string)) REQUIRED_1(subscribe));
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

	if (!getManager()->getPowered(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_ADAPTER_TURNED_OFF);
		return true;
	}

	pbnjson::JValue responseObj = pbnjson::Object();

	LS::SubscriptionPoint* subscriptionPoint;
	auto itr = mMonitorTransferSubscriptions.find(adapterAddress);
	if(itr == mMonitorTransferSubscriptions.end())
	{
		subscriptionPoint = new LS::SubscriptionPoint;
		mMonitorTransferSubscriptions.insert({adapterAddress,subscriptionPoint});
		subscriptionPoint->setServiceHandle(getManager());
		subscriptionPoint->subscribe(request);
	} else {
		subscriptionPoint = itr->second;
		subscriptionPoint->subscribe(request);
	}

	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", true);
	responseObj.put("returnValue", true);

	LSUtils::postToSubscriptionPoint(subscriptionPoint, responseObj);

	return true;
}

void BluetoothOppProfileService::setTransferRequestsAllowed(const std::string &adapterAddress, bool state)
{
	BT_DEBUG("Setting transferable to adapterAddress %s state %d",adapterAddress.c_str(), state);
	if (mTransferRequestsAllowed.find(adapterAddress) != mTransferRequestsAllowed.end())
		mTransferRequestsAllowed[adapterAddress] = state;
	else
		mTransferRequestsAllowed.insert(std::pair<std::string, int>(adapterAddress, state));

}

std::string BluetoothOppProfileService::generateRequestId()
{
	//Make a new requestID convert to string
	std::string mNextRequestIdStr = std::to_string(mNextRequestId);
	auto padStr = [](std::string &str, const size_t num, const char paddingChar) {
		if (num > str.size())
			str.insert(0, num - str.size(), paddingChar);
	};
	padStr(mNextRequestIdStr, 3, '0');
	mNextRequestId++;

	return mNextRequestIdStr;
}

void BluetoothOppProfileService::deletePushRequest(const std::string &requestId)
{
	// pass the pushRequest in mPushRequests to mDeletedPushRequested
	for (auto propIter = mPushRequests.begin(); propIter != mPushRequests.end(); propIter++)
	{
		PushRequest *pushRequest = propIter->second;
		if (pushRequest->requestId == requestId)
		{
			/* No need to insert in mDeletedPushREquest
			Always generate new request every time
			*/
			//mDeletedPushRequested.insert(std::pair<std::string, PushRequest*>(pushRequest->requestId, pushRequest));
			mPushRequests.erase(propIter);
			break;
		}
	}
}

void BluetoothOppProfileService::assignPushRequestId(PushRequest *pushRequest)
{
	if (mDeletedPushRequested.size())
	{
		//used deleteRequestId
		auto temp = mDeletedPushRequested.cbegin();
		pushRequest->requestId = temp->first;
		mDeletedPushRequested.erase(temp);
	}
	else
	{
		pushRequest->requestId = generateRequestId();
	}
}

void BluetoothOppProfileService::assignPushRequestFromUnused(PushRequest *pushRequest)
{
	//The mNextRequestId for 999 numbers of requests are maintained in a system.
	//If an user does not delete the mNextRequestIds of some requests manually,
	//they will be deleted oldest first.
	//So, this method found an oldest mRequestIndex when it over the threshold of
	//BLUETOOTH_PROFILE_OPP_MAX_REQUEST_ID and changed other values, but maintained
	//the mNextRequestId.
	auto eraseIter = mPushRequests.cbegin();
	uint64_t oldIndex = eraseIter->first;
	PushRequest *pushTemp = eraseIter->second;
	std::string oldRequestId = pushTemp->requestId;

	std::map<uint64_t, PushRequest*>::iterator prMap;
	for (prMap = mPushRequests.begin(); prMap != mPushRequests.end(); prMap++)
	{
		if (oldIndex > prMap->first)
		{
			eraseIter = prMap;
			oldIndex = prMap->first;
			pushTemp = prMap->second;
			oldRequestId = pushTemp->requestId;
		}
	}

	pushRequest->requestId = oldRequestId;
	mPushRequests.erase(eraseIter);
}

void BluetoothOppProfileService::createPushRequest(BluetoothOppTransferId transferId, const std::string &adapterAddress, const std::string &address, const std::string &deviceName, const std::string &fileName, uint64_t fileSize)
{
	PushRequest *pushRequest = new PushRequest();
	pushRequest->adapterAddress = adapterAddress;
	pushRequest->address = address;
	pushRequest->name = deviceName;
	pushRequest->fileName = fileName;
	pushRequest->fileSize = fileSize;

	if (mNextRequestId > BLUETOOTH_PROFILE_OPP_MAX_REQUEST_ID)
		assignPushRequestFromUnused(pushRequest);
	else
		assignPushRequestId(pushRequest);

	mPushRequests.insert(std::pair<uint64_t, PushRequest*>(mRequestIndex, pushRequest));
	mTransferIds.insert(std::pair<uint64_t, BluetoothOppTransferId>(mRequestIndex, transferId));
	notifyTransferConfirmation(mRequestIndex);
	mRequestIndex++;
}

void BluetoothOppProfileService::notifyTransferConfirmation(uint64_t requestIndex)
{
	BT_INFO("OPP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue object = pbnjson::Object();

	auto requestIter = mPushRequests.find(requestIndex);
	if (requestIter != mPushRequests.end())
	{
		PushRequest * pushRequest = requestIter->second;

		pbnjson::JValue responseObj = pbnjson::Object();
		responseObj.put("adapterAddress", pushRequest->adapterAddress);
		responseObj.put("requestId", pushRequest->requestId);
		responseObj.put("address", pushRequest->address);
		responseObj.put("name", pushRequest->name);
		responseObj.put("fileName", pushRequest->fileName);
		responseObj.put("fileSize", (int64_t) pushRequest->fileSize);
		object.put("request", responseObj);

		for (auto incomingTransferWatch: mIncomingTransferWatchesForMultipleAdapters)
		{

			if(pushRequest->adapterAddress == incomingTransferWatch->getAdapterAddress())
			{
				LSUtils::postToClient(incomingTransferWatch->getMessage(), object);
				return;
			}
		}
	}
}

void BluetoothOppProfileService::transferConfirmationRequested(BluetoothOppTransferId transferId, const std::string &adapterAddress, const std::string &address, const std::string &deviceName, const std::string &fileName, uint64_t fileSize)
{
	BT_DEBUG("Received transfer request from adapter %s device %s and file %s with size %llu", adapterAddress.c_str(), address.c_str(), deviceName.c_str(), fileSize);

	if (mTransferRequestsAllowed.find(adapterAddress) == mTransferRequestsAllowed.end())
	{
		BT_DEBUG("Not allowed to accept incoming transfer request");
		return;
	}

	if (!mTransferRequestsAllowed[adapterAddress])
	{
		BT_DEBUG("Not allowed to accept incoming transfer request");
		return;
	}

	createPushRequest(transferId, adapterAddress, address, deviceName, fileName, fileSize);
}

void BluetoothOppProfileService::notifyConfirmationRequest(LS::Message &request, const std::string &adapterAddress, bool success)
{
	BT_INFO("OPP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue responseObj = pbnjson::Object();

	if (!success)
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_STATE_ERR);
	}

	responseObj.put("returnValue", success);
	responseObj.put("adapterAddress", adapterAddress);
	responseObj.put("subscribed", false);

	LSUtils::postToClient(request, responseObj);
	LSMessageUnref(request.get());
}

uint64_t BluetoothOppProfileService::getPushRequestId(const std::string &requestIdStr)
{
	uint64_t pushRequestId = 0;

	for (auto propIter : mPushRequests)
	{
		PushRequest *pushRequest = propIter.second;
		if (pushRequest->requestId == requestIdStr)
		{
			pushRequestId = (int64_t) propIter.first;
			break;
		}
	}

	return pushRequestId;
}

void BluetoothOppProfileService::deleteTransferId(uint64_t requestId)
{
	auto idIter = mTransferIds.find(requestId);
	if (idIter == mTransferIds.end())
		return;

	mTransferIds.erase(idIter);
}

void BluetoothOppProfileService::deleteTransferId(const std::string &requestIdStr)
{
	uint64_t requestId = getPushRequestId(requestIdStr);

	auto idIter = mTransferIds.find(requestId);
	if (idIter == mTransferIds.end())
		return;

	mTransferIds.erase(idIter);
}

BluetoothOppTransferId BluetoothOppProfileService::findTransferId(const std::string &requestIdStr)
{
	BluetoothOppTransferId transferId = BLUETOOTH_OPP_TRANSFER_ID_INVALID;
	uint64_t requestId = getPushRequestId(requestIdStr);

	auto idIter = mTransferIds.find(requestId);
	if (idIter != mTransferIds.end())
	{
		transferId = idIter->second;
	}

	return transferId;
}

BluetoothOppProfileService::PushRequest* BluetoothOppProfileService::findRequest(const std::string &requestIdStr)
{
	for (auto propIter : mPushRequests)
	{
		PushRequest *pushRequest = propIter.second;
		if (pushRequest->requestId == requestIdStr)
			return pushRequest;
	}

	return 0;
}

void BluetoothOppProfileService::transferStateChanged(BluetoothOppTransferId transferId, uint64_t transferred, bool finished)
{
	BT_INFO("OPP", 0, "Observer is called : [%s : %d]", __FUNCTION__, __LINE__);
	// This method is called by stack when it receives a file from remote device to
	// let the service know the transfer status.
	if (mTransferIds.size())
	{
		uint64_t requestListIndex = 0;

		for (auto transferIter : mTransferIds)
		{
			if (transferId == transferIter.second)
			{
				requestListIndex = (int64_t) transferIter.first;
				break;
			}
		}

		auto updateRequest = mPushRequests.find(requestListIndex);
		if (updateRequest == mPushRequests.end())
			return;

		PushRequest *pushRequest = updateRequest->second;

		if (!pushRequest)
		{
			return;
		}

		if (finished)
		{
			if (pushRequest->transferred == pushRequest->fileSize)
			{
				deleteTransferId(requestListIndex);
				deletePushRequest(pushRequest->requestId);
				return;
			}
			else
			{
				notifyTransferStatus(pushRequest->adapterAddress);
				deleteTransferId(requestListIndex);
				deletePushRequest(pushRequest->requestId);
				return;
			}
		}

		pushRequest->transferred += transferred;

		notifyTransferStatus(pushRequest->adapterAddress);

		if (pushRequest->transferred == pushRequest->fileSize)
		{
			deleteTransferId(requestListIndex);
			deletePushRequest(pushRequest->requestId);
		}
	}
}

bool BluetoothOppProfileService::prepareConfirmationRequest(LS::Message &request, pbnjson::JValue &requestObj, bool accept)
{
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(requestId, string), PROP(adapterAddress, string)) REQUIRED_1(requestId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (parseError != JSON_PARSE_SCHEMA_ERROR)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("requestId"))
			LSUtils::respondWithError(request, BT_ERR_OPP_REQUESTID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothOppProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	if (mTransferRequestsAllowed.find(adapterAddress) == mTransferRequestsAllowed.end())
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFER_NOT_ALLOWED);
		return true;
	}

	if (!mTransferRequestsAllowed[adapterAddress])
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFER_NOT_ALLOWED);
		return true;
	}

	std::string requestIdStr = requestObj["requestId"].asString();

	PushRequest *pushRequest = findRequest(requestIdStr);
	if (!pushRequest)
	{
                LSUtils::respondWithError(request, BT_ERR_OPP_REQUESTID_NOT_EXIST);
		return true;
	}

	// if the transferred and total fileSize is the same, cannot allowed accept transfer.
	if (accept && pushRequest->transferred == pushRequest->fileSize)
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_ALREADY_ACCEPT_FILE);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothOppTransferId transferId = findTransferId(requestIdStr);
	if (BLUETOOTH_OPP_TRANSFER_ID_INVALID == transferId)
	{
                LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFERID_NOT_EXIST);
		return true;
	}

	auto transferCallback = [this, adapterAddress, requestMessage](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
			notifyConfirmationRequest(request, adapterAddress, false);
		else
			notifyConfirmationRequest(request, adapterAddress, true);
	};
	getImpl<BluetoothOppProfile>(adapterAddress)->supplyTransferConfirmation(transferId, accept, transferCallback);

	if (!accept)
		deleteTransferId(requestIdStr);

	return true;
}

bool BluetoothOppProfileService::acceptTransferRequest(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	return prepareConfirmationRequest(request, requestObj, true);
}

bool BluetoothOppProfileService::rejectTransferRequest(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;

	return prepareConfirmationRequest(request, requestObj, false);
}

bool BluetoothOppProfileService::cancelTransfer(LSMessage &message)
{
	LS::Message request(&message);
	pbnjson::JValue requestObj;
	int parseError = 0;

	const std::string schema = STRICT_SCHEMA(PROPS_2(PROP(requestId, string), PROP(adapterAddress, string)) REQUIRED_1(requestId));

	if (!LSUtils::parsePayload(request.getPayload(), requestObj, schema, &parseError))
	{
		if (JSON_PARSE_SCHEMA_ERROR != parseError)
			LSUtils::respondWithError(request, BT_ERR_BAD_JSON);

		else if (!requestObj.hasKey("requestId"))
			LSUtils::respondWithError(request, BT_ERR_OPP_REQUESTID_PARAM_MISSING);

		else
			LSUtils::respondWithError(request, BT_ERR_SCHEMA_VALIDATION_FAIL);

		return true;
	}

	std::string adapterAddress;
	if (!getManager()->isRequestedAdapterAvailable(request, requestObj, adapterAddress))
		return true;

	if (!getImpl<BluetoothOppProfile>(adapterAddress))
	{
		LSUtils::respondWithError(request, BT_ERR_PROFILE_UNAVAIL);
		return true;
	}

	std::string requestIdStr = requestObj["requestId"].asString();

	if (mTransferRequestsAllowed.find(adapterAddress) == mTransferRequestsAllowed.end())
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFER_NOT_ALLOWED);
		return true;
	}

	if (!mTransferRequestsAllowed[adapterAddress])
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFER_NOT_ALLOWED);
		return true;
	}

	PushRequest *pushRequest = findRequest(requestIdStr);
	if (!pushRequest)
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_REQUESTID_NOT_EXIST);
		return true;
	}

	LSMessage *requestMessage = request.get();
	LSMessageRef(requestMessage);

	BluetoothOppTransferId transferId = findTransferId(requestIdStr);
	if (BLUETOOTH_OPP_TRANSFER_ID_INVALID == transferId)
	{
		LSUtils::respondWithError(request, BT_ERR_OPP_TRANSFERID_NOT_EXIST);
		return true;
	}

	auto cancelTransferCallback = [this, pushRequest, requestMessage, adapterAddress](BluetoothError error) {
		LS::Message request(requestMessage);

		if (BLUETOOTH_ERROR_NONE != error)
		{
			deleteTransferId(pushRequest->requestId);
			deletePushRequest(pushRequest->requestId);
			notifyConfirmationRequest(request, adapterAddress, false);
		}
		else
		{
			deleteTransferId(pushRequest->requestId);
			deletePushRequest(pushRequest->requestId);

			notifyConfirmationRequest(request, adapterAddress, true);
		}
	};
	getImpl<BluetoothOppProfile>(adapterAddress)->cancelTransfer(transferId, cancelTransferCallback);

	return true;
}
