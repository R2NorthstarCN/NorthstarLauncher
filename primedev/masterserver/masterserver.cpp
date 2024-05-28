#include "masterserver/masterserver.h"
#include "core/convar/concommand.h"
#include "shared/playlist.h"
#include "server/auth/serverauthentication.h"
#include "core/tier0.h"
#include "core/vanilla.h"
#include "engine/r2engine.h"
#include "mods/modmanager.h"
#include "shared/misccommands.h"
#include "util/utils.h"
#include "util/version.h"
#include "server/auth/bansystem.h"
#include "dedicated/dedicated.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"

#include "curl/curl.h"

#include <cstring>
#include <regex>
#include <random>
#include "masterserver.h"

using namespace std::chrono_literals;

MasterServerManager* g_pMasterServerManager;

ConVar* Cvar_ns_masterserver_hostname;
ConVar* Cvar_ns_curl_log_enable;

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<uint32_t> dis(1, 0xFFFFFFFF);

RemoteServerInfo::RemoteServerInfo(
	const char* newId,
	const char* newName,
	const char* newDescription,
	const char* newMap,
	const char* newPlaylist,
	const char* newRegion,
	int newPlayerCount,
	int newMaxPlayers,
	bool newRequiresPassword)
{
	// passworded servers don't have public ips
	requiresPassword = newRequiresPassword;

	strncpy_s((char*)id, sizeof(id), newId, sizeof(id) - 1);
	strncpy_s((char*)name, sizeof(name), newName, sizeof(name) - 1);

	description = std::string(newDescription);

	strncpy_s((char*)map, sizeof(map), newMap, sizeof(map) - 1);
	strncpy_s((char*)playlist, sizeof(playlist), newPlaylist, sizeof(playlist) - 1);

	strncpy((char*)region, newRegion, sizeof(region));
	region[sizeof(region) - 1] = 0;

	playerCount = newPlayerCount;
	maxPlayers = newMaxPlayers;
}

void SetCommonHttpClientOptions(CURL* curl)
{
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, Cvar_ns_curl_log_enable->GetBool());
	curl_easy_setopt(curl, CURLOPT_USERAGENT, &NSUserAgent);
	// Timeout since the MS has fucky async functions without await, making curl hang due to a successful connection but no response for ~90
	// seconds.
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
	// curl_easy_setopt(curl, CURLOPT_STDERR, stdout);
	if (CommandLine()->FindParm("-msinsecure")) // TODO: this check doesn't seem to work
	{
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	}
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

void MasterServerManager::AuthenticateOriginWithMasterServer(const char* uid, const char* originToken)
{
	if (m_bOriginAuthWithMasterServerInProgress || g_pVanillaCompatibility->GetVanillaCompatibility())
		return;

	// do this here so it's instantly set
	m_bOriginAuthWithMasterServerInProgress = true;
	std::string uidStr(uid);
	std::string tokenStr(originToken);

	m_bOriginAuthWithMasterServerSuccessful = false;
	m_sOriginAuthWithMasterServerErrorCode = "";
	m_sOriginAuthWithMasterServerErrorMessage = "";

	std::thread requestThread(
		[this, uidStr, tokenStr]()
		{
			spdlog::info("Trying to authenticate with northstar masterserver for user {}", uidStr);

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);
			std::string readBuffer;
			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format("{}/client/origin_auth?id={}&token={}", Cvar_ns_masterserver_hostname->GetString(), uidStr, tokenStr).c_str());
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			CURLcode result = curl_easy_perform(curl);
			ScopeGuard cleanup(
				[&]
				{
					m_bOriginAuthWithMasterServerInProgress = false;
					m_bOriginAuthWithMasterServerDone = true;
					curl_easy_cleanup(curl);
				});

			if (result == CURLcode::CURLE_OK)
			{
				m_bSuccessfullyConnected = true;

				try
				{
					nlohmann::json originAuthInfo = nlohmann::json::parse(readBuffer);
					if (originAuthInfo.at("success").template get<bool>())
					{
						strncpy_s(
							m_sOwnClientAuthToken,
							sizeof(m_sOwnClientAuthToken),
							originAuthInfo.at("token").template get<std::string>().c_str(),
							sizeof(m_sOwnClientAuthToken) - 1);
						spdlog::info("Northstar origin authentication completed successfully!");
						m_bOriginAuthWithMasterServerSuccessful = true;
					}
					else
					{
						if (originAuthInfo.contains("error") && originAuthInfo["error"].contains("enum"))
						{
							originAuthInfo["error"]["enum"].get_to(m_sOriginAuthWithMasterServerErrorCode);
						}

						if (originAuthInfo.contains("error") && originAuthInfo["error"].contains("msg"))
						{
							originAuthInfo["error"]["msg"].get_to(m_sOriginAuthWithMasterServerErrorMessage);
						}
					}
				}
				catch (nlohmann::json::exception e)
				{
					spdlog::error("Failed reading origin auth info response: encountered error \"{}\"", e.what());
					return;
				}
			}
			else
			{
				spdlog::error("Failed performing northstar origin auth: error {}", curl_easy_strerror(result));
				m_bSuccessfullyConnected = false;
			}
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

			spdlog::info("Requesting server list from {}", Cvar_ns_masterserver_hostname->GetString());

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(curl, CURLOPT_URL, fmt::format("{}/client/servers", Cvar_ns_masterserver_hostname->GetString()).c_str());
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			CURLcode result = curl_easy_perform(curl);
			ScopeGuard cleanup(
				[&]
				{
					m_bRequestingServerList = false;
					m_bScriptRequestingServerList = false;
					curl_easy_cleanup(curl);
				});

			if (result == CURLcode::CURLE_OK)
			{
				m_bSuccessfullyConnected = true;

				try
				{
					nlohmann::json serverInfo = nlohmann::json::parse(readBuffer);
					if (serverInfo.contains("error"))
					{
						spdlog::error("Failed getting server list from masterserver: error \"{}\"", readBuffer);
						return;
					}
					spdlog::info("Got {} servers", serverInfo.size());

					m_vRemoteServers.clear();

					for (auto& serverObj : serverInfo)
					{
						auto& server = m_vRemoteServers.emplace_back(
							serverObj.at("id").template get<std::string>().c_str(),
							serverObj.at("name").template get<std::string>().c_str(),
							serverObj.at("description").template get<std::string>().c_str(),
							serverObj.at("map").template get<std::string>().c_str(),
							serverObj.at("playlist").template get<std::string>().c_str(),
							serverObj.contains("region") ? serverObj["region"].template get<std::string>().c_str() : "",
							serverObj.at("playerCount").template get<int>(),
							serverObj.at("maxPlayers").template get<int>(),
							serverObj.at("hasPassword").template get<bool>());
						server.requiredMods.clear();
						for (auto& mod : serverObj.at("modInfo").at("Mods"))
						{
							try
							{
								auto modInfo = mod.template get<RemoteModInfo>();
								if (modInfo.RequiredOnClient)
								{
									server.requiredMods.push_back(std::move(modInfo));
								}
							}
							catch (nlohmann::json::exception e)
							{
								spdlog::warn("Malformed required mod: \"{}\"", e.what());
							}
						}
					}

					std::sort(
						m_vRemoteServers.begin(),
						m_vRemoteServers.end(),
						[](RemoteServerInfo& a, RemoteServerInfo& b) { return a.playerCount > b.playerCount; });
				}
				catch (nlohmann::json::exception e)
				{
					spdlog::error("Failed getting server list from masterserver: json error \"{}\"", e.what());
					return;
				}
			}
			else
			{
				spdlog::error("Failed requesting servers: error {}", curl_easy_strerror(result));
				m_bSuccessfullyConnected = false;
			}
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

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(
				curl, CURLOPT_URL, fmt::format("{}/client/mainmenupromos", Cvar_ns_masterserver_hostname->GetString()).c_str());
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			CURLcode result = curl_easy_perform(curl);
			ScopeGuard cleanup([&] { curl_easy_cleanup(curl); });

			if (result == CURLcode::CURLE_OK)
			{
				m_bSuccessfullyConnected = true;

				try
				{
					nlohmann::json::parse(readBuffer).get_to(m_sMainMenuPromoData);
				}
				catch (nlohmann::json::exception e)
				{
					spdlog::error("Failed reading masterserver main menu promos response: encountered error\"{}\"", e.what());
					return;
				}

				m_bHasMainMenuPromoData = true;
			}
			else
			{
				spdlog::error("Failed requesting main menu promos: error {}", curl_easy_strerror(result));
				m_bSuccessfullyConnected = false;
			}
		});

	requestThread.detach();
}

