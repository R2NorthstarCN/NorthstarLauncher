#include "pch.h"
#include "dedicated.h"
#include <cstring>
#include "masterserver.h"
#include <mutex>
#include "squirrel.h"
#include "r2engine.h"
#include "hooks.h"


AUTOHOOK_INIT()

std::map<std::string,std::string> m_ClanTags;




std::string GetClanTagFromUsername(std::string uid)
{
	
}


static SQRESULT SQ_SetLocalPlayerClanTag(HSquirrelVM* sqvm)
{
	std::string clantag = g_pSquirrel<ScriptContext::CLIENT>->getstring(sqvm, 1);
	bool result = g_pMasterServerManager->SetLocalPlayerClanTag(clantag);
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(sqvm, result);
	return SQRESULT_NOTNULL;
}


AUTOHOOK(CBaseClient__SetName, engine.dll + 0x105ED0, void, __fastcall, (R2::CBaseClient * player, char* name))
{

	
	char* clantag = ((char*)player + 0x318 + 64);
	strcpy(clantag, "TEST");
	return CBaseClient__SetName(player,name);
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

/*
AUTOHOOK(GetClanTag, client.dll + 0x164600, char*, __fastcall, (void* arraybase, int playerindex))
{
	char* playerName = GetClanTag(arraybase, playerindex);
	std::string convertedName(playerName);

	if (m_ClanTags.count(convertedName) == 0)
	{
		GetClanTagFromUsername(convertedName);
		return playerName;
	}
	std::string clantagbuffer = g_pMasterServerManager->m_ClanTags[convertedName];
	if (clantagbuffer.empty())
		clantagbuffer = "ADV";
	char finalname[288] = {'\0'};
	strcat_s(finalname, "[");
	strcat_s(finalname, clantagbuffer.c_str());
	strcat_s(finalname, "] ");
	strcat_s(finalname, playerName);
	return finalname;
}
*/
ON_DLL_LOAD_CLIENT("engine.dll", RespawnShit, (CModule module))
{
	AUTOHOOK_DISPATCH();
}
ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ClantagInitialize, ClientSquirrel, (CModule module))
{
	
	if (IsDedicatedServer())
		return;
	g_pSquirrel<ScriptContext::UI>->AddFuncRegistration(
		"bool", "NSSetLocalPlayerClanTag", "string clantag", "", SQ_SetLocalPlayerClanTag);
}
