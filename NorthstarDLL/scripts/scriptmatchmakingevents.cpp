#include "pch.h"
#include "squirrel/squirrel.h"
#include "masterserver/masterserver.h"
#include "server/auth/serverauthentication.h"
#include "core/hooks.h"
#include "client/r2client.h"
#include "scriptmasterservermessages.h"
AUTOHOOK_INIT();

const std::string MatchMakingStatus[] = {
	"#MATCH_NOTHING",
	"#MATCHMAKING_SEARCHING_FOR_MATCH",
	"#MATCHMAKING_QUEUED",
	"#MATCHMAKING_ALLOCATING_SERVER",
	"#MATCHMAKING_MATCH_CONNECTING"};

struct DummyMatchmakingParamData
{
	std::string playlistName = "ps";
	std::string etaSeconds = "30";
	std::string mapIdx = "1";
	std::string modeIdx = "1";
	std::string PlaylistList = "ps,aitdm";
};

int CurrentMatchmakingStatus = 0;
bool AreWeMatchMakingTemp = false;
// clang-format off
AUTOHOOK(CCLIENT__StartMatchmaking, client.dll + 0x213D00, SQRESULT, __fastcall, (HSquirrelVM* clientsqvm))
// clang-format on
{
	const char* str = g_pSquirrel<ScriptContext::CLIENT>->getstring(clientsqvm, 1);
	spdlog::info(str);
	AreWeMatchMakingTemp = true;
	return SQRESULT_NOTNULL;
}

AUTOHOOK(CCLIENT__AreWeMatchmaking, client.dll + 0x211970, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(clientsqvm, AreWeMatchMakingTemp);
	return SQRESULT_NOTNULL;
}

AUTOHOOK(CCLIENT__GetMyMatchmakingStatusParam, client.dll + 0x3B3570, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	DummyMatchmakingParamData* dummydata = new DummyMatchmakingParamData;
	int param = g_pSquirrel<ScriptContext::CLIENT>->getinteger(clientsqvm, 1);

	switch (param)
	{
	case 1:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, dummydata->playlistName.c_str(), -1);
	case 2:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, dummydata->etaSeconds.c_str(), -1);
	case 3:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, dummydata->mapIdx.c_str(), -1);
	case 4:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, dummydata->modeIdx.c_str(), -1);
	case 5:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, dummydata->PlaylistList.c_str(), -1);
	}

	return SQRESULT_NOTNULL;
}

AUTOHOOK(CCLIENT__GetMyMatchmakingStatus, client.dll + 0x3B1B70, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, MatchMakingStatus[CurrentMatchmakingStatus].c_str(), -1);
	return SQRESULT_NOTNULL;
}

void ConCommand_test_incstatus(const CCommand& args)
{
	if (CurrentMatchmakingStatus == 4)
	{
		CurrentMatchmakingStatus = 0;
	}
	CurrentMatchmakingStatus++;
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptMatchmakingEvents, ClientSquirrel, (CModule module))
{
	AUTOHOOK_DISPATCH();
	RegisterConCommand("inc_status", ConCommand_test_incstatus, "i hate cpp", FCVAR_NONE);
}