void MasterServerManager::AuthenticateWithOwnServer(const char* uid, const char* playerToken)
{
	// dont wait, just stop if we're trying to do 2 auth requests at once
	if (m_bAuthenticatingWithGameServer || g_pVanillaCompatibility->GetVanillaCompatibility())
		return;

	m_bAuthenticatingWithGameServer = true;
	m_bScriptAuthenticatingWithGameServer = true;
	m_bSuccessfullyAuthenticatedWithGameServer = false;
	m_sAuthFailureReason = "Authentication Failed";

	std::string uidStr(uid);
	std::string tokenStr(playerToken);

	std::thread requestThread(
		[this, uidStr, tokenStr]()
		{
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format("{}/client/auth_with_self?id={}&playerToken={}", Cvar_ns_masterserver_hostname->GetString(), uidStr, tokenStr)
					.c_str());
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			CURLcode result = curl_easy_perform(curl);
			ScopeGuard cleanup(
				[&]
				{
					m_bAuthenticatingWithGameServer = false;
					m_bScriptAuthenticatingWithGameServer = false;

					if (m_bNewgameAfterSelfAuth)
					{
						// pretty sure this is threadsafe?
						Cbuf_AddText(Cbuf_GetCurrentPlayer(), "ns_end_reauth_and_leave_to_lobby", cmd_source_t::kCommandSrcCode);
						m_bNewgameAfterSelfAuth = false;
					}

					curl_easy_cleanup(curl);
				});

			if (result == CURLcode::CURLE_OK)
			{
				m_bSuccessfullyConnected = true;

				rapidjson_document authInfoJson;
				authInfoJson.Parse(readBuffer.c_str());

				if (authInfoJson.HasParseError())
				{
					spdlog::error(
						"Failed reading masterserver authentication response: encountered parse error \"{}\"",
						rapidjson::GetParseError_En(authInfoJson.GetParseError()));
					return;
				}

				if (!authInfoJson.IsObject())
				{
					spdlog::error("Failed reading masterserver authentication response: root object is not an object");
					return;
				}

				if (authInfoJson.HasMember("error"))
				{
					spdlog::error("Failed reading masterserver response: got fastify error response");
					spdlog::error(readBuffer);

					if (authInfoJson["error"].HasMember("msg"))
						m_sAuthFailureReason = authInfoJson["error"]["msg"].GetString();
					else if (authInfoJson["error"].HasMember("enum"))
						m_sAuthFailureReason = authInfoJson["error"]["enum"].GetString();
					else
						m_sAuthFailureReason = "No error message provided";

					return;
				}

				if (!authInfoJson["success"].IsTrue())
				{
					spdlog::error("Authentication with masterserver failed: \"success\" is not true");
					return;
				}

				if (!authInfoJson.HasMember("success") || !authInfoJson.HasMember("id") || !authInfoJson["id"].IsString() ||
					!authInfoJson.HasMember("authToken") || !authInfoJson["authToken"].IsString() ||
					!authInfoJson.HasMember("persistentData") || !authInfoJson["persistentData"].IsArray())
				{
					spdlog::error("Failed reading masterserver authentication response: malformed json object");
					return;
				}

				RemoteAuthData newAuthData {};
				strncpy_s(newAuthData.uid, sizeof(newAuthData.uid), authInfoJson["id"].GetString(), sizeof(newAuthData.uid) - 1);

				newAuthData.pdataSize = authInfoJson["persistentData"].GetArray().Size();
				newAuthData.pdata = new char[newAuthData.pdataSize];
				// memcpy(newAuthData.pdata, authInfoJson["persistentData"].GetString(), newAuthData.pdataSize);

				int i = 0;
				// note: persistentData is a uint8array because i had problems getting strings to behave, it sucks but it's just how it be
				// unfortunately potentially refactor later
				for (auto& byte : authInfoJson["persistentData"].GetArray())
				{
					if (!byte.IsUint() || byte.GetUint() > 255)
					{
						spdlog::error("Failed reading masterserver authentication response: malformed json object");
						return;
					}

					newAuthData.pdata[i++] = static_cast<char>(byte.GetUint());
				}

				std::lock_guard<std::mutex> guard(g_pServerAuthentication->m_AuthDataMutex);
				g_pServerAuthentication->m_RemoteAuthenticationData.clear();
				g_pServerAuthentication->m_RemoteAuthenticationData.insert(
					std::make_pair(authInfoJson["authToken"].GetString(), newAuthData));

				m_bSuccessfullyAuthenticatedWithGameServer = true;
			}
			else
			{
				spdlog::error("Failed authenticating with own server: error {}", curl_easy_strerror(result));
				m_bSuccessfullyConnected = false;
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;
			}
		});

	requestThread.detach();
}

