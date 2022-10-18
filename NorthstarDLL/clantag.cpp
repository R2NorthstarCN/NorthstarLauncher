#include "pch.h"
#include "dedicated.h"
#include <cstring>
#include "masterserver.h"
#include <mutex>
#include "squirrel.h"
AUTOHOOK_INIT();
typedef char* (*WriteClanTagFunc)(void* arraybase,int playerindex);
WriteClanTagFunc OriginalWriteClanTagFunc;

static SQRESULT SQ_SetLocalPlayerClanTag(HSquirrelVM* sqvm)
{
	std::string clantag = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 1);
	bool result = g_pMasterServerManager->SetLocalPlayerClanTag(clantag);
	g_pMasterServerManager->m_ClanTagsMutex.lock();
	std::string localplayername = R2::g_pCVar->FindVar("name")->GetString();
	g_pMasterServerManager->m_ClanTags.insert_or_assign(localplayername, clantag);
	g_pMasterServerManager->m_ClanTagsMutex.unlock();
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(sqvm, result);
	return SQRESULT_NOTNULL;
}

static SQRESULT SQ_GetLocalPlayerClanTag(HSquirrelVM* sqvm)
{
	std::string localplayername = R2::g_pCVar->FindVar("name")->GetString();
	if (g_pMasterServerManager->m_ClanTags.count(localplayername) == 0)
	{
		g_pMasterServerManager->GetClanTagFromUsername(localplayername);
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(sqvm, "", -1);
	}
	else
	{
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(sqvm, g_pMasterServerManager->m_ClanTags[localplayername].c_str(), -1);
	}
	return SQRESULT_NOTNULL;
}

AUTOHOOK(WriteClanTagFunctionHook, client.dll + 0x164600, char*,__fastcall, (void* arraybase, int playerindex))
{
	char* playerName = OriginalWriteClanTagFunc(arraybase, playerindex);
	std::string convertedName(playerName);
	
	if (g_pMasterServerManager->m_ClanTags.count(convertedName) == 0)
	{
		g_pMasterServerManager->GetClanTagFromUsername(convertedName);
		return playerName;
	}
	std::string clantagbuffer = g_pMasterServerManager->m_ClanTags[convertedName];
	if (clantagbuffer.empty())
		clantagbuffer = "ADV";
	char finalname[288] = { '\0' };
	strcat_s(finalname, "[");
	strcat_s(finalname, clantagbuffer.c_str());
	strcat_s(finalname, "] ");
	strcat_s(finalname, playerName);
	return finalname;
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ClantagInitialize, ClientSquirrel, (CModule module))
{
	if (IsDedicatedServer())
		return;
	AUTOHOOK_DISPATCH();
	g_pSquirrel<ScriptContext::UI>->AddFuncRegistration(
		"bool", "NSSetLocalPlayerClanTag", "string clantag", "", SQ_SetLocalPlayerClanTag);
	g_pSquirrel<ScriptContext::UI>->AddFuncRegistration("string", "NSGetLocalPlayerClanTag", "", "", SQ_GetLocalPlayerClanTag);
}
