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
inline std::string encode_query_param(const std::string& value)
{
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;

	for (const auto c : value)
	{
		if (std::isalnum(static_cast<uint8_t>(c)) || c == '-' || c == '.' || c == '_' || c == '~')
		{
			escaped << c;
		}
		else
		{
			escaped << std::uppercase;
			escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
			escaped << std::nouppercase;
		}
	}

	return escaped.str();
}
void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, Cvar_ns_curl_log_enable->GetBool());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, &NSUserAgent);
	if (!strstr(GetCommandLineA(), "-disabledoh"))
	{
		std::string masterserver_hostname = Cvar_ns_masterserver_hostname->GetString();
		const std::string doh_result = g_DohWorker->GetDOHResolve(masterserver_hostname);
		if (g_DohWorker->m_bDohAvailable)
		{
			struct curl_slist* host = nullptr;
			masterserver_hostname.erase(0, 8);
			const std::string addr = masterserver_hostname + ":443:" + doh_result;
			// spdlog::info(addr);
			host = curl_slist_append(nullptr, addr.c_str());
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
	std::string ms_addr = Cvar_ns_masterserver_hostname->GetString();
	httplib::Client cli(ms_addr);
	//, {"Accept-Encoding", "gzip"}, {"Content-Encoding", "gzip"}
	cli.set_default_headers({{"User-Agent", NSUserAgent}});
	cli.set_compress(true);
	cli.set_decompress(true);
	cli.set_read_timeout(10, 0);
	cli.set_write_timeout(10, 0);
	if (!strstr(GetCommandLineA(), "-disabledoh"))
	{
		std::string doh_result = g_DohWorker->GetDOHResolve(ms_addr);
		if (g_DohWorker->m_bDohAvailable)
		{
			cli.set_hostname_addr_map({{ms_addr, doh_result}});
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
	static_cast<std::string*>(userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}
bool MasterServerManager::StartMatchmaking(MatchmakeInfo* status)
{
	// no need for a request thread here cuz we always call this function in a new thread.
	// ps,aitdm,ttdm
	CURL* curl = curl_easy_init();
	SetCommonHttpClientOptions(curl);
	std::string read_buffer;
	const std::string token = m_sOwnClientAuthToken;
	const std::string local_uid = R2::g_pLocalPlayerUserID;
	char* local_uid_escaped = curl_easy_escape(curl, local_uid.c_str(), local_uid.length());
	char* token_escaped = curl_easy_escape(curl, token.c_str(), token.length());
	std::string query_fmt_str = "{}/matchmaking/join?id={}&token={}";
	for (int i = 0; i < status->playlistList.size(); i++)
	{
		query_fmt_str.append(fmt::format("&playlistList[{}]={}", i, status->playlistList[i]));
	}
	spdlog::warn("{}", query_fmt_str);
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format(query_fmt_str, Cvar_ns_masterserver_hostname->GetString(), local_uid_escaped, token_escaped).c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

	const CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document server_response;
		server_response.Parse(read_buffer.c_str());

		if (server_response.HasParseError())
		{
			// spdlog::error(
			//"Failed reading player clantag: encountered parse error \"{}\"",
			// rapidjson::GetParseError_En(serverresponse.GetParseError()));
			goto REQUEST_END_CLEANUP;
		}

		if (!server_response.IsObject() || !server_response.HasMember("success"))
		{
			// spdlog::error("Failed reading origin auth info response: malformed response object {}", readBuffer);
			goto REQUEST_END_CLEANUP;
		}

		if (server_response["success"].IsTrue())
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
	const std::string token = m_sOwnClientAuthToken;
	char* token_escaped = curl_easy_escape(curl, token.c_str(), token.length());
	const std::string local_uid = R2::g_pLocalPlayerUserID;
	char* local_uid_escaped = curl_easy_escape(curl, local_uid.c_str(), local_uid.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format("{}/matchmaking/quit?id={}&token={}", Cvar_ns_masterserver_hostname->GetString(), local_uid_escaped, token_escaped)
			.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	const CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document server_response;
		server_response.Parse(readBuffer.c_str());

		if (server_response.HasParseError())
		{
			// spdlog::error(
			//"Failed reading player clantag: encountered parse error \"{}\"",
			// rapidjson::GetParseError_En(serverresponse.GetParseError()));
			goto REQUEST_END_CLEANUP;
		}

		if (!server_response.IsObject() || !server_response.HasMember("success"))
		{
			// spdlog::error("Failed reading origin auth info response: malformed response object {}", readBuffer);
			goto REQUEST_END_CLEANUP;
		}

		if (server_response["success"].IsTrue())
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
	std::string read_buffer;
	const std::string token = m_sOwnClientAuthToken;
	const std::string local_uid = R2::g_pLocalPlayerUserID;
	char* local_uid_escaped = curl_easy_escape(curl, local_uid.c_str(), local_uid.length());
	char* token_escaped = curl_easy_escape(curl, token.c_str(), token.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format("{}/matchmaking/state?id={}&token={}", Cvar_ns_masterserver_hostname->GetString(), local_uid_escaped, token_escaped)
			.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);

	const CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		rapidjson_document server_response;
		server_response.Parse(read_buffer.c_str());

		if (server_response.HasParseError())
		{

			goto REQUEST_END_CLEANUP;
		}

		if (!server_response.IsObject() || !server_response.HasMember("success") && server_response.HasMember("state") &&
											   server_response["state"].HasMember("type") && server_response["success"].IsTrue())
		{

			goto REQUEST_END_CLEANUP;
		}
		// spdlog::info("{}", serverresponse["state"]["type"].GetString());
		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_QUEUED"))
		{
			if (!server_response["state"].HasMember("eta_seconds") || !server_response["state"]["eta_seconds"].IsNumber())
				goto REQUEST_END_CLEANUP;
			status->etaSeconds = std::to_string(server_response["info"]["etaSeconds"].GetInt());
			status->status = server_response["state"]["type"].GetString();
			spdlog::info("[Matchmaker] MATCHMAKING_QUEUED");
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_CONFIRMING"))
		{
			if (!server_response["state"].HasMember("timeout") || !server_response["state"]["timeout"].IsNumber() ||
				!server_response["state"].HasMember("playlist_name") || !server_response["state"]["playlist_name"].IsString())
				goto REQUEST_END_CLEANUP;
			status->timeout = std::to_string(server_response["info"]["timeout"].GetInt());
			status->playlistName = server_response["info"]["playlist_name"].GetString();
			status->status = server_response["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_CONFIRMED"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_CONFIRMED");
			status->status = server_response["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_ALLOCATING"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_ALLOCATING");
			status->status = server_response["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_CONNECTING"))
		{
			if (!server_response["state"].HasMember("ip") || !server_response["state"]["ip"].IsString() ||
				!server_response["state"].HasMember("auth_token") || !server_response["state"]["auth_token"].IsString() ||
				!server_response["state"].HasMember("port") || !server_response["state"]["port"].IsNumber())
				goto REQUEST_END_CLEANUP;

			status->etaSeconds = std::to_string(server_response["state"]["etaSeconds"].GetInt());
			auto* connection_info = new MatchmakeConnectionInfo;
			connection_info->ip.S_un.S_addr = inet_addr(server_response["state"]["ip"].GetString());
			connection_info->port = static_cast<unsigned short>(server_response["state"]["port"].GetUint());

			strncpy_s(
				connection_info->authToken,
				sizeof(m_pendingConnectionInfo.authToken),
				server_response["state"]["authToken"].GetString(),
				sizeof(m_pendingConnectionInfo.authToken) - 1);

			status->connectionInfo = connection_info;
			status->serverReady = true;
			status->status = server_response["state"]["type"].GetString();
			curl_easy_cleanup(curl);
			return true;
		}

		if (!strcmp(server_response["state"]["type"].GetString(), "#MATCHMAKING_NOTHING"))
		{
			spdlog::info("[Matchmaker] TODO: MATCHMAKING_NOTHING");
			status->status = server_response["state"]["type"].GetString();
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

bool MasterServerManager::SetLocalPlayerClanTag(const std::string clantag)
{

	httplib::Client cli = SetupHttpClient();
	const std::string querystring = fmt::format(
		"/client/clantag?clantag={}&id={}&token={}",
		encode_query_param(clantag),
		encode_query_param(R2::g_pLocalPlayerUserID),
		encode_query_param(m_sOwnClientAuthToken));
	auto res = cli.Post(querystring);
	if (res && res->status == 200)
	{
		try
		{
			nlohmann::json setclantag_json = nlohmann::json::parse(res->body);
			if (setclantag_json.at("success"))
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
	std::string uid_str(uid);
	std::string token_str(originToken);

	std::thread request_thread(
		[this, uid_str, token_str]()
		{
			spdlog::info("Trying to authenticate with northstar masterserver for user {}", uid_str);

			httplib::Client cli = SetupHttpClient();
			const std::string endpoint =
				fmt::format("/client/origin_auth?id={}&token={}", encode_query_param(uid_str), encode_query_param(token_str));

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
				const auto err = res.error();
				m_sAuthFailureReason = std::string("ERROR_NO_CONNECTION");
				m_sAuthFailureMessage = fmt::format("与主服务器进行初始化通信时出现错误：{}", httplib::to_string(err));
				spdlog::error("Failed performing northstar origin auth: {}", httplib::to_string(err));
				m_bSuccessfullyConnected = false;
			}

			m_bOriginAuthWithMasterServerInProgress = false;
			m_bOriginAuthWithMasterServerDone = true;
		});

	request_thread.detach();
}

void MasterServerManager::RequestServerList()
{
	// do this here so it's instantly set on call for scripts
	m_bScriptRequestingServerList = true;

	std::thread request_thread(
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
					nlohmann::json server_list_json = nlohmann::json::parse(res->body);

					m_vRemoteServers.clear();

					for (auto& server_obj : server_list_json)
					{
						RemoteServerInfo server;

						server.id = server_obj.at("id");
						server.name = server_obj.at("name");
						server.description = server_obj.at("description");
						server.map = server_obj.at("map");
						server.playlist = server_obj.at("playlist");
						server.gameState = server_obj.at("gameState");
						server.playerCount = server_obj.at("playerCount");
						server.maxPlayers = server_obj.at("maxPlayers");
						server.requiresPassword = server_obj.at("hasPassword");

						for (auto& mod : server_obj.at("modInfo").at("Mods"))
						{
							if (mod.at("RequiredOnClient"))
							{
								RemoteModInfo mod_info;

								mod_info.Name = mod.at("Name");
								mod_info.Version = mod.at("Version");
								server.requiredMods.push_back(mod_info);
							}
						}

						m_vRemoteServers.push_back(server);
					}

					std::ranges::sort(
						m_vRemoteServers.begin(),
						m_vRemoteServers.end(),
						[](const RemoteServerInfo& a, const RemoteServerInfo& b) { return a.playerCount > b.playerCount; });
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

	request_thread.detach();
}

void MasterServerManager::RequestMainMenuPromos()
{
	m_bHasMainMenuPromoData = false;

	std::thread request_thread(
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
				auto const err = res.error();
				spdlog::error("Failed reading masterserver main menu promos response:: {}", httplib::to_string(err));
				m_bSuccessfullyConnected = false;
			}
		});

	request_thread.detach();
}

void MasterServerManager::AuthenticateWithOwnServer(const char* uid, const std::string& playerToken)
{
	// don't wait, just stop if we're trying to do 2 auth requests at once
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
			const std::string querystring =
				fmt::format("/client/auth_with_self?id={}&playerToken={}", encode_query_param(uidStr), encode_query_param(tokenStr));
			httplib::Client cli = SetupHttpClient();
			auto res = cli.Post(querystring);
			if (res && res->status == 200)
			{
				m_bSuccessfullyConnected = true;
				try
				{
					// spdlog::info("{}", res->status);
					nlohmann::json authInfoJson = nlohmann::json::parse(res->body);
					RemoteAuthData newAuthData;

					newAuthData.uid = authInfoJson.at("id");

					const std::string original = authInfoJson.at("persistentData");
					newAuthData.pdata = base64_decode(original);

					std::lock_guard<std::mutex> guard(g_pServerAuthentication->m_AuthDataMutex);
					g_pServerAuthentication->m_RemoteAuthenticationData.clear();
					g_pServerAuthentication->m_RemoteAuthenticationData.insert(std::make_pair(authInfoJson.at("authToken"), newAuthData));

					m_bSuccessfullyAuthenticatedWithGameServer = true;
					m_bAuthenticatingWithGameServer = false;
					m_bScriptAuthenticatingWithGameServer = false;
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed authenticating with local server: encountered parse error \"{}\"", e.what());
					m_bSuccessfullyAuthenticatedWithGameServer = false;
					m_bScriptAuthenticatingWithGameServer = false;
					m_bAuthenticatingWithGameServer = false;
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed authenticating with local server: encountered data error \"{}\"", e.what());
					m_bSuccessfullyAuthenticatedWithGameServer = false;
					m_bScriptAuthenticatingWithGameServer = false;
					m_bAuthenticatingWithGameServer = false;
				}
			}
			else
			{
				auto err = res.error();
				spdlog::error("Failed authenticating with local server: encountered connection error {}", err);
				m_bSuccessfullyConnected = false;
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;
			}

			m_bAuthenticatingWithGameServer = false;
			m_bScriptAuthenticatingWithGameServer = false;

			if (m_bNewgameAfterSelfAuth)
			{
				// pretty sure this is threadsafe?
				R2::Cbuf_AddText(R2::Cbuf_GetCurrentPlayer(), "ns_end_reauth_and_leave_to_lobby", R2::cmd_source_t::kCommandSrcCode);
				m_bNewgameAfterSelfAuth = false;
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

	std::string uid_str(uid);
	const std::string& token_str(playerToken);
	const std::string& server_id_str(serverId);
	std::string password_str(password);

	std::thread requestThread(
		[this, uid_str, token_str, server_id_str, password_str]()
		{
			// ensure that any persistence saving is done, so we know masterserver has newest
			while (m_sPlayerPersistenceStates.contains(uid_str))
				Sleep(100);

			spdlog::info("Attempting authentication with server of id \"{}\"", server_id_str);

			httplib::Client cli = SetupHttpClient();
			const std::string querystring = fmt::format(
				"/client/auth_with_server?id={}&playerToken={}&server={}&password={}",
				encode_query_param(uid_str),
				encode_query_param(token_str),
				encode_query_param(server_id_str),
				encode_query_param(password_str));
			auto res = cli.Post(querystring);

			if (res && res->status == 200)
			{
				m_bSuccessfullyConnected = true;
				try
				{
					nlohmann::json connection_info_json = nlohmann::json::parse(res->body);
					if (connection_info_json.at("success") == true)
					{
						m_pendingConnectionInfo.ip.S_un.S_addr = inet_addr(std::string(connection_info_json.at("ip")).c_str());
						m_pendingConnectionInfo.port = static_cast<unsigned short>(connection_info_json.at("port"));
						m_pendingConnectionInfo.authToken = connection_info_json.at("authToken");
						m_bHasPendingConnectionInfo = true;
						m_bSuccessfullyAuthenticatedWithGameServer = true;
						m_bScriptAuthenticatingWithGameServer = false;
						m_bAuthenticatingWithGameServer = false;
					}
					else
					{
						m_sAuthFailureReason = connection_info_json.at("error").at("enum");
						m_sAuthFailureMessage = connection_info_json.at("error").at("msg");
						m_bSuccessfullyAuthenticatedWithGameServer = false;
						m_bScriptAuthenticatingWithGameServer = false;
						m_bAuthenticatingWithGameServer = false;
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
														   : std::to_string(static_cast<int>(res.error())));
				m_bSuccessfullyConnected = false;
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;
				m_bAuthenticatingWithGameServer = false;
			}
		});

	requestThread.detach();
}

void MasterServerManager::WritePlayerPersistentData(const char* player_id, const char* pdata, size_t pdata_size)
{
	std::string strPlayerId(player_id);
	// still call this if we don't have a server id, since lobbies that aren't port forwarded need to be able to call it
	if (m_sPlayerPersistenceStates.contains(strPlayerId))
	{
		spdlog::warn("player {} attempted to write pdata while previous request still exists!");
		// player is already requesting for leave, ignore the request.
		return;
	}
	if (!pdata_size)
	{
		spdlog::warn("attempted to write pdata of size 0!");
		return;
	}

	m_PlayerPersistenceMutex.lock();
	m_sPlayerPersistenceStates.insert(strPlayerId);
	m_PlayerPersistenceMutex.unlock();

	std::vector<BYTE> str_pdata(pdata_size);
	memcpy(str_pdata.data(), pdata, pdata_size);

	std::thread request_thread(
		[this, strPlayerId, str_pdata, pdata_size]
		{
			spdlog::info("[Pdata] Writing persistence for user: {}", strPlayerId);
			httplib::Client cli = SetupHttpClient();
			const std::string querystring = fmt::format(
				"/accounts/write_persistence?id={}&serverId={}", encode_query_param(strPlayerId), encode_query_param(m_sOwnServerId));
			const std::string encoded = base64_encode(str_pdata.data(), pdata_size);
			auto res = cli.Post(querystring, encoded, "text/plain");
			if (res && res->status == 200)
			{
				spdlog::info("[Pdata] Successfully wrote pdata for user: {}", strPlayerId);
				m_bSuccessfullyConnected = true;
			}
			else
			{
				auto err = res.error();
				spdlog::error("[Pdata] Write persistence failed for user: {}, error: {}", strPlayerId, err);

				m_bSuccessfullyConnected = false;
			}

			m_PlayerPersistenceMutex.lock();
			m_sPlayerPersistenceStates.erase(strPlayerId);
			m_PlayerPersistenceMutex.unlock();
		});

	request_thread.detach();
}

void ConCommand_ns_fetchservers(const CCommand& args)
{
	g_pMasterServerManager->RequestServerList();
}

MasterServerManager::MasterServerManager() : m_sOwnServerId {""}, m_sOwnClientAuthToken {""}, m_pendingConnectionInfo {} {}

ON_DLL_LOAD_RELIESON("engine.dll", MasterServer, (ConCommand, ServerPresence), (CModule module))
{
	g_pMasterServerManager = new MasterServerManager;
	Cvar_ns_server_reg_token = new ConVar("ns_server_reg_token", "0", FCVAR_GAMEDLL, "Server account string used for registration");
	Cvar_ns_masterserver_hostname = new ConVar("ns_masterserver_hostname", "127.0.0.1", FCVAR_NONE, "");
	Cvar_ns_curl_log_enable = new ConVar("ns_curl_log_enable", "0", FCVAR_NONE, "Whether curl should log to the console");

	RegisterConCommand("ns_fetchservers", ConCommand_ns_fetchservers, "Fetch all servers from the masterserver", FCVAR_CLIENTDLL);

	auto* presence_reporter = new MasterServerPresenceReporter;
	g_pServerPresence->AddPresenceReporter(presence_reporter);
}

void MasterServerPresenceReporter::CreatePresence(const ServerPresence* pServerPresence)
{
	m_nNumRegistrationAttempts = 0;
}

void MasterServerPresenceReporter::ReportPresence(const ServerPresence* pServerPresence)
{
	// make a copy of presence for multi threading purposes
	ServerPresence threaded_presence(pServerPresence);

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

	std::thread request_thread(
		[this]
		{
			httplib::Client cli = SetupHttpClient();
			const std::string querystring = fmt::format(
				"/server/remove_server?id={}?serverAuthToken={}",
				encode_query_param(g_pMasterServerManager->m_sOwnServerId),
				encode_query_param(g_pMasterServerManager->m_sOwnServerAuthToken));
			cli.Delete(querystring);
		});

	request_thread.detach();
}

void MasterServerPresenceReporter::RunFrame(double flCurrentTime, const ServerPresence* pServerPresence)
{
	// Check if we're already running an InternalAddServer() call in the background.
	// If so, grab the result if it's ready.
	if (addServerFuture.valid())
	{
		const std::future_status status = addServerFuture.wait_for(0ms);
		if (status != std::future_status::ready)
		{
			// Still running, no need to do anything.
			return;
		}

		// Check the result.
		const auto result_data = addServerFuture.get();

		g_pMasterServerManager->m_bSuccessfullyConnected = result_data.result != MasterServerReportPresenceResult::FailedNoConnect;

		switch (result_data.result)
		{
		case MasterServerReportPresenceResult::Success:
			// Copy over the server id and auth token granted by the MS.
			strncpy_s(
				g_pMasterServerManager->m_sOwnServerId,
				sizeof(g_pMasterServerManager->m_sOwnServerId),
				result_data.id.value().c_str(),
				sizeof(g_pMasterServerManager->m_sOwnServerId) - 1);
			strncpy_s(
				g_pMasterServerManager->m_sOwnServerAuthToken,
				sizeof(g_pMasterServerManager->m_sOwnServerAuthToken),
				result_data.serverAuthToken.value().c_str(),
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
		const std::future_status status = updateServerFuture.wait_for(0ms);

		if (status != std::future_status::ready)
		{
			// Still running, no need to do anything.
			return;
		}

		const auto result_data = updateServerFuture.get();
		if (result_data.result == MasterServerReportPresenceResult::Success)
		{
			if (result_data.id)
			{
				strncpy_s(
					g_pMasterServerManager->m_sOwnServerId,
					sizeof(g_pMasterServerManager->m_sOwnServerId),
					result_data.id.value().c_str(),
					sizeof(g_pMasterServerManager->m_sOwnServerId) - 1);
			}

			if (result_data.serverAuthToken)
			{
				strncpy_s(
					g_pMasterServerManager->m_sOwnServerAuthToken,
					sizeof(g_pMasterServerManager->m_sOwnServerAuthToken),
					result_data.serverAuthToken.value().c_str(),
					sizeof(g_pMasterServerManager->m_sOwnServerAuthToken) - 1);
			}
		}
	}
}

void MasterServerPresenceReporter::InternalAddServer(const ServerPresence* pServerPresence)
{
	const ServerPresence threaded_presence(pServerPresence);
	// Never call this with an ongoing InternalAddServer() call.
	assert(!addServerFuture.valid());

	g_pMasterServerManager->m_sOwnServerId[0] = 0;
	g_pMasterServerManager->m_sOwnServerAuthToken[0] = 0;

	std::string mod_info = g_pMasterServerManager->m_sOwnModInfoJson;
	std::string hostname = Cvar_ns_masterserver_hostname->GetString();
	std::string server_account = Cvar_ns_server_reg_token->GetString();

	spdlog::info("Attempting to register the local server to the master server.");

	addServerFuture = std::async(
		std::launch::async,
		[threaded_presence, mod_info, hostname, server_account]
		{
			httplib::Client cli = SetupHttpClient();
			const std::string querystring = fmt::format(
				"/server/"
				"add_server?port={}&authPort={}&name={}&description={}&map={}&playlist={}&maxPlayers={}&password={}&serverRegToken="
				"{}",
				threaded_presence.m_iPort,
				threaded_presence.m_iAuthPort,
				encode_query_param(threaded_presence.m_sServerName),
				encode_query_param(threaded_presence.m_sServerDesc),
				encode_query_param(threaded_presence.m_MapName),
				encode_query_param(threaded_presence.m_PlaylistName),
				threaded_presence.m_iMaxPlayers,
				encode_query_param(threaded_presence.m_Password),
				encode_query_param(server_account));

			// Lambda to quickly cleanup resources and return a value.
			auto return_cleanup =
				[](const MasterServerReportPresenceResult result, const std::string& id = "", const std::string& server_auth_token = "")
			{
				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;
				data.id = id;
				data.serverAuthToken = server_auth_token;
				return data;
			};
			spdlog::info("{}", mod_info);
			auto res = cli.Post(querystring, mod_info, "application/json");

			if (res && res->status == 200)
			{
				try
				{
					nlohmann::json server_added_json = nlohmann::json::parse(res->body);
					if (server_added_json["success"])
					{
						spdlog::info("Successfully registered the local server to the master server.");
						return return_cleanup(
							MasterServerReportPresenceResult::Success, server_added_json.at("id"), server_added_json.at("serverAuthToken"));
					}
					else
					{
						if (!strcmp(std::string(server_added_json.at("error").at("enum")).c_str(), "DUPLICATE_SERVER"))
						{
							spdlog::error("Cooling down while the master server cleans the dead server entry, if any.");
							return return_cleanup(MasterServerReportPresenceResult::FailedDuplicateServer);
						}
						return return_cleanup(MasterServerReportPresenceResult::Failed);
					}
				}
				catch (nlohmann::json::parse_error& e)
				{
					spdlog::error("Failed registering server: encountered parse error \"{}\"", e.what());
					return return_cleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}
				catch (nlohmann::json::out_of_range& e)
				{
					spdlog::error("Failed registering server: encountered data error \"{}\"", e.what());
					return return_cleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}
			}
			else
			{
				spdlog::error(
					"Failed adding self to server list: error {}",
					res.error() == httplib::Error::Success ? fmt::format("{} {}", res->status, res->body)
														   : std::to_string(static_cast<int>(res.error())));
				return return_cleanup(MasterServerReportPresenceResult::FailedNoConnect);
			}
		});
}

void MasterServerPresenceReporter::InternalUpdateServer(const ServerPresence* pServerPresence)
{
	const ServerPresence threaded_presence(pServerPresence);

	// Never call this with an ongoing InternalUpdateServer() call.
	assert(!updateServerFuture.valid());

	const std::string server_id = g_pMasterServerManager->m_sOwnServerId;
	const std::string hostname = Cvar_ns_masterserver_hostname->GetString();
	const std::string mod_info = g_pMasterServerManager->m_sOwnModInfoJson;
	const std::string server_account = Cvar_ns_server_reg_token->GetString();

	updateServerFuture = std::async(
		std::launch::async,
		[threaded_presence, server_id, hostname, mod_info, server_account]
		{
			// Lambda to quickly cleanup resources and return a value.
			auto return_cleanup =
				[](const MasterServerReportPresenceResult result, const std::string& id = "", const std::string& server_auth_token = "")
			{
				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;

				if (!id.empty())
				{
					data.id = id;
				}

				if (!server_auth_token.empty())
				{
					data.serverAuthToken = server_auth_token;
				}

				return data;
			};

			httplib::Client cli = SetupHttpClient();
			const std::string querystring = fmt::format(
				"/server/"
				"update_values?id={}&port={}&authPort={}&name={}&description={}&map={}&playlist={}&playerCount={}&"
				"maxPlayers={}&password={}&gameState={}&serverAuthToken={}",
				server_id.c_str(),
				threaded_presence.m_iPort,
				threaded_presence.m_iAuthPort,
				encode_query_param(threaded_presence.m_sServerName),
				encode_query_param(threaded_presence.m_sServerDesc),
				encode_query_param(threaded_presence.m_MapName),
				encode_query_param(threaded_presence.m_PlaylistName),
				threaded_presence.m_iPlayerCount,
				threaded_presence.m_iMaxPlayers,
				encode_query_param(threaded_presence.m_Password),
				encode_query_param(std::to_string(g_pSQGameState->eGameState)),
				encode_query_param(g_pMasterServerManager->m_sOwnServerAuthToken));
			auto res = cli.Post(querystring, mod_info, "application/json");
			std::string updated_id;
			std::string updated_auth_token;
			if (res && res->status == 200)
			{
				return return_cleanup(MasterServerReportPresenceResult::Success);
			}
			else
			{
				spdlog::error(
					"Failed during heartbeat request: {}",
					res.error() == httplib::Error::Success ? fmt::format("{} {}", res->status, res->body)
														   : std::to_string(static_cast<int>(res.error())));
				return return_cleanup(MasterServerReportPresenceResult::Failed);
			}
		});
}