void MasterServerManager::AuthenticateWithServer(const char* uid, const char* playerToken, RemoteServerInfo server, const char* password)
{
	// dont wait, just stop if we're trying to do 2 auth requests at once
	if (m_bAuthenticatingWithGameServer || g_pVanillaCompatibility->GetVanillaCompatibility())
		return;

	m_bAuthenticatingWithGameServer = true;
	m_bScriptAuthenticatingWithGameServer = true;
	m_bSuccessfullyAuthenticatedWithGameServer = false;
	m_sAuthFailureReason = "Authentication Failed";

	std::string uidStr(uid);
	std::string tokenStr(playerToken);
	std::string serverIdStr(server.id);
	std::string passwordStr(password);

	std::thread requestThread(
		[this, uidStr, tokenStr, serverIdStr, passwordStr, server]()
		{
			// esnure that any persistence saving is done, so we know masterserver has newest
			while (m_bSavingPersistentData)
				Sleep(100);

			spdlog::info("Attempting authentication with server of id \"{}\"", serverIdStr);

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			{
				char* escapedPassword = curl_easy_escape(curl, passwordStr.c_str(), (int)passwordStr.length());

				curl_easy_setopt(
					curl,
					CURLOPT_URL,
					fmt::format(
						"{}/client/auth_with_server?id={}&playerToken={}&server={}&password={}",
						Cvar_ns_masterserver_hostname->GetString(),
						uidStr,
						tokenStr,
						serverIdStr,
						escapedPassword)
						.c_str());

				curl_free(escapedPassword);
			}

			CURLcode result = curl_easy_perform(curl);
			ScopeGuard cleanup(
				[&]
				{
					m_bAuthenticatingWithGameServer = false;
					m_bScriptAuthenticatingWithGameServer = false;
					curl_easy_cleanup(curl);
				});

			if (result == CURLcode::CURLE_OK)
			{
				m_bSuccessfullyConnected = true;

				rapidjson_document connectionInfoJson;
				connectionInfoJson.Parse(readBuffer.c_str());

				if (connectionInfoJson.HasParseError())
				{
					spdlog::error(
						"Failed reading masterserver authentication response: encountered parse error \"{}\"",
						rapidjson::GetParseError_En(connectionInfoJson.GetParseError()));
					return;
				}

				if (!connectionInfoJson.IsObject())
				{
					spdlog::error("Failed reading masterserver authentication response: root object is not an object");
					return;
				}

				if (connectionInfoJson.HasMember("error"))
				{
					spdlog::error("Failed reading masterserver response: got fastify error response");
					spdlog::error(readBuffer);

					if (connectionInfoJson["error"].HasMember("msg"))
						m_sAuthFailureReason = connectionInfoJson["error"]["msg"].GetString();
					else if (connectionInfoJson["error"].HasMember("enum"))
						m_sAuthFailureReason = connectionInfoJson["error"]["enum"].GetString();
					else
						m_sAuthFailureReason = "No error message provided";

					return;
				}

				if (!connectionInfoJson["success"].IsTrue())
				{
					spdlog::error("Authentication with masterserver failed: \"success\" is not true");
					return;
				}

				if (!connectionInfoJson.HasMember("success") || !connectionInfoJson.HasMember("ip") ||
					!connectionInfoJson["ip"].IsString() || !connectionInfoJson.HasMember("port") ||
					!connectionInfoJson["port"].IsNumber() || !connectionInfoJson.HasMember("authToken") ||
					!connectionInfoJson["authToken"].IsString())
				{
					spdlog::error("Failed reading masterserver authentication response: malformed json object");
					return;
				}

				m_pendingConnectionInfo.ip.S_un.S_addr = inet_addr(connectionInfoJson["ip"].GetString());
				m_pendingConnectionInfo.port = (unsigned short)connectionInfoJson["port"].GetUint();

				strncpy_s(
					m_pendingConnectionInfo.authToken,
					sizeof(m_pendingConnectionInfo.authToken),
					connectionInfoJson["authToken"].GetString(),
					sizeof(m_pendingConnectionInfo.authToken) - 1);

				m_bHasPendingConnectionInfo = true;
				m_bSuccessfullyAuthenticatedWithGameServer = true;

				m_currentServer = server;
				m_sCurrentServerPassword = passwordStr;
			}
			else
			{
				spdlog::error("Failed authenticating with server: error {}", curl_easy_strerror(result));
				m_bSuccessfullyConnected = false;
				m_bSuccessfullyAuthenticatedWithGameServer = false;
				m_bScriptAuthenticatingWithGameServer = false;
			}
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
		return;
	}

	std::string strPlayerId(playerId);
	std::string strPdata(pdata, pdataSize);

	std::thread requestThread(
		[this, strPlayerId, strPdata, pdataSize]
		{
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format(
					"{}/accounts/write_persistence?id={}&serverId={}",
					Cvar_ns_masterserver_hostname->GetString(),
					strPlayerId,
					m_sOwnServerId)
					.c_str());
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			curl_mime* mime = curl_mime_init(curl);
			curl_mimepart* part = curl_mime_addpart(mime);

			curl_mime_data(part, strPdata.c_str(), pdataSize);
			curl_mime_name(part, "pdata");
			curl_mime_filename(part, "file.pdata");
			curl_mime_type(part, "application/octet-stream");

			curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

			CURLcode result = curl_easy_perform(curl);

			if (result == CURLcode::CURLE_OK)
				m_bSuccessfullyConnected = true;
			else
				m_bSuccessfullyConnected = false;

			curl_easy_cleanup(curl);

			m_bSavingPersistentData = false;
		});

	requestThread.detach();
}

