#ifndef BLUETOOTHMAPPROFILESERVICE_H
#define BLUETOOTHMAPPROFILESERVICE_H

#include <string>
#include <bluetooth-sil-api.h>
#include <luna-service2/lunaservice.hpp>
#include <pbnjson.hpp>
#include "bluetoothprofileservice.h"
#include "bluetootherrors.h"
#include "bluetoothdevice.h"

class BluetoothDevice;

class BluetoothMapProfileService : public BluetoothProfileService,
                                    public BluetoothMapStatusObserver
{
public:
	BluetoothMapProfileService(BluetoothManagerService *manager);
	~BluetoothMapProfileService();

	void initialize();
	void initialize(const std::string &adapterAddress);
	bool getMASInstances(LSMessage &message);
	bool connect(LSMessage &message);
	bool disconnect(LSMessage &message);
	bool getStatus(LSMessage &message);
	bool getMessageFilters(LSMessage &message);
	bool getMessageList(LSMessage &message);
	bool getFolderList(LSMessage &message);
	bool setFolder(LSMessage &message);
	bool getMessage(LSMessage &message);
	bool setMessageStatus(LSMessage &message);
	bool pushMessage(LSMessage &message);
	bool getMessageNotification(LSMessage &message);
	void handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey);
	void propertiesChanged(const std::string &adapterAddress, const std::string &sessionKey, BluetoothPropertiesList properties);
private:
	void handleDeviceClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey);
	void notifyGetMasInstaces(pbnjson::JValue responseObj, const std::string &adapterAddress, const std::string &deviceAddress);
	pbnjson::JValue buildMapGetStatusResp(const std::string &adapterAddress, const std::string &deviceAddress, const std::string &instanceName);
	pbnjson::JValue appendMasInstanceStatus(const std::string &adapterAddress, const std::string &deviceAddress, const std::string &masInstance);
	pbnjson::JValue appendMasInstances(const std::string &adapterAddress, const std::string &deviceAddress);
	pbnjson::JValue appendMasInstanceSupportedtypes(std::vector<std::string> supportedtypes);
	pbnjson::JValue createJsonFilterList(const std::list<std::string> &filters);
	bool prepareGetMasInstances(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool prepareConnect(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool prepareGetStatus(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	void markDeviceAsConnectedWithSessionKey(const std::string &adapterAddress, const std::string &sessionId, const std::string &sessionKey);
	void removeDeviceAsConnectedWithSessionKey(const std::string &adapterAddress, const std::string &sessionKey);
	bool isSessionIdSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool isInstanceNameValid(const std::string &instance, const std::string &adapterAddress, const std::string &deviceAddress);
	bool isSessionIdValid(const std::string &adapterAddress, const std::string &deviceAddress,const std::string &sessionId, std::string &sessionKey);
	bool requiredCheckForMapProfile(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	std::string parseInstanceNameFromSessionKey(const std::string &sessionKey);
	std::string parseAddressFromSessionKey(const std::string &sessionKey);
	inline std::string generateSessionKey(const std::string &deviceAddress, const std::string &instanceName);
	void notifyGetStatusSubscribers(const std::string &adapterAddress, const std::string &sessionKey);
	std::string getSessionId(const std::string &adapterAddress, const std::string &sessionKey);
	void removeConnectWatchForDevice(const std::string &address, const std::string &adapterAddress, const std::string &key, const std::string &sessionId, bool disconnected, bool remoteDisconnect);
	bool isGetFolderListSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	void getFolderCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error,std::vector<std::string>& folderList);
	bool isSetFolderSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	void setFolderCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error);
	bool isGetMessageListSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	void addGetMessageFilters(const pbnjson::JValue &requestObj, BluetoothMapPropertiesList &filters);
	void getMessageListCallback(LS::Message &request,const std::string& address, const std::string& sessionKey,const std::string& adapterAddress,BluetoothError error, BluetoothMessageList& messageList);
	void appendMessageList(pbnjson::JValue &responseObject , BluetoothMessageList& messageList);
	bool isGetMessageSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	std::string buildStorageDirPath(const std::string &path, const std::string &address);
	bool isSetMessageStatusSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool isPushMessageSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool isGetMessageNotificationSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	void notifySubscribersAboutPropertiesChange(const std::string &adapterAddress, const std::string &sessionId, BluetoothMessageList &messageList);
	void appendCurrentProperties(pbnjson::JValue &object);
	std::string findSessionKey(const std::string &adapterAddress, const std::string &sessionId);
	void messageNotificationEvent(const std::string &adapterAddress, const std::string &sessionId, BluetoothMessageList &properties);
	void appendNotificationEvent(pbnjson::JValue &responseObject , BluetoothMessageList& messageList);
	void handleMessageNotificationClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey);
	std::map<std::string, std::map<std::string, std::string>> mConnectedDevicesForMultipleAdaptersWithSessionKey;
	std::map<std::string, std::map<std::string, LS::SubscriptionPoint*>> mMapGetStatusSubscriptionsForMultipleAdapters;
	std::map<std::string, std::map<std::string, LSUtils::ClientWatch*>> mConnectWatchesForMultipleAdaptersWithSessionKey;
	std::map<std::string, std::map<std::string, LS::SubscriptionPoint*>> mNotificationPropertiesSubscriptionsForMultipleAdapters;
};
#endif // BLUETOOTHMAPPROFILESERVICE_H
