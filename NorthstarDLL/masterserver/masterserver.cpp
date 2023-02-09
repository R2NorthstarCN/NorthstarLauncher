#include "pch.h"
#include "masterserver/masterserver.h"
#include "core/convar/concommand.h"
#include "playlist.h"
#include "server/auth/serverauthentication.h"
#include "core/tier0.h"
#include "engine/r2engine.h"
#include "client/r2client.h"
#include "mods/modmanager.h"
#include "shared/misccommands.h"
#include "util/version.h"
#include "util/dohworker.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include "server/auth/bansystem.h"
#include "core/anticheat.h"
#include <cstring>
#include <regex>
#include "util/base64.h"
#include "scripts/scriptgamestate.h"
#include "scriptmatchmakingevents.h"
#include "nlohmann/json.hpp"
using namespace std::chrono_literals;
MasterServerManager* g_pMasterServerManager;
ClientAnticheatSystem g_ClientAnticheatSystem;

ConVar* Cvar_ns_masterserver_hostname;
ConVar* Cvar_ns_curl_log_enable;
ConVar* Cvar_ns_server_reg_token;

void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, Cvar_ns_curl_log_enable->GetBool());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, &NSUserAgent);
	if (!strstr(GetCommandLineA(), "-disabledoh"))
	{
		std::string masterserverhostname = Cvar_ns_masterserver_hostname->GetString();
		std::string DohResult = g_DohWorker->GetDOHResolve(masterserverhostname);
		if (g_DohWorker->m_bDohAvailable)
		{
			struct curl_slist* host = NULL;
			masterserverhostname.erase(0, 8);
			std::string addr = masterserverhostname + ":443:" + DohResult;
			// spdlog::info(addr);
			host = curl_slist_append(NULL, addr.c_str());
			curl_easy_setopt(curl, CURLOPT_RESOLVE, host);
		}
		else
		{
			spdlog::warn("[DOH] service is not available. falling back to DNS");
		}
	}
	else
	{
		// spdlog::warn("[DOH] service disabled");
	}
	// curl_easy_setopt(curl, CURLOPT_STDERR, stdout);

	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
}

httplib::Client SetupHttpClient()
{
	std::string msaddr = Cvar_ns_masterserver_hostname->GetString();
	httplib::Client cli(msaddr);
	//, {"Accept-Encoding", "gzip"}, {"Content-Encoding", "gzip"}
	cli.set_default_headers({{"User-Agent", NSUserAgent}});
	cli.set_compress(true);
	cli.set_decompress(true);
	cli.set_read_timeout(10, 0);
	cli.set_write_timeout(10, 0);
	if (!strstr(GetCommandLineA(), "-disabledoh"))
	{
		std::string DohResult = g_DohWorker->GetDOHResolve(msaddr);
		if (g_DohWorker->m_bDohAvailable)
		{
			cli.set_hostname_addr_map({{msaddr, DohResult}});
		}
		else
		{
			spdlog::warn("[DOH] service is not available. falling back to DNS");
		}
	}
	return cli;
}

void MasterServerManager::ClearServerList()
{
	// this doesn't really do anything lol, probably isn't threadsafe
	m_bRequestingServerList = true;

	m_vRemoteServers.clear();

	m_bRequestingServerList = false;
}

size_t CurlWriteToStringBufferCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}
bool MasterServerManager::StartMatchmaking(MatchmakeInfo* status)
{
	// no need for a request thread here cuz we always call this function in a new thread.
	// ps,aitdm,ttdm
	CURL* curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);
	std::string readBuffer;
	std::string token = m_sOwnClientAuthToken;
	std::string localuid = R2::g_pLocalPlayerUserID;
	char* localuidescaped = curl_easy_escape(curl, localuid.c_str(), localuid.length());
	char* tokenescaped = curl_easy_escape(curl, token.c_str(), token.length());
	std::string queryfmtstr = "{}/matchmaking/join?id={}&token={}";
	for (int i = 0; i < status->playlistList.size(); i++)
	{
		queryfmtstr.append(fmt::format("&playlistList[{}]={}", i, status->playlistList[i]));
	}
	spdlog::warn("{}", queryfmtstr);
	curl_easy_setopt(
		curl, CURLOPT_URL, fmt::format(queryfmtstr, Cvar_ns_masterserver_hostname->GetString(), localuidescaped, tokenescaped).c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document serverresponse;
		serverresponse.Parse(readBuffer.c_str());

		if (serverresponse.HasParseError())
		{
			// spdlog::error(
			//"Failed reading player clantag: encountered parse error \"{}\"",
			// rapidjson::GetParseError_En(serverresponse.GetParseError()));
			goto REQUEST_END_CLEANUP;
		}

		if (!serverresponse.IsObject() || !serverresponse.HasMember("success"))
		{
			// spdlog::error("Failed reading origin auth info response: malformed response object {}", readBuffer);
			goto REQUEST_END_CLEANUP;
		}

		if (serverresponse["success"].IsTrue())
		{
			// std::cout << "Successfully set local player clantag." << std::endl;
			curl_easy_cleanup(curl);
			return true;
		}

		// spdlog::error("Failed reading player clantag");
	}

	// we goto this instead of returning so we always hit this