void MasterServerManager::ProcessConnectionlessPacketSigreq1(std::string data)
{
	rapidjson_document obj;
	obj.Parse(data);

	if (obj.HasParseError())
	{
		// note: it's okay to print the data as-is since we've already checked that it actually came from Atlas
		spdlog::error("invalid Atlas connectionless packet request ({}): {}", data, GetParseError_En(obj.GetParseError()));
		return;
	}

	if (!obj.HasMember("type") || !obj["type"].IsString())
	{
		spdlog::error("invalid Atlas connectionless packet request ({}): missing type", data);
		return;
	}

	std::string type = obj["type"].GetString();

	if (type == "connect")
	{
		if (!obj.HasMember("token") || !obj["token"].IsString())
		{
			spdlog::error("failed to handle Atlas connect request: missing or invalid connection token field");
			return;
		}
		std::string token = obj["token"].GetString();

		if (!m_handledServerConnections.contains(token))
			m_handledServerConnections.insert(token);
		else
			return; // already handled

		spdlog::info("handling Atlas connect request {}", data);

		if (!obj.HasMember("uid") || !obj["uid"].IsUint64())
		{
			spdlog::error("failed to handle Atlas connect request {}: missing or invalid uid field", token);
			return;
		}
		uint64_t uid = obj["uid"].GetUint64();

		std::string username;
		if (obj.HasMember("username") && obj["username"].IsString())
			username = obj["username"].GetString();

		std::string reject;
		if (!g_pBanSystem->IsUIDAllowed(uid))
			reject = "Banned from this server.";

		std::string pdata;
		if (reject == "")
		{
			spdlog::info("getting pdata for connection {} (uid={} username={})", token, uid, username);

			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format("{}/server/connect?serverId={}&token={}", Cvar_ns_masterserver_hostname->GetString(), m_sOwnServerId, token)
					.c_str());

			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pdata);

			CURLcode result = curl_easy_perform(curl);
			if (result != CURLcode::CURLE_OK)
			{
				spdlog::error("failed to make Atlas connect pdata request {}: {}", token, curl_easy_strerror(result));
				curl_easy_cleanup(curl);
				return;
			}

			long respStatus = -1;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respStatus);

			curl_easy_cleanup(curl);

			if (respStatus != 200)
			{
				rapidjson_document obj;
				obj.Parse(pdata.c_str());

				if (!obj.HasParseError() && obj.HasMember("error") && obj["error"].IsObject())
					spdlog::error(
						"failed to make Atlas connect pdata request {}: response status {}, error: {} ({})",
						token,
						respStatus,
						((obj["error"].HasMember("enum") && obj["error"]["enum"].IsString()) ? obj["error"]["enum"].GetString() : ""),
						((obj["error"].HasMember("msg") && obj["error"]["msg"].IsString()) ? obj["error"]["msg"].GetString() : ""));
				else
					spdlog::error("failed to make Atlas connect pdata request {}: response status {}", token, respStatus);
				return;
			}

			if (!pdata.length())
			{
				spdlog::error("failed to make Atlas connect pdata request {}: pdata response is empty", token);
				return;
			}

			if (pdata.length() > PERSISTENCE_MAX_SIZE)
			{
				spdlog::error(
					"failed to make Atlas connect pdata request {}: pdata is too large (max={} len={})",
					token,
					PERSISTENCE_MAX_SIZE,
					pdata.length());
				return;
			}
		}

		if (reject == "")
			spdlog::info("accepting connection {} (uid={} username={}) with {} bytes of pdata", token, uid, username, pdata.length());
		else
			spdlog::info("rejecting connection {} (uid={} username={}) with reason \"{}\"", token, uid, username, reject);

		if (reject == "")
			g_pServerAuthentication->AddRemotePlayer(token, uid, username, pdata);

		{
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			char* rejectEnc = curl_easy_escape(curl, reject.c_str(), (int)reject.length());
			if (!rejectEnc)
			{
				spdlog::error("failed to handle Atlas connect request {}: failed to escape reject", token);
				return;
			}
			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format(
					"{}/server/connect?serverId={}&token={}&reject={}",
					Cvar_ns_masterserver_hostname->GetString(),
					m_sOwnServerId,
					token,
					rejectEnc)
					.c_str());
			curl_free(rejectEnc);

			// note: we don't actually have any POST data, so we can't use CURLOPT_POST or the behavior is undefined (e.g., hangs in wine)
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");

			std::string buf;
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

			CURLcode result = curl_easy_perform(curl);
			if (result != CURLcode::CURLE_OK)
			{
				spdlog::error("failed to respond to Atlas connect request {}: {}", token, curl_easy_strerror(result));
				curl_easy_cleanup(curl);
				return;
			}

			long respStatus = -1;
			curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respStatus);

			curl_easy_cleanup(curl);

			if (respStatus != 200)
			{
				rapidjson_document obj;
				obj.Parse(buf.c_str());

				if (!obj.HasParseError() && obj.HasMember("error") && obj["error"].IsObject())
					spdlog::error(
						"failed to respond to Atlas connect request {}: response status {}, error: {} ({})",
						token,
						respStatus,
						((obj["error"].HasMember("enum") && obj["error"]["enum"].IsString()) ? obj["error"]["enum"].GetString() : ""),
						((obj["error"].HasMember("msg") && obj["error"]["msg"].IsString()) ? obj["error"]["msg"].GetString() : ""));
				else
					spdlog::error("failed to respond to Atlas connect request {}: response status {}", token, respStatus);
				return;
			}
		}

		return;
	}

	spdlog::error("invalid Atlas connectionless packet request: unknown type {}", type);
}

