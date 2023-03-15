#pragma once

#include "core/convar/convar.h"
#include "server/serverpresence.h"
#include <winsock2.h>
#include <string>
#include <shared_mutex>
#include <cstring>
#include <future>
#include "scripts/scriptmatchmakingevents.h"
#include <unordered_set>
extern ConVar* Cvar_ns_masterserver_hostname;
extern ConVar* Cvar_ns_curl_log_enable;

struct RemoteModInfo
{
	std::string Name;
	std::string Version;
};

struct RemoteServerInfo
{
	std::string id;
	std::string name;
	std::string description;
	std::string map;
	std::string playlist;
	std::string region;
	std::vector<RemoteModInfo> requiredMods;
	int gameState;
	int playerCount;
	int maxPlayers;

	// connection stuff
	bool requiresPassword;
};

struct RemoteServerConnectionInfo
{
	std::string authToken;
	in_addr ip;
	unsigned short port;
	unsigned int conv;
};

struct MainMenuPromoData
{
	std::string newInfoTitle1;
	std::string newInfoTitle2;
	std::string newInfoTitle3;

	std::string largeButtonTitle;
	std::string largeButtonText;
	std::string largeButtonUrl;
	int largeButtonImageIndex;

	std::string smallButton1Title;
	std::string smallButton1Url;
	int smallButton1ImageIndex;

	std::string smallButton2Title;
	std::string smallButton2Url;
	int smallButton2ImageIndex;
};

class MasterServerManager
{
  private:
	bool m_RequestingClantag = false;
	bool m_RequestingRemoteBanlistVersion = false;
	bool m_RequestingRemoteBanlist = false;
	bool m_bRequestingServerList = false;
	bool m_bAuthenticatingWithGameServer = false;

  public:
	std::unordered_set<std::string> m_sPlayerPersistenceStates;
	std::mutex m_PlayerPersistenceMutex;

	char m_sOwnServerId[33];
	char m_sOwnServerAuthToken[33];
	std::string m_sOwnClientAuthToken;

	std::string m_sOwnModInfoJson;

	std::string RemoteBanlistString;
	std::string LocalBanlistVersion = "undefined";
	std::string RemoteBanlistVersion;

	bool m_bOriginAuthWithMasterServerDone = false;
	bool m_bOriginAuthWithMasterServerInProgress = false;
	bool m_bOriginAuthWithMasterServerSuccess = false;

	bool m_bSavingPersistentData = false;

	bool m_bScriptRequestingServerList = false;
	bool m_bSuccessfullyConnected = true;

	bool m_bNewgameAfterSelfAuth = false;
	bool m_bScriptAuthenticatingWithGameServer = false;
	bool m_bSuccessfullyAuthenticatedWithGameServer = false;
	std::string m_sAuthFailureReason {};
	std::string m_sAuthFailureMessage {};

	bool m_bHasPendingConnectionInfo = false;
	RemoteServerConnectionInfo m_pendingConnectionInfo;

	std::vector<RemoteServerInfo> m_vRemoteServers;

	bool m_bHasMainMenuPromoData = false;
	MainMenuPromoData m_sMainMenuPromoData;

  public:
	MasterServerManager();
	void ClearServerList();
	void RequestServerList();
	void RequestMainMenuPromos();
	void AuthenticateOriginWithMasterServer(const char* uid, const char* originToken);
	void AuthenticateWithOwnServer(const char* uid, const std::string& playerToken);
	void AuthenticateWithServer(const char* uid, const std::string& playerToken, const std::string& serverId, const char* password);
	void WritePlayerPersistentData(const char* player_id, const char* pdata, size_t pdata_size);
	bool SetLocalPlayerClanTag(std::string clantag);
	bool StartMatchmaking(MatchmakeInfo* status);
	bool CancelMatchmaking();
	bool UpdateMatchmakingStatus(MatchmakeInfo* status);
};

extern MasterServerManager* g_pMasterServerManager;
extern ConVar* Cvar_ns_masterserver_hostname;

/** Result returned in the std::future of a MasterServerPresenceReporter::ReportPresence() call. */
enum class MasterServerReportPresenceResult
{
	// Adding this server to the MS was successful.
	Success,
	// We failed to add this server to the MS and should retry.
	Failed,
	// We failed to add this server to the MS and shouldn't retry.
	FailedNoRetry,
	// We failed to even reach the MS.
	FailedNoConnect,
	// We failed to add the server because an existing server with the same ip:port exists.
	FailedDuplicateServer,
};

class MasterServerPresenceReporter : public ServerPresenceReporter
{
  public:
	/** Full data returned in the std::future of a MasterServerPresenceReporter::ReportPresence() call. */
	struct ReportPresenceResultData
	{
		MasterServerReportPresenceResult result;

		std::optional<std::string> id;
		std::optional<std::string> serverAuthToken;
	};

	const int MAX_REGISTRATION_ATTEMPTS = 5;

	// Called to initialise the master server presence reporter's state.
	void CreatePresence(const ServerPresence* pServerPresence) override;

	// Run on an internal to either add the server to the MS or update it.
	void ReportPresence(const ServerPresence* pServerPresence) override;

	// Called when we need to remove the server from the master server.
	void DestroyPresence(const ServerPresence* pServerPresence) override;

	// Called every frame.
	void RunFrame(double flCurrentTime, const ServerPresence* pServerPresence) override;

  protected:
	// Contains the async logic to add the server to the MS.
	void InternalAddServer(const ServerPresence* pServerPresence);

	// Contains the async logic to update the server on the MS.
	void InternalUpdateServer(const ServerPresence* pServerPresence);

	// The future used for InternalAddServer() calls.
	std::future<ReportPresenceResultData> addServerFuture;

	// The future used for InternalAddServer() calls.
	std::future<ReportPresenceResultData> updateServerFuture;

	int m_nNumRegistrationAttempts;

	double m_fNextAddServerAttemptTime;
};
