#include <pbnjson.hpp>
#include <luna-service2/lunaservice.hpp>
#include "logging.h"
#include "ls2utils.h"

void LSUtils::postToClient(LS::Message &message, pbnjson::JValue &object)
{
	std::string payload;
	LSUtils::generatePayload(object, payload);

	try
	{
		message.respond(payload.c_str());
	}
	catch (LS::Error &error)
	{
		BT_ERROR(MSGID_LS2_FAILED_TO_SEND, 0, "Failed to submit response: %s", error.what());
	}
}

#ifdef MULTI_SESSION_SUPPORT

#include <map>
#include <string>

std::map<std::string, std::string> sessionInfoMap;

LSUtils::DisplaySetId LSUtils::getDisplaySetIdIndex(LSMessage &message, LS::Handle *handle)
{
	if (LSMessageGetSessionId(&message) != NULL)
	{
		std::string sessionId = LSMessageGetSessionId(&message);
		BT_INFO("INFO_SESSION", 0, "session id is %s", sessionId.c_str());

		if (sessionId == "host")
			return HOST;

		pbnjson::JValue payload = pbnjson::Object();
		payload.put("sessionId", sessionId.c_str());
		pbnjson::JGenerator serializer(nullptr);
		std::string payloadstr;
		serializer.toString(payload, pbnjson::JSchema::AllSchema(), payloadstr);

		auto it = sessionInfoMap.find(sessionId);
		std::string deviceSetId;
		if( it == sessionInfoMap.end())
		{
			auto reply = handle->callOneReply("luna://com.webos.service.account/getSession", payloadstr.c_str()).get();
			pbnjson::JValue replyObj = pbnjson::Object();
			LSUtils::parsePayload(reply.getPayload(), replyObj);

			bool returnValue = replyObj["returnValue"].asBool();
			if (!returnValue) {
				return HOST;
			}

			auto sessionInfo = replyObj["session"];
			auto deviceInfo = sessionInfo["deviceSetInfo"];
			deviceSetId = deviceInfo["deviceSetId"].asString();

			sessionInfoMap[sessionId] = deviceSetId;

		}
		else
		{
			deviceSetId = it->second;
		}

		BT_INFO("INFO_SESSION", 0, "deviceSetId %s", deviceSetId.c_str());

		return getDisplaySetIdIndex(deviceSetId);
	}

	BT_INFO("INFO_SESSION", 0, "session is null");

	//failure
	return HOST;
}

LSUtils::DisplaySetId LSUtils::getDisplaySetIdIndex(const std::string &deviceSetId)
{
	if ("RSE-L" == deviceSetId)
	{
		return RSE_L;
	} else if ("RSE-R" == deviceSetId)
	{
		return RSE_R;
	}
	else if ("AVN" == deviceSetId)
	{
		return AVN;
	}

	return HOST;
}
#endif

bool LSUtils::callDb8MeshFindToken(LS::Handle *serviceHandle, std::string &token)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);
	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/find",
											"{\"query\":{ \"from\":\"com.webos.service.bluetooth2.meshtoken:1\"}}").get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		BT_INFO("MESH", 0, "Db8 find API returned error: %d==%s : [%s : %d]",
			replyObj["errorCode"].asNumber<int32_t>(), replyObj["errorText"].asString().c_str(),
			__FUNCTION__, __LINE__);
		return false;
	}

	BT_DEBUG("replyObj: %s", replyObj.stringify().c_str());
	pbnjson::JValue results = replyObj["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			if (results[i].hasKey("meshToken"))
			{
				pbnjson::JValue meshEntry = results[i];
				token = meshEntry["meshToken"].asString();
				break;
			}
		}

		BT_INFO("MESH", 0, "token received from db: %s", token.c_str());
		return true;
	}
	return false;
}

bool LSUtils::callDb8MeshSetToken(LS::Handle *serviceHandle, std::string &token)
{
	pbnjson::JValue objArray = pbnjson::Array();
	pbnjson::JValue tokenObj = pbnjson::Object();
	pbnjson::JValue reqObj = pbnjson::Object();

	tokenObj.put("_kind", "com.webos.service.bluetooth2.meshtoken:1");
	tokenObj.put("meshToken", token);
	objArray.append(tokenObj);
	reqObj.put("objects", objArray);

	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/put",
											reqObj.stringify().c_str()).get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		return false;
	}
	return true;
}