void ConCommand_ns_wstest(const CCommand& args)
{
	NOTE_UNUSED(args);
	WebSocketManager::instance();
}

void ConCommand_ns_fetchservers(const CCommand& args)
{
	NOTE_UNUSED(args);
	g_pMasterServerManager->RequestServerList();
}

MasterServerManager::MasterServerManager() : m_pendingConnectionInfo {}, m_sOwnServerId {""}, m_sOwnClientAuthToken {""} {}

WebSocketManager& WebSocketManager::instance()
{
	static WebSocketManager WEBSOCKET_MANAGER;
	return WEBSOCKET_MANAGER;
}

WebSocketManager::WebSocketManager()
{
	spdlog::info("Initializing WebSocket Manager for server authentication");
	EstablishMasterServerConnection();
}

bool WebSocketManager::InitialiseWebSocket()
{
	std::string url = std::string(Cvar_ns_masterserver_hostname->GetString());
	std::string wsurl = fmt::format("{}/v2/server/ws", url.replace(url.find_first_of("http"), 4, "ws")); // http -> ws, https -> wss
	spdlog::info("WS:Initialize on {}", wsurl);

	curl = curl_easy_init();
	CURLcode res;
	SetCommonHttpClientOptions(curl);
	// I don't we need to strip this from main thread because for dedicated servers this step is rather crucial.
	curl_easy_setopt(curl, CURLOPT_URL, wsurl.c_str());
	curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		// websocket establish failed. we should retry as long as the server is still running!
		fprintf(stderr, "WS:Establish failed: %s\n", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		return false;
		// todo: add retry solution
	}
	else
	{
		// connected and ready
		
		return true; // do not clean curl stuff here as we still need to use the socket.
	}

}

void WebSocketManager::SendJsonRequest(nlohmann::json json)
{
	uint32_t msg_id = dis(gen);
	json["metadata"]["id"] = msg_id;
	std::string jsonString = json.dump();
	size_t sent;
	CURLcode result = curl_ws_send(curl, jsonString.c_str(), strlen(jsonString.c_str()), &sent, 0, CURLWS_TEXT);
	AddMessageCallback(msg_id); // immediately add coresponding callback for handling the response later.
}

void WebSocketManager::SendJsonResponse(nlohmann::json json)
{
	std::string jsonString = json.dump();
	size_t sent;
	CURLcode result = curl_ws_send(curl, jsonString.c_str(), strlen(jsonString.c_str()), &sent, 0, CURLWS_TEXT);
}