REQUEST_END_CLEANUP:

	curl_easy_cleanup(curl);
	return false;
}
bool MasterServerManager::CancelMatchmaking()
{
	// no need for a request thread here cuz we always call this function in a new thread.
	// ps,aitdm,ttdm
	CURL* curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);
	std::string readBuffer;
	std::string token = m_sOwnClientAuthToken;
	char* tokenescaped = curl_easy_escape(curl, token.c_str(), token.length());
	std::string localuid = R2::g_pLocalPlayerUserID;
	char* localuidescaped = curl_easy_escape(curl, localuid.c_str(), localuid.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format("{}/matchmaking/quit?id={}&token={}", Cvar_ns_masterserver_hostname->GetString(), localuidescaped, tokenescaped)
			.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document serverresponse;
		serverresponse.Parse(readBuffer.c_str());

		if (serverresponse.HasParseError())
		{
			// spdlog::error(
			//"Failed reading player clantag: encountered parse error \"{}\"",
			// rapidjson::GetParseError_En(serverresponse.GetParseError()));
			goto REQUEST_END_CLEANUP;
		}

		if (!serverresponse.IsObject() || !serverresponse.HasMember("success"))
		{
			// spdlog::error("Failed reading origin auth info response: malformed response object {}", readBuffer);
			goto REQUEST_END_CLEANUP;
		}

		if (serverresponse["success"].IsTrue())
		{
			// std::cout << "Successfully set local player clantag." << std::endl;
			curl_easy_cleanup(curl);
			return true;
		}

		// spdlog::error("Failed reading player clantag");
	}

	// we goto this instead of returning so we always hit this
REQUEST_END_CLEANUP:

	curl_easy_cleanup(curl);
	return false;
}

