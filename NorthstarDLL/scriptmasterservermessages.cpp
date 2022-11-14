#include "pch.h"
#include "squirrel.h"
#include "masterserver.h"
#include "serverauthentication.h"
#include "hooks.h"
#include "r2client.h"
#include "scriptmasterservermessages.h"

MasterserverMessenger *g_pMasterserverMessenger = new MasterserverMessenger;

ADD_SQFUNC("table", NSGetLastMasterserverMessage, "", "", ScriptContext::SERVER)
{
	if (g_pMasterserverMessenger->m_vQueuedMasterserverMessages.empty())
	{
		g_pSquirrel<context>->newtable(sqvm);
		g_pSquirrel<context>->pushstring(sqvm, "msgtype", -1);
		g_pSquirrel<context>->pushstring(sqvm, "none", -1); // message type
		g_pSquirrel<context>->newslot(sqvm, -3, false);
		return SQRESULT_NOTNULL;
	}
	std::pair<std::string, std::string> thismessage = g_pMasterserverMessenger->m_vQueuedMasterserverMessages.front();
	g_pSquirrel<context>->newtable(sqvm);
	g_pSquirrel<context>->pushstring(sqvm, "msgtype", -1);
	g_pSquirrel<context>->pushstring(sqvm, thismessage.first.c_str(), -1); // message type
	g_pSquirrel<context>->pushstring(sqvm, "message", -1);
	g_pSquirrel<context>->pushstring(sqvm, thismessage.second.c_str(), -1); // message content
	g_pSquirrel<context>->newslot(sqvm ,-5, false);
	g_pMasterserverMessenger->m_vQueuedMasterserverMessages.pop();
	return SQRESULT_NOTNULL;
}