void WebSocketManager::EstablishMasterServerConnection()
{

	// create message handler thread
	updateThread = std::thread(
		[this]
		{
			// 1. initiate websocket connection with ms, grab curl result. if success, send register request then go to REGISTERING.
			// 2. go to message polling loop, if get register response, if success: goto REGISTERED, continue on register loop. if failed,
			// go back to 1.

			while (!this->updateThreadStopToken.stop_requested())
			{

				while (this->webSocketState == WebSocketStates::STATE_CONNECTING)
				{
					if (InitialiseWebSocket())
					{
						spdlog::info("WS:Successfully established websocket connection to masterserver.");
						this->webSocketState == WebSocketStates::STATE_REGISTERING;
						break;
					}
					else
					{
						spdlog::error("WS:Failed to establish websocket connection to masterserver. retrying in 5 seconds.");
						std::this_thread::sleep_for(std::chrono::milliseconds(5000));
					}
				}
				// websocket connection is established, now we keep sending registering request until we are registered.
				std::chrono::steady_clock::time_point lastRegisterRequest = std::chrono::steady_clock::now();
				uint32_t registerRetryCount;
				while (this->webSocketState == WebSocketStates::STATE_REGISTERING)
				{
					WebSocketRequestRegistration request;
					auto elapsed =
						std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastRegisterRequest)
							.count();

					if (elapsed > 3000LL || registerRetryCount == 0)
					{
						if (registerRetryCount == 0)
						{
							spdlog::info("WS:Attempting to register us on masterserver!");
						}
						else
						{
							spdlog::info("WS:Retrying masterserver registration ({})", registerRetryCount);
						}

						registerRetryCount++;
						std::string password = "";
						uint32_t msg_id = dis(gen);
						request.metadata.id = msg_id;
						request.metadata.type = 1;
						request.info.name = "Test Name";
						request.info.desc = "Test Desc";
						request.info.port = 37015;
						request.info.map = "mp_forwardbase_kodai";
						request.info.playlist = "ps";
						request.info.curPlayers = 114;
						request.info.maxPlayers = 514;
						request.info.password = password.empty() ? std::optional<std::string>() : std::optional<std::string>(password);
						request.info.gameState = 3;
						request.regToken = "wtz_very_fat_cock_i_love_sucking";
						nlohmann::json j = request;
						std::string jsonString = j.dump();
						size_t sent;
						CURLcode result = curl_ws_send(curl, jsonString.c_str(), strlen(jsonString.c_str()), &sent, 0, CURLWS_TEXT);
						lastRegisterRequest = std::chrono::steady_clock::now();
						if (result != CURLE_OK)
						{
							spdlog::error("WS:SocketError: {}", curl_easy_strerror(result));
							this->webSocketState = WebSocketStates::STATE_CONNECTING;
							curl_easy_cleanup(curl);
							break;
						}

					} // send register request per 3 sec if we didn't receive a response yet.

					size_t rlen;
					const curl_ws_frame* meta;
					char buffer[16384];
					CURLcode res = curl_ws_recv(curl, buffer, sizeof(buffer), &rlen, &meta);
					if (res != CURLE_OK)
					{
						if (res == CURLE_AGAIN)
						{
							// socket have nothing to give us, wait for it!
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
							continue;
						}
						else
						{
							spdlog::error("WS:SocketError: {}", curl_easy_strerror(res));
						}
					}
					if (meta->flags & CURLWS_TEXT)
					{
						nlohmann::json message = nlohmann::json::parse(buffer);
						auto metadata = message.at("metadata").get<WebSocketMetadata>();
						if (metadata.id != request.metadata.id)
						{
							spdlog::error("WS:Registration error: message id mismatch!");
							continue;
						}
						if (metadata.type == 2) // reg response
						{
							auto response = message.get<WebSocketResponseRegistration>();
							if (response.success)
							{
								spdlog::info("WS:Successfully registered on masterserver!");
								this->webSocketState = WebSocketStates::STATE_REGISTERED;
								break;
							}
							else
							{
								spdlog::error("WS:Failed while registering on masterserver: {}. Retrying in 3 seconds.", response.error.value().msg);
								std::this_thread::sleep_for(std::chrono::milliseconds(3000));
							}
						}
					}
				}

				while (this->webSocketState == WebSocketStates::STATE_REGISTERED)
				{
					// recv operations
					size_t rlen;
					const curl_ws_frame* meta;
					char buffer[16384];
					CURLcode res = curl_ws_recv(curl, buffer, sizeof(buffer), &rlen, &meta);
					if (res != CURLE_OK)
					{
						if (res == CURLE_AGAIN)
						{
							// socket have nothing to give us, wait for it!
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
							continue;
						}
						else
						{
							spdlog::error("WS:SocketError: {}", curl_easy_strerror(res));
							curl_easy_cleanup(curl);
							this->webSocketState = WebSocketStates::STATE_CONNECTING;
						}
					}
					if (meta->flags & CURLWS_PING)
					{
						// ms sent ping frames to us, respond with presence data
						std::string password = "";
						WebSocketResponseServerPresence payload;
						payload.name = "Test Name";
						payload.desc = "Test Desc";
						payload.port = 37015;
						payload.map = "mp_forwardbase_kodai";
						payload.playlist = "ps";
						payload.curPlayers = 114;
						payload.maxPlayers = 514;
						payload.password = password.empty() ? std::optional<std::string>() : std::optional<std::string>(password);
						payload.gameState = 3;
						nlohmann::json j = payload;
						std::string jstr = j.dump();
						size_t sent;
						CURLcode result = curl_ws_send(curl, jstr.c_str(), strlen(jstr.c_str()), &sent, 0, CURLWS_PONG);
						continue;
					}
					else if (meta->flags & CURLWS_CLOSE)
					{
						// send a close frame to server, then cleanup
						spdlog::info("WS:Masterserver sent close frame to us. Disconnecting");
						this->webSocketState = WebSocketStates::STATE_CONNECTING;
						//size_t sent;
						//CURLcode result = curl_ws_send(curl, jstr.c_str(), strlen(jstr.c_str()), &sent, 0, CURLWS_CLOSE);
					}
					else if (meta->flags & CURLWS_TEXT)
					{
						nlohmann::json message = nlohmann::json::parse(buffer);
						auto metadata = message.at("metadata").get<WebSocketMetadata>();

						// handle incoming message by type first, so we don't have to traverse callbacks on every recv call.
						if (metadata.type == 3)
						{
							// player join request
							// put this in a new thread and move on to the next message, cause player join request can take quite some time
							// (download pdata etc)
							std::thread(
								[this, message]
								{
									auto request = message.get<WebSocketRequestPlayerJoin>();

									// strip some data we need from the message

									// download pdata, add remote player info and send success back.

									WebSocketResponsePlayerJoin response;
									response.metadata.type = 4;
									response.metadata.id = request.metadata.id;
									response.success = true;
									SendJsonResponse(response);
								})
								.detach();
							continue;
						}

						// handle response messages
						for (auto it = this->messageCallbacks.begin(); it != this->messageCallbacks.end();)
						{
							if (metadata.id == it->first)
							{
								if (it->second(message)) // execute the callback and remove it from pending messages
								{
									it = this->messageCallbacks.erase(it);
									break; // go to the next message
								}
							}
							++it;
						}
					}
					// sleep
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}
		});
	updateThread.detach();
}
void WebSocketManager::AddMessageCallback(int id)
{
	// process general messages here
	messageCallbacks.emplace(
		id,
		[this](nlohmann::json message)
		{
			auto metadata = message.at("metadata").get<WebSocketMetadata>();
			switch (metadata.type)
			{
			case 1:
				return true;
				
			default:
				return false;
			}
		});
}