bool MasterServerManager::UpdateMatchmakingStatus(MatchmakeInfo* status)
{
	// no need for a request thread here cuz we always call this function in a new thread.
	// ps,aitdm,ttdm
	CURL* curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);
	std::string readBuffer;
	std::string token = m_sOwnClientAuthToken;
	std::string localuid = R2::g_pLocalPlayerUserID;
	char* localuidescaped = curl_easy_escape(curl, localuid.c_str(), localuid.length());
	char* tokenescaped = curl_easy_escape(curl, token.c_str(), token.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format("{}/matchmaking/state?id={}&token={}", Cvar_ns_masterserver_hostname->GetString(), localuidescaped, tokenescaped)
			.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document serverresponse;
		serverresponse.Parse(readBuffer.c_str());

		if (serverresponse.HasParseError())
		{

			goto REQUEST_END_CLEANUP;
		}

		if (!serverresponse.IsObject() || !serverresponse.HasMember("success") && serverresponse.HasMember("state") &&
											  serverresponse["state"].HasMember("type") && serverresponse["success"].IsTrue())
		{

			goto REQUEST_END_CLEANUP;
		}
		// spdlog::info("{}", serverresponse["state"]["type"].GetString());
		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_QUEUED"))
		{
			if (!serverresponse["state"].HasMember("eta_seconds") || !serverresponse["state"]["eta_seconds"].IsNumber())
				goto REQUEST_END_CLEANUP;
			status->etaSeconds = std::to_string(serverresponse["info"]["etaSeconds"].GetInt());
			status->status = serverresponse["state"]["type"].GetString();
			spdlog::info("[Matchmaker] MATCHMAKING_QUEUED");
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_CONFIRMING"))
		{
			if (!serverresponse["state"].HasMember("timeout") || !serverresponse["state"]["timeout"].IsNumber() ||
				!serverresponse["state"].HasMember("playlist_name") || !serverresponse["state"]["playlist_name"].IsString())
				goto REQUEST_END_CLEANUP;
			status->timeout = std::to_string(serverresponse["info"]["timeout"].GetInt());
			status->playlistName = serverresponse["info"]["playlist_name"].GetString();
			status->status = serverresponse["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_CONFIRMED"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_CONFIRMED");
			status->status = serverresponse["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_ALLOCATING"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_ALLOCATING");
			status->status = serverresponse["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_CONNECTING"))
		{
			if (!serverresponse["state"].HasMember("ip") || !serverresponse["state"]["ip"].IsString() ||
				!serverresponse["state"].HasMember("auth_token") || !serverresponse["state"]["auth_token"].IsString() ||
				!serverresponse["state"].HasMember("port") || !serverresponse["state"]["port"].IsNumber())
				goto REQUEST_END_CLEANUP;

			status->etaSeconds = std::to_string(serverresponse["state"]["etaSeconds"].GetInt());
			MatchmakeConnectionInfo* connctionInfo = new MatchmakeConnectionInfo;
			connctionInfo->ip.S_un.S_addr = inet_addr(serverresponse["state"]["ip"].GetString());
			connctionInfo->port = (unsigned short)serverresponse["state"]["port"].GetUint();

			strncpy_s(
				connctionInfo->authToken,
				sizeof(m_pendingConnectionInfo.authToken),
				serverresponse["state"]["authToken"].GetString(),
				sizeof(m_pendingConnectionInfo.authToken) - 1);

			status->connectionInfo = connctionInfo;
			status->serverReady = true;
			status->status = serverresponse["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(serverresponse["state"]["type"].GetString(), "#MATCHMAKING_NOTHING"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_NOTHING");
			status->status = serverresponse["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		spdlog::error("Failed reading matchmaking status");
	}

	// we goto this instead of returning so we always hit this
REQUEST_END_CLEANUP:

	curl_easy_cleanup(curl);
	return false;
}

bool MasterServerManager::SetLocalPlayerClanTag(std::string clantag)
{

	httplib::Client cli = SetupHttpClient();
	std::string querystring =
		fmt::format("/client/clantag?clantag={}&id={}&token={}", clantag, R2::g_pLocalPlayerUserID, m_sOwnClientAuthToken);

	auto res = cli.Post(querystring);
	if (res && res->status == 200)
	{
		try
		{
			nlohmann::json setclantagJson = nlohmann::json::parse(res->body);
			if (setclantagJson.at("success"))
			{
				return true;
			}
			else
			{
				return false;
			}
		}
		catch (nlohmann::json::parse_error& e)
		{
			spdlog::error("Failed setting local player clantag: \"{}\"", e.what());
		}
		catch (nlohmann::json::out_of_range& e)
		{
			spdlog::error("Failed setting local player clantag: \"{}\"", e.what());
		}

		return false;
	}
}

void MasterServerManager::AuthenticateOriginWithMasterServer(const char* uid, const char* originToken)
{
	if (m_bOriginAuthWithMasterServerInProgress)
		return;

	// do this here so it's instantly set
	m_bOriginAuthWithMasterServerInProgress = true;
	std::string uidStr(uid);
	std::string tokenStr(originToken);

	std::thread requestThread(
		[this, uidStr, tokenStr]()
		{
			spdlog::info("Trying to authenticate with northstar masterserver for user {}", uidStr);

			httplib::Client cli = SetupHttpClient();
			std::string endpoint = fmt::format("/client/origin_auth?id={}&token={}", uidStr, tokenStr);

			if (auto res = cli.Get(endpoint))
			{
				m_bSuccessfullyConnected = true;
				try
				{
					nlohmann::json originAuthInfo = nlohmann::json::parse(res->body);
					if (originAuthInfo["success"])
					{
						m_sOwnClientAuthToken = originAuthInfo.at("token");
						m_bOriginAuthWithMasterServerSuccess = true;
						spdlog::info("Northstar origin authentication completed successfully!");
					}
					else
					{
						m_sAuthFailureReason = originAuthInfo.at("error").at("enum");
						m_sAuthFailureMessage = originAuthInfo.at("error").at("msg");
						spdlog::error("Northstar origin authentication failed: {}", m_sAuthFailureMessage);
					}
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed reading origin auth info response: encountered parse error \"{}\"", e.what());
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed reading origin auth info response: encountered data error \"{}\"", e.what());
				}
			}
			else
			{
				auto err = res.error();
				m_sAuthFailureReason = std::string("ERROR_NO_CONNECTION");
				m_sAuthFailureMessage = fmt::format("与主服务器进行初始化通信时出现错误：{}", httplib::to_string(err));
				spdlog::error("Failed performing northstar origin auth: {}", httplib::to_string(err));
				m_bSuccessfullyConnected = false;
			}

			m_bOriginAuthWithMasterServerInProgress = false;
			m_bOriginAuthWithMasterServerDone = true;
		});

	requestThread.detach();
}

void MasterServerManager::RequestServerList()
{
	// do this here so it's instantly set on call for scripts
	m_bScriptRequestingServerList = true;

	std::thread requestThread(
		[this]()
		{
			// make sure we never have 2 threads writing at once
			// i sure do hope this is actually threadsafe
			while (m_bRequestingServerList)
				Sleep(100);

			m_bRequestingServerList = true;
			m_bScriptRequestingServerList = true;

			httplib::Client cli = SetupHttpClient();

			if (auto res = cli.Get("/client/servers"))
			{
				m_bSuccessfullyConnected = true;
				try
				{
					nlohmann::json serverListJson = nlohmann::json::parse(res->body);

					m_vRemoteServers.clear();

					for (auto& serverObj : serverListJson)
					{
						RemoteServerInfo server;

						server.id = serverObj.at("id");
						server.name = serverObj.at("name");
						server.description = serverObj.at("description");
						server.map = serverObj.at("map");
						server.playlist = serverObj.at("playlist");
						server.gameState = serverObj.at("gameState");
						server.playerCount = serverObj.at("playerCount");
						server.maxPlayers = serverObj.at("maxPlayers");
						server.requiresPassword = serverObj.at("hasPassword");

						for (auto& mod : serverObj.at("modInfo").at("Mods"))
						{
							if (mod.at("RequiredOnClient"))
							{
								RemoteModInfo modInfo;

								modInfo.Name = mod.at("Name");
								modInfo.Version = mod.at("Version");
								server.requiredMods.push_back(modInfo);
							}
						}

						m_vRemoteServers.push_back(server);
					}

					std::sort(
						m_vRemoteServers.begin(),
						m_vRemoteServers.end(),
						[](RemoteServerInfo& a, RemoteServerInfo& b) { return a.playerCount > b.playerCount; });
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed reading origin auth info response: encountered parse error \"{}\"", e.what());
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed reading origin auth info response: encountered data error \"{}\"", e.what());
				}
			}
			else
			{
				auto err = res.error();
				spdlog::error("Failed requesting servers: error {}", err);
				m_bSuccessfullyConnected = false;
			}
			m_bRequestingServerList = false;
			m_bScriptRequestingServerList = false;
		});

	requestThread.detach();
}

void MasterServerManager::RequestMainMenuPromos()
{
	m_bHasMainMenuPromoData = false;

	std::thread requestThread(
		[this]()
		{
			while (m_bOriginAuthWithMasterServerInProgress || !m_bOriginAuthWithMasterServerDone)
				Sleep(500);

			httplib::Client cli = SetupHttpClient();
			if (auto res = cli.Get("/client/mainmenupromos"))
			{
				m_bSuccessfullyConnected = true;
				try
				{
					nlohmann::json mainMenuPromoJson = nlohmann::json::parse(res->body);
					m_sMainMenuPromoData.newInfoTitle1 = mainMenuPromoJson.at("newInfo").at("Title1");
					m_sMainMenuPromoData.newInfoTitle2 = mainMenuPromoJson.at("newInfo").at("Title2");
					m_sMainMenuPromoData.newInfoTitle3 = mainMenuPromoJson.at("newInfo").at("Title3");

					m_sMainMenuPromoData.largeButtonTitle = mainMenuPromoJson.at("largeButton").at("Title");
					m_sMainMenuPromoData.largeButtonText = mainMenuPromoJson.at("largeButton").at("Text");
					m_sMainMenuPromoData.largeButtonUrl = mainMenuPromoJson.at("largeButton").at("Url");
					m_sMainMenuPromoData.largeButtonImageIndex = mainMenuPromoJson.at("largeButton").at("ImageIndex");

					m_sMainMenuPromoData.smallButton1Title = mainMenuPromoJson.at("smallButton1").at("Title");
					m_sMainMenuPromoData.smallButton1Url = mainMenuPromoJson.at("smallButton1").at("Url");
					m_sMainMenuPromoData.smallButton1ImageIndex = mainMenuPromoJson.at("smallButton1").at("ImageIndex");

					m_sMainMenuPromoData.smallButton2Title = mainMenuPromoJson.at("smallButton2").at("Title");
					m_sMainMenuPromoData.smallButton2Url = mainMenuPromoJson.at("smallButton2").at("Url");
					m_sMainMenuPromoData.smallButton2ImageIndex = mainMenuPromoJson.at("smallButton2").at("ImageIndex");

					m_bHasMainMenuPromoData = true;
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed reading masterserver main menu promos response: encountered parse error \"{}\"", e.what());
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed reading masterserver main menu promos response: encountered data error \"{}\"", e.what());
				}
			}
			else
			{
				auto err = res.error();
				spdlog::error("Failed reading masterserver main menu promos response:: {}", httplib::to_string(err));
				m_bSuccessfullyConnected = false;
			}
		});

	requestThread.detach();
}

