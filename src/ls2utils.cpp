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

	BT_DEBUG("MESH", 0, "replyObj: %s", replyObj.stringify().c_str());
	pbnjson::JValue resultsObj = pbnjson::Array();
	pbnjson::JValue results = replyObj["results"];
	if (results.isValid() && (results.arraySize() > 0))
	{
		pbnjson::JValue meshEntry = results[0];
		token = meshEntry["meshToken"].asString();
		BT_DEBUG("MESH", 0, "token received from db: %s", token.c_str());
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