ON_DLL_LOAD_RELIESON("engine.dll", MasterServer, (ConCommand, ServerPresence), (CModule module))
{
	g_pMasterServerManager = new MasterServerManager;

	Cvar_ns_masterserver_hostname = new ConVar("ns_masterserver_hostname", "127.0.0.1", FCVAR_NONE, "");
	Cvar_ns_curl_log_enable = new ConVar("ns_curl_log_enable", "0", FCVAR_NONE, "Whether curl should log to the console");

	RegisterConCommand("ns_fetchservers", ConCommand_ns_fetchservers, "Fetch all servers from the masterserver", FCVAR_CLIENTDLL);
	RegisterConCommand("ns_wstest", ConCommand_ns_wstest, "Test websocket connection", FCVAR_CLIENTDLL);
	MasterServerPresenceReporter* presenceReporter = new MasterServerPresenceReporter;
	g_pServerPresence->AddPresenceReporter(presenceReporter);
}

void MasterServerPresenceReporter::CreatePresence(const ServerPresence* pServerPresence)
{
	NOTE_UNUSED(pServerPresence);
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
		if (Plat_FloatTime() < m_fNextAddServerAttemptTime)
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
	NOTE_UNUSED(pServerPresence);
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
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
			curl_easy_setopt(
				curl,
				CURLOPT_URL,
				fmt::format(
					"{}/server/remove_server?id={}", Cvar_ns_masterserver_hostname->GetString(), g_pMasterServerManager->m_sOwnServerId)
					.c_str());

			CURLcode result = curl_easy_perform(curl);
			curl_easy_cleanup(curl);
		});

	requestThread.detach();
}