void MasterServerManager::AuthenticateWithOwnServer(const char* uid, const std::string& playerToken)
{
	// dont wait, just stop if we're trying to do 2 auth requests at once
	if (m_bAuthenticatingWithGameServer)
		return;

	m_sAuthFailureReason = "No error message provided";
	m_sAuthFailureMessage = "No error message provided";

	m_bAuthenticatingWithGameServer = true;
	m_bScriptAuthenticatingWithGameServer = true;
	m_bSuccessfullyAuthenticatedWithGameServer = false;
	std::string uidStr(uid);
	std::string tokenStr(playerToken);
	std::thread requestThread(
		[this, uidStr, tokenStr]()
		{
			std::string querystring = fmt::format("/client/auth_with_self?id={}&playerToken={}", uidStr, tokenStr);
			httplib::Client cli = SetupHttpClient();

			if (auto res = cli.Post(querystring))
			{
				try
				{
					// spdlog::info("{}", res->status);
					nlohmann::json authInfoJson = nlohmann::json::parse(res->body);
					RemoteAuthData newAuthData;

					newAuthData.uid = authInfoJson.at("id");

					std::string original = authInfoJson.at("persistentData");
					newAuthData.pdata = base64_decode(original);

					std::lock_guard<std::mutex> guard(g_pServerAuthentication->m_AuthDataMutex);
					g_pServerAuthentication->m_RemoteAuthenticationData.clear();
					g_pServerAuthentication->m_RemoteAuthenticationData.insert(std::make_pair(authInfoJson.at("authToken"), newAuthData));

					m_bSuccessfullyAuthenticatedWithGameServer = true;
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed authenticating with local server: encountered parse error \"{}\"", e.what());
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed authenticating with local server: encountered data error \"{}\"", e.what());
				}
			}
			else
			{
				auto err = res.error();
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;

				m_bAuthenticatingWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;

				if (m_bNewgameAfterSelfAuth)
				{
					// pretty sure this is threadsafe?
					R2::Cbuf_AddText(R2::Cbuf_GetCurrentPlayer(), "ns_end_reauth_and_leave_to_lobby", R2::cmd_source_t::kCommandSrcCode);
					m_bNewgameAfterSelfAuth = false;
				}
			}
		});

	requestThread.detach();
}

