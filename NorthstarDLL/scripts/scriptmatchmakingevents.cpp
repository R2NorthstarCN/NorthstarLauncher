#include "pch.h"
#include "squirrel/squirrel.h"
#include "masterserver/masterserver.h"
#include "server/auth/serverauthentication.h"
#include "core/hooks.h"
#include "client/r2client.h"
#include "scriptmasterservermessages.h"
#include "scriptmatchmakingevents.h"

AUTOHOOK_INIT()

std::string MatchmakeStatus::GetByParam(int idx)
{
	switch (idx)
	{
	case 1:
		return this->playlistName;
	case 2:
		return this->etaSeconds;
	case 3:
		return this->mapIdx;
	case 4:
		return this->modeIdx;
	case 5:
		return this->PlaylistList;
	}
}
void MatchmakeStatus::UpdateValues(
	std::string playlistname, std::string etaseconds, std::string mapidx, std::string modeidx, std::string playlistlist)
{
	this->playlistName = playlistname;
	this->etaSeconds = etaseconds;
	this->mapIdx = mapidx;
	this->modeIdx = modeidx;
	this->PlaylistList = playlistlist;
}

Matchmaker::Matchmaker(std::string playlistlist)
{
	spdlog::info("[Matchmaker] Starting new matchmaker of playlistlist:{}", playlistlist);
	MatchmakeStatus *statusptr = new MatchmakeStatus;
	this->status = statusptr;
	this->playlistlist = playlistlist;
}

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
// clang-format off
AUTOHOOK(CCLIENT__AreWeMatchmaking, client.dll + 0x211970, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(clientsqvm, AreWeMatchMakingTemp);
	return SQRESULT_NOTNULL;
}
// clang-format off
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
// clang-format off
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