void MasterServerPresenceReporter::RunFrame(double flCurrentTime, const ServerPresence* pServerPresence)
{
	NOTE_UNUSED(flCurrentTime);
	NOTE_UNUSED(pServerPresence);
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
			m_fNextAddServerAttemptTime = Plat_FloatTime() + 20.0f;
			break;
		}

		if (m_nNumRegistrationAttempts >= MAX_REGISTRATION_ATTEMPTS)
		{
			spdlog::log(
				IsDedicatedServer() ? spdlog::level::level_enum::err : spdlog::level::level_enum::warn,
				"Reached max ms server registration attempts.");
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

	spdlog::info("Attempting to register the local server to the master server.");

	addServerFuture = std::async(
		std::launch::async,
		[threadedPresence, modInfo, hostname, pServerPresence]
		{
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			std::string readBuffer;
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

			curl_mime* mime = curl_mime_init(curl);
			curl_mimepart* part = curl_mime_addpart(mime);

			// Lambda to quickly cleanup resources and return a value.
			auto ReturnCleanup =
				[curl, mime](MasterServerReportPresenceResult result, const char* id = "", const char* serverAuthToken = "")
			{
				curl_easy_cleanup(curl);
				curl_mime_free(mime);

				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;
				data.id = id;
				data.serverAuthToken = serverAuthToken;

				return data;
			};

			// don't log errors if we wouldn't actually show up in the server list anyway (stop tickets)
			// except for dedis, for which this error logging is actually pretty important
			bool shouldLogError = IsDedicatedServer() || (!strstr(pServerPresence->m_MapName, "mp_lobby") &&
														  strstr(pServerPresence->m_PlaylistName, "private_match"));

			curl_mime_data(part, modInfo.c_str(), modInfo.size());
			curl_mime_name(part, "modinfo");
			curl_mime_filename(part, "modinfo.json");
			curl_mime_type(part, "application/json");

			curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

			// format every paramter because computers hate me
			{
				char* nameEscaped = curl_easy_escape(curl, threadedPresence.m_sServerName.c_str(), 0);
				char* descEscaped = curl_easy_escape(curl, threadedPresence.m_sServerDesc.c_str(), 0);
				char* mapEscaped = curl_easy_escape(curl, threadedPresence.m_MapName, 0);
				char* playlistEscaped = curl_easy_escape(curl, threadedPresence.m_PlaylistName, 0);
				char* passwordEscaped = curl_easy_escape(curl, threadedPresence.m_Password, 0);

				curl_easy_setopt(
					curl,
					CURLOPT_URL,
					fmt::format(
						"{}/server/"
						"add_server?port={}&authPort=udp&name={}&description={}&map={}&playlist={}&maxPlayers={}&password={}",
						hostname.c_str(),
						threadedPresence.m_iPort,
						nameEscaped,
						descEscaped,
						mapEscaped,
						playlistEscaped,
						threadedPresence.m_iMaxPlayers,
						passwordEscaped)
						.c_str());

				curl_free(nameEscaped);
				curl_free(descEscaped);
				curl_free(mapEscaped);
				curl_free(playlistEscaped);
				curl_free(passwordEscaped);
			}

			CURLcode result = curl_easy_perform(curl);

			if (result == CURLcode::CURLE_OK)
			{
				rapidjson_document serverAddedJson;
				serverAddedJson.Parse(readBuffer.c_str());

				// If we could not parse the JSON or it isn't an object, assume the MS is either wrong or we're completely out of date.
				// No retry.
				if (serverAddedJson.HasParseError())
				{
					if (shouldLogError)
						spdlog::error(
							"Failed reading masterserver authentication response: encountered parse error \"{}\"",
							rapidjson::GetParseError_En(serverAddedJson.GetParseError()));
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}

				if (!serverAddedJson.IsObject())
				{
					if (shouldLogError)
						spdlog::error("Failed reading masterserver authentication response: root object is not an object");
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}

				// Log request id for easier debugging when combining with logs on masterserver
				if (serverAddedJson.HasMember("id"))
				{
					spdlog::info("Request id: {}", serverAddedJson["id"].GetString());
				}
				else
				{
					spdlog::error("Couldn't find request id in response");
				}

				if (serverAddedJson.HasMember("error"))
				{
					if (shouldLogError)
					{
						spdlog::error("Failed reading masterserver response: got fastify error response");
						spdlog::error(readBuffer);
					}

					// If this is DUPLICATE_SERVER, we'll retry adding the server every 20 seconds.
					// The master server will only update its internal server list and clean up dead servers on certain events.
					// And then again, only if a player requests the server list after the cooldown (1 second by default), or a server is
					// added/updated/removed. In any case this needs to be fixed in the master server rewrite.
					if (serverAddedJson["error"].HasMember("enum") &&
						strcmp(serverAddedJson["error"]["enum"].GetString(), "DUPLICATE_SERVER") == 0)
					{
						if (shouldLogError)
							spdlog::error("Cooling down while the master server cleans the dead server entry, if any.");
						return ReturnCleanup(MasterServerReportPresenceResult::FailedDuplicateServer);
					}

					// Retry until we reach max retries.
					return ReturnCleanup(MasterServerReportPresenceResult::Failed);
				}

				if (!serverAddedJson["success"].IsTrue())
				{
					if (shouldLogError)
						spdlog::error("Adding server to masterserver failed: \"success\" is not true");
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}

				if (!serverAddedJson.HasMember("id") || !serverAddedJson["id"].IsString() ||
					!serverAddedJson.HasMember("serverAuthToken") || !serverAddedJson["serverAuthToken"].IsString())
				{
					if (shouldLogError)
						spdlog::error("Failed reading masterserver response: malformed json object");
					return ReturnCleanup(MasterServerReportPresenceResult::FailedNoRetry);
				}

				spdlog::info("Successfully registered the local server to the master server.");
				return ReturnCleanup(
					MasterServerReportPresenceResult::Success,
					serverAddedJson["id"].GetString(),
					serverAddedJson["serverAuthToken"].GetString());
			}
			else
			{
				if (shouldLogError)
					spdlog::error("Failed adding self to server list: error {}", curl_easy_strerror(result));
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

	updateServerFuture = std::async(
		std::launch::async,
		[threadedPresence, serverId, hostname, modinfo]
		{
			CURL* curl = curl_easy_init();
			SetCommonHttpClientOptions(curl);

			// Lambda to quickly cleanup resources and return a value.
			auto ReturnCleanup = [curl](MasterServerReportPresenceResult result, const char* id = "", const char* serverAuthToken = "")
			{
				curl_easy_cleanup(curl);

				MasterServerPresenceReporter::ReportPresenceResultData data;
				data.result = result;

				if (id != nullptr)
				{
					data.id = id;
				}

				if (serverAuthToken != nullptr)
				{
					data.serverAuthToken = serverAuthToken;
				}

				return data;
			};

			std::string readBuffer;
			curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteToStringBufferCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
			curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);

			// send all registration info so we have all necessary info to reregister our server if masterserver goes down,
			// without a restart this isn't threadsafe :terror:
			{
				char* nameEscaped = curl_easy_escape(curl, threadedPresence.m_sServerName.c_str(), 0);
				char* descEscaped = curl_easy_escape(curl, threadedPresence.m_sServerDesc.c_str(), 0);
				char* mapEscaped = curl_easy_escape(curl, threadedPresence.m_MapName, 0);
				char* playlistEscaped = curl_easy_escape(curl, threadedPresence.m_PlaylistName, 0);
				char* passwordEscaped = curl_easy_escape(curl, threadedPresence.m_Password, 0);

				curl_easy_setopt(
					curl,
					CURLOPT_URL,
					fmt::format(
						"{}/server/"
						"update_values?id={}&port={}&authPort=udp&name={}&description={}&map={}&playlist={}&playerCount={}&"
						"maxPlayers={}&password={}",
						hostname.c_str(),
						serverId.c_str(),
						threadedPresence.m_iPort,
						nameEscaped,
						descEscaped,
						mapEscaped,
						playlistEscaped,
						threadedPresence.m_iPlayerCount,
						threadedPresence.m_iMaxPlayers,
						passwordEscaped)
						.c_str());

				curl_free(nameEscaped);
				curl_free(descEscaped);
				curl_free(mapEscaped);
				curl_free(playlistEscaped);
				curl_free(passwordEscaped);
			}

			curl_mime* mime = curl_mime_init(curl);
			curl_mimepart* part = curl_mime_addpart(mime);

			curl_mime_data(part, modinfo.c_str(), modinfo.size());
			curl_mime_name(part, "modinfo");
			curl_mime_filename(part, "modinfo.json");
			curl_mime_type(part, "application/json");

			curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);

			CURLcode result = curl_easy_perform(curl);

			if (result == CURLcode::CURLE_OK)
			{
				rapidjson_document serverAddedJson;
				serverAddedJson.Parse(readBuffer.c_str());

				const char* updatedId = nullptr;
				const char* updatedAuthToken = nullptr;

				if (!serverAddedJson.HasParseError() && serverAddedJson.IsObject())
				{
					if (serverAddedJson.HasMember("id") && serverAddedJson["id"].IsString())
					{
						updatedId = serverAddedJson["id"].GetString();
					}

					if (serverAddedJson.HasMember("serverAuthToken") && serverAddedJson["serverAuthToken"].IsString())
					{
						updatedAuthToken = serverAddedJson["serverAuthToken"].GetString();
					}
				}

				return ReturnCleanup(MasterServerReportPresenceResult::Success, updatedId, updatedAuthToken);
			}
			else
			{
				spdlog::warn("Heartbeat failed with error {}", curl_easy_strerror(result));
				return ReturnCleanup(MasterServerReportPresenceResult::Failed);
			}
		});
}