void MasterServerManager::AuthenticateWithServer(
	const char* uid, const std::string& playerToken, const std::string& serverId, const char* password)
{
	// dont wait, just stop if we're trying to do 2 auth requests at once
	if (m_bAuthenticatingWithGameServer)
		return;
	m_sAuthFailureReason = "No error message provided";
	m_sAuthFailureMessage = "No error message provided";

	m_bAuthenticatingWithGameServer = true;
	m_bScriptAuthenticatingWithGameServer = true;
	m_bSuccessfullyAuthenticatedWithGameServer = false;

	std::string uidStr(uid);
	std::string tokenStr(playerToken);
	std::string serverIdStr(serverId);
	std::string passwordStr(password);

	std::thread requestThread(
		[this, uidStr, tokenStr, serverIdStr, passwordStr]()
		{
			// esnure that any persistence saving is done, so we know masterserver has newest
			while (m_bSavingPersistentData)
				Sleep(100);

			spdlog::info("Attempting authentication with server of id \"{}\"", serverIdStr);

			httplib::Client cli = SetupHttpClient();
			std::string querystring = fmt::format(
				"/client/auth_with_server?id={}&playerToken={}&server={}&password={}", uidStr, tokenStr, serverIdStr, passwordStr);
			auto res = cli.Post(querystring);

			if (res && res->status == 200)
			{
				m_bSuccessfullyConnected = true;
				try
				{
					nlohmann::json connectionInfoJson = nlohmann::json::parse(res->body);
					if (connectionInfoJson.at("success") == true)
					{
						m_pendingConnectionInfo.ip.S_un.S_addr = inet_addr(std::string(connectionInfoJson.at("ip")).c_str());
						m_pendingConnectionInfo.port = (unsigned short)connectionInfoJson.at("port");
						m_pendingConnectionInfo.authToken = connectionInfoJson.at("authToken");
						m_bHasPendingConnectionInfo = true;
						m_bSuccessfullyAuthenticatedWithGameServer = true;
					}
					else
					{
						m_sAuthFailureReason = connectionInfoJson.at("error").at("enum");
						m_sAuthFailureMessage = connectionInfoJson.at("error").at("msg");
					}
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed authenticating with local server: encountered parse error \"{}\"", e.what());
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed authenticating with local server: encountered data error \"{}\"", e.what());
				}
			}
			else
			{
				spdlog::error(
					"Failed authenticating with server: {}",
					res.error() == httplib::Error::Success ? fmt::format("{} {}", res->status, res->body)
														   : std::to_string((int)res.error()));
				m_bSuccessfullyConnected = false;
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;
			}
			m_bAuthenticatingWithGameServer = false;
			m_bScriptAuthenticatingWithGameServer = false;
		});

	requestThread.detach();
}

