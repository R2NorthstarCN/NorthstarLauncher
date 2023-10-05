#include "pch.h"
#include "squirrel/squirrel.h"
#include "masterserver/masterserver.h"
#include "server/auth/serverauthentication.h"
#include "core/hooks.h"
#include "client/r2client.h"
#include "scriptmasterservermessages.h"
#include "scriptmatchmakingevents.h"
#include "core/tier0.h"

#define NSCN_MATCHMAKING

AUTOHOOK_INIT()

MatchmakeManager* g_pMatchmakerManager = new MatchmakeManager;

struct MatchmakeStatus_Baseline
{
	// this is used when game request for status before we had a response form masterserver.
	std::string status = "#MATCH_NOTHING";
	std::string playlistName = "ps";
	std::string etaSeconds = "30";
	std::string mapIdx = "1";
	std::string modeIdx = "1";
	std::string PlaylistList = "ps,aitdm";
};

std::string MatchmakeInfo::GetByParam(int idx)
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
		return this->playlistListstr;
	}
}

MatchmakeManager::MatchmakeManager()
{
	// Create the request thread for use later on
	std::thread* requestthreadptr = new std::thread(
		[this]()
		{
			while (true)
			{
				switch (LocalState)
				{
				case 0: // idle
					break;
				case 1: // new matchmaking event
					if (!g_pMasterServerManager->StartMatchmaking(info))
					{
						LocalState = 0;
						break;
					}
					else
					{
						LocalState = 2;
						break;
					}
					
				case 2: // matchmaking
					if (!g_pMasterServerManager->UpdateMatchmakingStatus(info))
					{
						LocalState = 0;
						break;
					}
					if (!strcmp(info->status.c_str(), "#MATCHMAKING_MATCH_CONNECTING"))
					{
						for (auto& pair : g_pServerAuthentication->m_PlayerAuthenticationData)
							g_pServerAuthentication->WritePersistentData(pair.first);

						
						// matchmake done, connect to the server
						g_pMasterServerManager->AuthenticateWithServer(
							R2::g_pLocalPlayerUserID,
							g_pMasterServerManager->m_sOwnClientAuthToken,
							info->serverId,
							"");
						while (g_pMasterServerManager->m_bScriptAuthenticatingWithGameServer)
						{
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
						}
						
						RemoteServerConnectionInfo& conn_info = g_pMasterServerManager->m_pendingConnectionInfo;
						if (!g_pMasterServerManager->m_bHasPendingConnectionInfo)
						{
							spdlog::error(
								"[Matchmaking] Failed while authenticating with matchmaking server: {} {}",
								g_pMasterServerManager->m_sAuthFailureReason,
								g_pMasterServerManager->m_sAuthFailureMessage);
							LocalState = 0;
							break;
							// TODO: cleanup
						}
						std::string connection_cmd = fmt::format(
							"kcp_connect ::ffff:{}.{}.{}.{} {} {}",
							conn_info.ip.S_un.S_un_b.s_b1,
							conn_info.ip.S_un.S_un_b.s_b2,
							conn_info.ip.S_un.S_un_b.s_b3,
							conn_info.ip.S_un.S_un_b.s_b4,
							conn_info.port,
							conn_info.conv);
						spdlog::info("Connect: {}", connection_cmd);
						R2::g_pCVar->FindVar("serverfilter")->SetValue(conn_info.authToken.c_str());
						R2::Cbuf_AddText(
							R2::Cbuf_GetCurrentPlayer(),
								connection_cmd.c_str(),
							R2::cmd_source_t::kCommandSrcCode);

						g_pMasterServerManager->m_bHasPendingConnectionInfo = false;
						LocalState = 0;
					}
					break;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			}
		});
	requestthreadptr->detach();
	this->requestthread = requestthreadptr;

	// Creating a status object
	MatchmakeInfo* statusptr = new MatchmakeInfo;
	statusptr->mapIdx = 1;
	statusptr->modeIdx = 1;
	statusptr->status = "";
	this->info = statusptr;
}
void MatchmakeManager::StartMatchmake(std::string playlistlist)
{
	if (this->LocalState == 2)
	{
		spdlog::warn("[Matchmaker] calling StartMatchmake while matchmaking!");
		return;
	}
	spdlog::info("[Matchmaker] Starting Matchmake");
	this->info->playlistListstr = playlistlist;
	std::vector<std::string> playlistvec;
	std::stringstream ss(playlistlist);
	while (ss.good())
	{
		std::string substr;
		std::getline(ss, substr, ',');
		spdlog::info("[Matchmaker] Adding playlist: {} to playlist_list", substr);
		playlistvec.push_back(substr);
	}
	this->info->playlistList = playlistvec;
	LocalState = 1;
}
void MatchmakeManager::CancelMatchmake()
{
	if (this->LocalState != 2)
	{
		spdlog::warn("[Matchmaker] calling StopMatchmake while not matchmaking!");
		return;
	}
	spdlog::info("[Matchmaker] Cancelling Matchmake");
	LocalState = 0;
	g_pMasterServerManager->CancelMatchmaking();
}

#ifdef NSCN_MATCHMAKING
// clang-format off
AUTOHOOK(CCLIENT__StartMatchmaking, client.dll + 0x213D00, SQRESULT, __fastcall, (HSquirrelVM* clientsqvm))
// clang-format on
{
	const char* str = g_pSquirrel<ScriptContext::CLIENT>->getstring(clientsqvm, 1);
	spdlog::info("[Matchmaker] Staring Matchmaking event:{}", str);
	g_pMatchmakerManager->StartMatchmake(str);

	return SQRESULT_NOTNULL;
}

// clang-format off
AUTOHOOK(CCLIENT__CancelMatchmaking, client.dll + 0x211640, SQRESULT, __fastcall, (HSquirrelVM* clientsqvm))
// clang-format on
{
	spdlog::info("[Matchmaker] Cancelled Matchmaking request");
	g_pMatchmakerManager->CancelMatchmake();

	return SQRESULT_NOTNULL;
}

// clang-format off
AUTOHOOK(CCLIENT__AreWeMatchmaking, client.dll + 0x211970, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	g_pSquirrel<ScriptContext::CLIENT>->pushbool(clientsqvm, (g_pMatchmakerManager->LocalState == 2));
	return SQRESULT_NOTNULL;
}
// clang-format off
AUTOHOOK(CCLIENT__GetMyMatchmakingStatusParam, client.dll + 0x3B3570, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	// DummyMatchmakingParamData* dummydata = new DummyMatchmakingParamData;
	int param = g_pSquirrel<ScriptContext::CLIENT>->getinteger(clientsqvm, 1);
	switch (param)
	{
	case 1:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->playlistName.c_str(), -1);
	case 2:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->etaSeconds.c_str(), -1);
	case 3:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->mapIdx.c_str(), -1);
	case 4:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->modeIdx.c_str(), -1);
	case 5:
		g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->playlistListstr.c_str(), -1);
	}

	return SQRESULT_NOTNULL;
}
// clang-format off
AUTOHOOK(CCLIENT__GetMyMatchmakingStatus, client.dll + 0x3B1B70, SQRESULT, __fastcall, (HSquirrelVM * clientsqvm))
// clang-format on
{
	g_pSquirrel<ScriptContext::CLIENT>->pushstring(clientsqvm, g_pMatchmakerManager->info->status.c_str(), -1);
	return SQRESULT_NOTNULL;
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptMatchmakingEvents, ClientSquirrel, (CModule module))
{
	if (Tier0::CommandLine()->CheckParm("-norestrictservercommands"))
	{
		spdlog::info("[Matchmaker] NSCN Matchmaker is disabled! (found -norestrictservercommands)");
		return;
	}
	AUTOHOOK_DISPATCH();
}
#endif
