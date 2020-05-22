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

		auto reply = handle->callOneReply("luna://com.webos.service.sessionmanager/getSessionInfo", payloadstr.c_str()).get();
		pbnjson::JValue replyObj = pbnjson::Object();
		LSUtils::parsePayload(reply.getPayload(), replyObj);

		bool returnValue = replyObj["returnValue"].asBool();
		if (!returnValue) {
			return HOST;
		}

		auto sessionInfo = replyObj["sessionInfo"];
		auto deviceInfo = sessionInfo["deviceSetInfo"];
		auto deviceSetId = deviceInfo["deviceSetId"].asString();

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