void MasterServerManager::WritePlayerPersistentData(const char* playerId, const char* pdata, size_t pdataSize)
{
	// still call this if we don't have a server id, since lobbies that aren't port forwarded need to be able to call it
	m_bSavingPersistentData = true;
	if (!pdataSize)
	{
		spdlog::warn("attempted to write pdata of size 0!");
		m_bSavingPersistentData = false;
		return;
	}

	std::string strPlayerId(playerId);
	std::vector<BYTE> strPdata(pdataSize);
	memcpy(strPdata.data(), pdata, pdataSize);

	std::thread requestThread(
		[this, strPlayerId, strPdata, pdataSize]
		{
			spdlog::info("[Pdata] Writing persistence for user: {}", strPlayerId);
			httplib::Client cli = SetupHttpClient();
			std::string querystring = fmt::format("/accounts/write_persistence?id={}&serverId={}", strPlayerId, m_sOwnServerId);
			std::string encoded = base64_encode(strPdata.data(), pdataSize);
			if (auto res = cli.Post(querystring, encoded, "text/plain"))
			{
				spdlog::error("[Pdata] Status: {}", res->status);
				spdlog::error("[Pdata] Body: {}", res->body);
				m_bSuccessfullyConnected = true;
			}
			else
			{
				auto err = res.error();
				spdlog::error("[Pdata] Write persistence failed: {}", err);

				m_bSuccessfullyConnected = false;
			}

			m_bSavingPersistentData = false;
		});

	requestThread.detach();
}

void ConCommand_ns_fetchservers(const CCommand& args)
{
	g_pMasterServerManager->RequestServerList();
}

MasterServerManager::MasterServerManager() : m_pendingConnectionInfo {}, m_sOwnServerId {""}, m_sOwnClientAuthToken {""} {}

ON_DLL_LOAD_RELIESON("engine.dll", MasterServer, (ConCommand, ServerPresence), (CModule module))
{
	g_pMasterServerManager = new MasterServerManager;
	Cvar_ns_server_reg_token = new ConVar("ns_server_reg_token", "0", FCVAR_GAMEDLL, "Server account string used for registeration");
	Cvar_ns_masterserver_hostname = new ConVar("ns_masterserver_hostname", "127.0.0.1", FCVAR_NONE, "");
	Cvar_ns_curl_log_enable = new ConVar("ns_curl_log_enable", "0", FCVAR_NONE, "Whether curl should log to the console");

	RegisterConCommand("ns_fetchservers", ConCommand_ns_fetchservers, "Fetch all servers from the masterserver", FCVAR_CLIENTDLL);

	MasterServerPresenceReporter* presenceReporter = new MasterServerPresenceReporter;
	g_pServerPresence->AddPresenceReporter(presenceReporter);
}

void MasterServerPresenceReporter::CreatePresence(const ServerPresence* pServerPresence)
{
	m_nNumRegistrationAttempts = 0;
}

void MasterServerPresenceReporter::ReportPresence(const ServerPresence* pServerPresence)
{
	// make a copy of presence for multithreading purposes
	ServerPresence threadedPresence(pServerPresence);

	if (!*g_pMasterServerManager->m_sOwnServerId)
	{
		// Don't try if we've reached the max registration attempts.
		// In the future, we should probably allow servers to re-authenticate after a while if the MS was down.
		if (m_nNumRegistrationAttempts >= MAX_REGISTRATION_ATTEMPTS)
		{
			return;
		}

		// Make sure to wait til the cooldown is over for DUPLICATE_SERVER failures.
		if (Tier0::Plat_FloatTime() < m_fNextAddServerAttemptTime)
		{
			return;
		}

		// If we're not running any InternalAddServer() attempt in the background.
		if (!addServerFuture.valid())
		{
			// Launch an attempt to add the local server to the master server.
			InternalAddServer(pServerPresence);
		}
	}
	else
	{
		// If we're not running any InternalUpdateServer() attempt in the background.
		if (!updateServerFuture.valid())
		{
			// Launch an attempt to update the local server on the master server.
			InternalUpdateServer(pServerPresence);
		}
	}
}

