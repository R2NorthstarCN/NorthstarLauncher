#include "pch.h"
#include "dedicated.h"
#include <cstring>
#include "masterserver.h"
#include <mutex>
#include "squirrel.h"
#include "r2engine.h"


static SQRESULT SQ_SetLocalPlayerClanTag(HSquirrelVM* sqvm)
{
	std::string clantag = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 1);
	bool result = g_pMasterServerManager->SetLocalPlayerClanTag(clantag);
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(sqvm, result);
	return SQRESULT_NOTNULL;
}



ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ClantagInitialize, ClientSquirrel, (CModule module))
{
	if (IsDedicatedServer())
		return;
	g_pSquirrel<ScriptContext::UI>->AddFuncRegistration(
		"bool", "NSSetLocalPlayerClanTag", "string clantag", "", SQ_SetLocalPlayerClanTag);
}
