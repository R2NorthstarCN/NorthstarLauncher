#include "pch.h"
#include "squirrel.h"
#include "masterserver.h"
#include "serverauthentication.h"
#include "hooks.h"
#include "r2client.h"
#include "scriptmasterservermessages.h"

MasterserverMessenger *g_pMasterserverMessenger = new MasterserverMessenger;

ADD_SQFUNC("string", NSGetLastMasterserverMessage, "", "", ScriptContext::SERVER)
{
	if (g_pMasterserverMessenger->m_vQueuedMasterserverMessages.empty())
	{
		g_pSquirrel<context>->pushstring(sqvm, "none", -1);
		return SQRESULT_NOTNULL;
	}
	std::string thismessage = g_pMasterserverMessenger->m_vQueuedMasterserverMessages.front();
	g_pSquirrel<context>->pushstring(sqvm, thismessage.c_str(), -1); // message content
	g_pMasterserverMessenger->m_vQueuedMasterserverMessages.pop();
	return SQRESULT_NOTNULL;
}