void MasterServerPresenceReporter::DestroyPresence(const ServerPresence* pServerPresence)
{
	// Don't call this if we don't have a server id.
	if (!*g_pMasterServerManager->m_sOwnServerId)
	{
		return;
	}

	// Not bothering with better thread safety in this case since DestroyPresence() is called when the game is shutting down.
	*g_pMasterServerManager->m_sOwnServerId = 0;

	std::thread requestThread(
		[this]
		{
			httplib::Client cli = SetupHttpClient();
			std::string querystring = fmt::format(
				"/server/remove_server?id={}?serverAuthToken={}",
				g_pMasterServerManager->m_sOwnServerId,
				g_pMasterServerManager->m_sOwnServerAuthToken);
			cli.Delete(querystring);
		});

	requestThread.detach();
}

void MasterServerPresenceReporter::RunFrame(double flCurrentTime, const ServerPresence* pServerPresence)
{
	// Check if we're already running an InternalAddServer() call in the background.
	// If so, grab the result if it's ready.
	if (addServerFuture.valid())
	{
		std::future_status status = addServerFuture.wait_for(0ms);
		if (status != std::future_status::ready)
		{
			// Still running, no need to do anything.
			return;
		}

		// Check the result.
		auto resultData = addServerFuture.get();

		g_pMasterServerManager->m_bSuccessfullyConnected = resultData.result != MasterServerReportPresenceResult::FailedNoConnect;

		switch (resultData.result)
		{
		case MasterServerReportPresenceResult::Success:
			// Copy over the server id and auth token granted by the MS.
			strncpy_s(
				g_pMasterServerManager->m_sOwnServerId,
				sizeof(g_pMasterServerManager->m_sOwnServerId),
				resultData.id.value().c_str(),
				sizeof(g_pMasterServerManager->m_sOwnServerId) - 1);
			strncpy_s(
				g_pMasterServerManager->m_sOwnServerAuthToken,
				sizeof(g_pMasterServerManager->m_sOwnServerAuthToken),
				resultData.serverAuthToken.value().c_str(),
				sizeof(g_pMasterServerManager->m_sOwnServerAuthToken) - 1);
			break;
		case MasterServerReportPresenceResult::FailedNoRetry:
		case MasterServerReportPresenceResult::FailedNoConnect:
			// If we failed to connect to the master server, or failed with no retry, stop trying.
			m_nNumRegistrationAttempts = MAX_REGISTRATION_ATTEMPTS;
			break;
		case MasterServerReportPresenceResult::Failed:
			++m_nNumRegistrationAttempts;
			break;
		case MasterServerReportPresenceResult::FailedDuplicateServer:
			++m_nNumRegistrationAttempts;
			// Wait at least twenty seconds until we re-attempt to add the server.
			m_fNextAddServerAttemptTime = Tier0::Plat_FloatTime() + 20.0f;
			break;
		}

		if (m_nNumRegistrationAttempts >= MAX_REGISTRATION_ATTEMPTS)
		{
			spdlog::error("Reached max ms server registration attempts.");
		}
	}
	else if (updateServerFuture.valid())
	{
		// Check if the InternalUpdateServer() call completed.
		std::future_status status = updateServerFuture.wait_for(0ms);

		if (status != std::future_status::ready)
		{
			// Still running, no need to do anything.
			return;
		}

		auto resultData = updateServerFuture.get();
		if (resultData.result == MasterServerReportPresenceResult::Success)
		{
			if (resultData.id)
			{
				strncpy_s(
					g_pMasterServerManager->m_sOwnServerId,
					sizeof(g_pMasterServerManager->m_sOwnServerId),
					resultData.id.value().c_str(),
					sizeof(g_pMasterServerManager->m_sOwnServerId) - 1);
			}

			if (resultData.serverAuthToken)
			{
				strncpy_s(
					g_pMasterServerManager->m_sOwnServerAuthToken,
					sizeof(g_pMasterServerManager->m_sOwnServerAuthToken),
					resultData.serverAuthToken.value().c_str(),
					sizeof(g_pMasterServerManager->m_sOwnServerAuthToken) - 1);
			}
		}
	}
}

