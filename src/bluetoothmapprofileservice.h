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
	void handleConnectClientDisappeared(const std::string &adapterAddress, const std::string &sessionKey);
private:
	void notifyGetMasInstaces(pbnjson::JValue responseObj, const std::string &adapterAddress, const std::string &deviceAddress);
	pbnjson::JValue appendMasInstances(const std::string &adapterAddress, const std::string &deviceAddress);
	pbnjson::JValue appendMasInstanceSupportedtypes(std::vector<std::string> supportedtypes);
	bool prepareGetMasInstances(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	bool prepareConnect(LS::Message &request, pbnjson::JValue &requestObj, std::string &adapterAddress);
	void markDeviceAsConnectedWithSessionId(const std::string &sessionId, const std::string &sessionKey);
	void removeDeviceAsConnectedWithSessionId(const std::string &sessionId);
	bool isDisconnectSchemaAvailable(LS::Message &request, pbnjson::JValue &requestObj);
	bool isInstanceNameValid(const std::string &instance, const std::string &adapterAddress, const std::string &deviceAddress);
	bool isSessionIdValid(const std::string &sessionId);
	std::string parseInstanceNameFromSessionKey(const std::string &sessionKey);
	std::string parseAddressFromSessionKey(const std::string &sessionKey);
	inline std::string generateSessionKey(const std::string &deviceAddress, const std::string &instanceName);
	std::string getSessionId(const std::string &sessionKey);
	void removeConnectWatchForDevice(const std::string &address, const std::string &adapterAddress, const std::string &key, const std::string &sessionId, bool disconnected, bool remoteDisconnect);
	std::map<std::string, std::string> mConnectedDevicesWithSessionId;
	std::map<std::string, std::map<std::string, LSUtils::ClientWatch*>> mConnectWatchesForMultipleAdaptersWithSessionKey;

};
#endif // BLUETOOTHMAPPROFILESERVICE_H