bool LSUtils::callDb8MeshPutAppKey(LS::Handle *serviceHandle, uint16_t appKeyInex,
									const std::string &appName)
{
	BT_INFO("MESH", 0, "appKeyInex: %d, appName: %s", appKeyInex, appName.c_str());

	pbnjson::JValue objArray = pbnjson::Array();
	pbnjson::JValue tokenObj = pbnjson::Object();
	pbnjson::JValue reqObj = pbnjson::Object();

	tokenObj.put("_kind", "com.webos.service.bluetooth2.meshappkey:1"); //kind for appkey:appname
	tokenObj.put("appKey", appKeyInex);
	tokenObj.put("appName", appName);
	objArray.append(tokenObj);
	reqObj.put("objects", objArray);

	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/put",
											reqObj.stringify().c_str()).get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		return false;
	}
	return true;
}
bool LSUtils::callDb8MeshGetAppKeys(LS::Handle *serviceHandle, pbnjson::JValue &result)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);
	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/find",
											"{\"query\":{ \"from\":\"com.webos.service.bluetooth2.meshappkey:1\"}}").get();

	BT_INFO("MESH", 0, "After API is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	result = replyObj;
	return true;
}

bool LSUtils::callDb8MeshGetNodeInfo(LS::Handle *serviceHandle, pbnjson::JValue &result)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);
	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/find",
											"{\"query\":{ \"from\":\"com.webos.service.bluetooth2.meshnodeinfo:1\"}}").get();

	BT_INFO("MESH", 0, "After API is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	result = replyObj;
	return true;

}
bool LSUtils::callDb8MeshPutNodeInfo(LS::Handle *serviceHandle, uint16_t unicastAddress, const std::string &uuid, uint8_t count)
{
	pbnjson::JValue objArray = pbnjson::Array();
	pbnjson::JValue nodeInfoObj = pbnjson::Object();
	pbnjson::JValue reqObj = pbnjson::Object();
	pbnjson::JValue appKeyIndexesObjArr = pbnjson::Array();

	nodeInfoObj.put("_kind", "com.webos.service.bluetooth2.meshnodeinfo:1");
	nodeInfoObj.put("unicastAddress", unicastAddress);
	nodeInfoObj.put("uuid", uuid);
	nodeInfoObj.put("count", count);
	nodeInfoObj.put("netKeyIndex", 0);
	nodeInfoObj.put("appKeyIndexes", appKeyIndexesObjArr);
	objArray.append(nodeInfoObj);
	reqObj.put("objects", objArray);

	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/put",
											reqObj.stringify().c_str()).get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		return false;
	}
	return true;
}

std::string LSUtils::getObjectID(LS::Handle *serviceHandle, uint16_t unicastAddress)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);
	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/find",
											"{\"query\":{ \"from\":\"com.webos.service.bluetooth2.meshnodeinfo:1\"}}").get();
  std::string id;
	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		BT_INFO("MESH", 0, "Db8 find API returned error: %d==%s : [%s : %d]",
			replyObj["errorCode"].asNumber<int32_t>(), replyObj["errorText"].asString().c_str(),
			__FUNCTION__, __LINE__);
		return id;
	}

	BT_DEBUG("replyObj: %s", replyObj.stringify().c_str());
	pbnjson::JValue resultsObj = pbnjson::Array();
	pbnjson::JValue results = replyObj["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			if (results[i].hasKey("unicastAddress"))
			{
				pbnjson::JValue meshEntry = results[i];
				if(unicastAddress == meshEntry["unicastAddress"].asNumber<int32_t>())
				{
					return meshEntry["_id"].asString();
				}
			}
		}
	}
	return id;
}