void MasterServerPresenceReporter::InternalAddServer(const ServerPresence* pServerPresence)
{
	const ServerPresence threadedPresence(pServerPresence);
	// Never call this with an ongoing InternalAddServer() call.
	assert(!addServerFuture.valid());

	g_pMasterServerManager->m_sOwnServerId[0] = 0;
	g_pMasterServerManager->m_sOwnServerAuthToken[0] = 0;

	std::string modInfo = g_pMasterServerManager->m_sOwnModInfoJson;
	std::string hostname = Cvar_ns_masterserver_hostname->GetString();
	std::string serverAccount = Cvar_ns_server_reg_token->GetString();

	spdlog::info("Attempting to register the local server to the master server.");

	addServerFuture = std::async(
		std::launch::async,
		[threadedPresence, modInfo, hostname, serverAccount]
		{
			httplib::Client cli = SetupHttpClient();
			std::string querystring = fmt::format(
				"/server/"
				"add_server?port={}&authPort={}&name={}&description={}&map={}&playlist={}&maxPlayers={}&password={}&serverRegToken="
				"{}",
				threadedPresence.m_iPort,
				threadedPresence.m_iAuthPort,
				threadedPresence.m_sServerName,
				threadedPresence.m_sServerDesc,
				threadedPresence.m_MapName,
				threadedPresence.m_PlaylistName,
				threadedPresence.m_iMaxPlayers,
				threadedPresence.m_Password,
				serverAccount);

			// Lambda to quickly cleanup resources and return a value.
			auto ReturnCleanup = [](MasterServerReportPresenceResult result, std::string id = "", std::string serverAuthToken = "")
			{
				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;
				data.id = id;
				data.serverAuthToken = serverAuthToken;
				return data;
			};
			spdlog::info("{}", modInfo);
			auto res = cli.Post(querystring, modInfo, "application/json");

			if (res && res->status == 200)
			{
				try
				{
					nlohmann::json serverAddedJson = nlohmann::json::parse(res->body);
					if (serverAddedJson["success"])
					{
						spdlog::info("Successfully registered the local server to the master server.");
						return ReturnCleanup(
							MasterServerReportPresenceResult::Success, serverAddedJson.at("id"), serverAddedJson.at("serverAuthToken"));
					}
					else
					{
						if (!strcmp(std::string(serverAddedJson.at("error").at("enum")).c_str(), "DUPLICATE_SERVER"))
						{
							spdlog::error("Cooling down while the master server cleans the dead server entry, if any.");
							return ReturnCleanup(MasterServerReportPresenceResult::FailedDuplicateServer);
						}
						return ReturnCleanup(MasterServerReportPresenceResult::Failed);
					}
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed registering server: encountered parse error \"{}\"", e.what());
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed registering server: encountered data error \"{}\"", e.what());
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}
			}
			else
			{
				spdlog::error(
					"Failed adding self to server list: error {}",
					res.error() == httplib::Error::Success ? fmt::format("{} {}", res->status, res->body)
														   : std::to_string((int)res.error()));
				return ReturnCleanup(MasterServerReportPresenceResult::FailedNoConnect);
			}
		});
}

void MasterServerPresenceReporter::InternalUpdateServer(const ServerPresence* pServerPresence)
{
	const ServerPresence threadedPresence(pServerPresence);

	// Never call this with an ongoing InternalUpdateServer() call.
	assert(!updateServerFuture.valid());

	const std::string serverId = g_pMasterServerManager->m_sOwnServerId;
	const std::string hostname = Cvar_ns_masterserver_hostname->GetString();
	const std::string modinfo = g_pMasterServerManager->m_sOwnModInfoJson;
	const std::string serverAccount = Cvar_ns_server_reg_token->GetString();

	updateServerFuture = std::async(
		std::launch::async,
		[threadedPresence, serverId, hostname, modinfo, serverAccount]
		{
			// Lambda to quickly cleanup resources and return a value.
			auto ReturnCleanup = [](MasterServerReportPresenceResult result, std::string id = "", std::string serverAuthToken = "")
			{
				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;

				if (!id.empty())
				{
					data.id = id;
				}

				if (!serverAuthToken.empty())
				{
					data.serverAuthToken = serverAuthToken;
				}

				return data;
			};

			httplib::Client cli = SetupHttpClient();
			std::string querystring = fmt::format(
				"/server/"
				"update_values?id={}&port={}&authPort={}&name={}&description={}&map={}&playlist={}&playerCount={}&"
				"maxPlayers={}&password={}&gameState={}&serverAuthToken={}",
				serverId.c_str(),
				threadedPresence.m_iPort,
				threadedPresence.m_iAuthPort,
				threadedPresence.m_sServerName,
				threadedPresence.m_sServerDesc,
				threadedPresence.m_MapName,
				threadedPresence.m_PlaylistName,
				threadedPresence.m_iPlayerCount,
				threadedPresence.m_iMaxPlayers,
				threadedPresence.m_Password,
				std::to_string(g_pSQGameState->eGameState),
				g_pMasterServerManager->m_sOwnServerAuthToken);
			auto res = cli.Post(querystring, modinfo, "application/json");
			std::string updatedId;
			std::string updatedAuthToken;
			if (res && res->status == 200)
			{
				return ReturnCleanup(MasterServerReportPresenceResult::Success);
			}
			else
			{
				spdlog::error(
					"Failed during heartbeat request: {}",
					res.error() == httplib::Error::Success ? fmt::format("{} {}", res->status, res->body)
														   : std::to_string((int)res.error()));
				return ReturnCleanup(MasterServerReportPresenceResult::Failed);
			}
		});
}
