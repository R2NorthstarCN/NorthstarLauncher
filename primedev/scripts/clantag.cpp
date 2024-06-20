#include "dedicated/dedicated.h"
#include <cstring>
#include "masterserver/masterserver.h"
#include <mutex>
#include "squirrel/squirrel.h"
#include "engine/r2engine.h"
#include "core/hooks.h"
#include <cstring>


AUTOHOOK_INIT()



static SQRESULT SQ_SetLocalPlayerClanTag(HSquirrelVM* sqvm)
{
	std::string clantag = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 1);
	bool result = g_pMasterServerManager->SetLocalPlayerClanTag(clantag);
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(sqvm, result);
	return SQRESULT_NOTNULL;
}





AUTOHOOK(StryderShit,engine.dll + 0x1712F0, char*,__fastcall,
	(__int64* a1,int a2, char * a3,int a4,__int64 a5,const char* a6,
		 char* a7,int a8,void* a9,int a10,int a11, char*Src,
		int a13,__int64 a14,__int64 a15,__int64 a16,__int64 a17,int a18))
{
	//if (Src != NULL)
		//spdlog::info("||HEADER | {} ||",Src);
	//if (a9 != NULL)
		//spdlog::info("||BODY | {} || {} ||", a9,(char*)a9);

	//if (!strcmp(a7,"datacenters_v3_tchinese.txt"))
	//{
	//	std::string test = "test.northstar.cool";
	//	spdlog::info("{} -----> {}", test, a7);
	//	return StryderShit(a1, a2, (char*)test.c_str(), a4, a5, a6, a7, a8, a9, a10, a11, Src, a13, a14, a15, a16, a17, a18);
	//}
	//spdlog::info("{} -----> {}", a3, a7);
	return StryderShit(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,Src,a13,a14,a15,a16,a17,a18);
}

ON_DLL_LOAD_CLIENT("engine.dll", ClantagInitializeServer, (CModule module))
{
	AUTOHOOK_DISPATCH();
}
ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ClantagInitializeClient, ClientSquirrel, (CModule module))
{
	
	if (IsDedicatedServer())
		return;
	g_pSquirrel<ScriptContext::UI>->AddFuncRegistration(
		"bool", "NSSetLocalPlayerClanTag", "string clantag", "", SQ_SetLocalPlayerClanTag);
}