std::string LSUtils::getObjectIDByUUID(LS::Handle *serviceHandle, const std::string &uuid)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);
	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/find",
											"{\"query\":{ \"from\":\"com.webos.service.bluetooth2.meshnodeinfo:1\"}}").get();
  std::string id;
	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		BT_INFO("MESH", 0, "Db8 find API returned error: %d==%s : [%s : %d]",
			replyObj["errorCode"].asNumber<int32_t>(), replyObj["errorText"].asString().c_str(),
			__FUNCTION__, __LINE__);
		return id;
	}

	BT_DEBUG("replyObj: %s", replyObj.stringify().c_str());
	pbnjson::JValue resultsObj = pbnjson::Array();
	pbnjson::JValue results = replyObj["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		for (int i = 0; i < results.arraySize(); ++i)
		{
			if (results[i].hasKey("uuid"))
			{
				pbnjson::JValue meshEntry = results[i];
				if(uuid == meshEntry["uuid"].asString())
				{
					return meshEntry["_id"].asString();
				}
			}
		}
	}
	return id;
}

bool LSUtils::callDb8MeshDeleteNode(LS::Handle *serviceHandle, uint16_t unicastAddress)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);

	std::string id =  getObjectID(serviceHandle, unicastAddress);
	if(id.empty())
	{
		BT_INFO("MESH", 0, "unicastAddress is not present in db: %d", unicastAddress);
		return true;
	}

	bool returnValue = callDb8DeleteId(serviceHandle, id);
	if (!returnValue)
	{
		BT_INFO("MESH", 0, "delete id from db failed: %s", id.c_str());
		return false;
	}

	BT_INFO("MESH", 0, "delete id from db success: %s", id.c_str());
	return true;
}

bool LSUtils::callDb8DeleteId(LS::Handle *serviceHandle, const std::string &id)
{

	pbnjson::JValue objArray = pbnjson::Array();
	pbnjson::JValue nodeInfoObj = pbnjson::Object();
	pbnjson::JValue reqObj = pbnjson::Object();

	objArray.append(id);
	reqObj.put("ids", objArray);

	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/del",
											reqObj.stringify().c_str()).get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		return false;
	}
	return true;
}

bool LSUtils::callDb8UpdateAppkey(LS::Handle *serviceHandle, uint16_t unicastAddress, std::vector<uint16_t> appKeyIndexes)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);

	std::string id =  getObjectID(serviceHandle, unicastAddress);
	if(id.empty())
	{
		BT_INFO("MESH", 0, "unicastAddress is not present in db: %d", unicastAddress);
		return true;
	}
	bool returnValue = callDb8UpdateId(serviceHandle, id, appKeyIndexes);
	if (!returnValue)
	{
		BT_INFO("MESH", 0, "Update appkeys for unicastAddress %d failed", unicastAddress);
		return false;
	}

	BT_INFO("MESH", 0, "Update appkeys for unicastAddress %d success", unicastAddress);
	return true;
}

bool LSUtils::callDb8UpdateId(LS::Handle *serviceHandle, const std::string &id, std::vector<uint16_t> appKeyIndexes)
{
	BT_INFO("MESH", 0, "API is called : [%s : %d]", __FUNCTION__, __LINE__);

	pbnjson::JValue objArray = pbnjson::Array();
	pbnjson::JValue tokenObj = pbnjson::Object();
	pbnjson::JValue reqObj = pbnjson::Object();

	pbnjson::JValue appKeyIndexesArray = pbnjson::Array();

	for (unsigned int i = 0; i < appKeyIndexes.size(); i++)
		appKeyIndexesArray.append(appKeyIndexes[i]);


	tokenObj.put("_id", id);
	tokenObj.put("appKeyIndexes", appKeyIndexesArray);
	objArray.append(tokenObj);
	reqObj.put("objects", objArray);

	auto reply = serviceHandle->callOneReply("luna://com.webos.service.db/merge",
											reqObj.stringify().c_str()).get();

	pbnjson::JValue replyObj = pbnjson::Object();
	LSUtils::parsePayload(reply.getPayload(), replyObj);

	bool returnValue = replyObj["returnValue"].asBool();
	if (!returnValue)
	{
		return false;
	}
	return true;
}