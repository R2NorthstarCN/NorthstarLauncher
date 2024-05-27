#pragma once

#include "core/convar/convar.h"
#include "curl/curl.h"
#include "server/serverpresence.h"
#include <winsock2.h>
#include <string>
#include <cstring>
#include <future>
#include <unordered_set>
#include <map>

namespace nlohmann {

	template <class T>
	void to_json(nlohmann::json& j, const std::optional<T>& v)
	{
		if (v.has_value())
			j = *v;
		else
			j = nullptr;
	}

	template <class T>
	void from_json(const nlohmann::json& j, std::optional<T>& v)
	{
		if (j.is_null())
			v = std::nullopt;
		else
			v = j.get<T>();
	}
	
} // namespace nlohmann

extern ConVar* Cvar_ns_masterserver_hostname;
extern ConVar* Cvar_ns_curl_log_enable;

struct RemoteModInfo
{
	std::string Name;
	std::string Version;
	bool RequiredOnClient;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(RemoteModInfo, Name, Version, RequiredOnClient)
};

class RemoteServerInfo
{
public:
	char id[33]; // 32 bytes + nullterminator

	// server info
	char name[64];
	std::string description;
	char map[32];
	char playlist[16];
	char region[32];
	std::vector<RemoteModInfo> requiredMods;

	int playerCount;
	int maxPlayers;

	// connection stuff
	bool requiresPassword;

public:
	RemoteServerInfo(
		const char* newId,
		const char* newName,
		const char* newDescription,
		const char* newMap,
		const char* newPlaylist,
		const char* newRegion,
		int newPlayerCount,
		int newMaxPlayers,
		bool newRequiresPassword);
};

struct RemoteServerConnectionInfo
{
public:
	char authToken[32];

	in_addr ip;
	unsigned short port;
};

struct MainMenuPromoData
{
public:
	struct NewInfo
	{
		std::string Title1;
		std::string Title2;
		std::string Title3;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(NewInfo, Title1, Title2, Title3)
	} newInfo;

	struct LargeButton
	{
		int ImageIndex;
		std::string Text;
		std::string Title;
		std::string Url;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(LargeButton, ImageIndex, Text, Title, Url)
	} largeButton;

	struct SmallButton
	{
		int ImageIndex;
		std::string Title;
		std::string Url;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(SmallButton, ImageIndex, Title, Url)
	} smallButton1, smallButton2;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(MainMenuPromoData, newInfo, largeButton, smallButton1, smallButton2)
};

class MasterServerManager
{
private:
	bool m_bRequestingServerList = false;
	bool m_bAuthenticatingWithGameServer = false;

public:
	char m_sOwnServerId[33];
	char m_sOwnServerAuthToken[33];
	char m_sOwnClientAuthToken[33];

	std::string m_sOwnModInfoJson;

	bool m_bOriginAuthWithMasterServerDone = false;
	bool m_bOriginAuthWithMasterServerInProgress = false;

	bool m_bOriginAuthWithMasterServerSuccessful = false;
	std::string m_sOriginAuthWithMasterServerErrorCode = "";
	std::string m_sOriginAuthWithMasterServerErrorMessage = "";

	bool m_bSavingPersistentData = false;

	bool m_bScriptRequestingServerList = false;
	bool m_bSuccessfullyConnected = true;

	bool m_bNewgameAfterSelfAuth = false;
	bool m_bScriptAuthenticatingWithGameServer = false;
	bool m_bSuccessfullyAuthenticatedWithGameServer = false;
	std::string m_sAuthFailureReason {};

	bool m_bHasPendingConnectionInfo = false;
	RemoteServerConnectionInfo m_pendingConnectionInfo;

	std::vector<RemoteServerInfo> m_vRemoteServers;

	bool m_bHasMainMenuPromoData = false;
	MainMenuPromoData m_sMainMenuPromoData;

	std::optional<RemoteServerInfo> m_currentServer;
	std::string m_sCurrentServerPassword;

	std::unordered_set<std::string> m_handledServerConnections;

public:
	MasterServerManager();

	void ClearServerList();
	void RequestServerList();
	void RequestMainMenuPromos();
	void AuthenticateOriginWithMasterServer(const char* uid, const char* originToken);
	void AuthenticateWithOwnServer(const char* uid, const char* playerToken);
	void AuthenticateWithServer(const char* uid, const char* playerToken, RemoteServerInfo server, const char* password);
	void WritePlayerPersistentData(const char* playerId, const char* pdata, size_t pdataSize);
	void ProcessConnectionlessPacketSigreq1(std::string req);
};

extern MasterServerManager* g_pMasterServerManager;
extern ConVar* Cvar_ns_masterserver_hostname;

enum WebSocketStates
{
	STATE_CONNECTING = 0,
	STATE_REGISTERING,
	STATE_REGISTERED
};




struct WebSocketMetadata
{
public:
	uint32_t type;
	int64_t id;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMetadata, type, id)
};
struct WebSocketError
{
public:
	int32_t type;
	std::string msg;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketError,type,msg)
};

struct WebSocketResponseServerPresence
{
public:
	std::string name;
	std::string desc;
	uint16_t port;
	std::string map;
	std::string playlist;
	int32_t curPlayers;
	int32_t maxPlayers;
	std::optional<std::string> password;
	int32_t gameState;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketResponseServerPresence,name,desc,port,map,playlist,curPlayers,maxPlayers,password,gameState)
};

struct WebSocketRequestRegistration
{
public:
	WebSocketMetadata metadata; 
	WebSocketResponseServerPresence info;
	std::string regToken;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketRequestRegistration, metadata, info, regToken)
};

struct WebSocketResponseRegistration
{
public:
	WebSocketMetadata metadata; 
	bool success;
	std::optional<int64_t> id;
	std::optional<WebSocketError> error;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketResponseRegistration,metadata,success,id,error)
};


struct WebSocketRequestPlayerJoin
{
	WebSocketMetadata metadata;
	std::string sessionToken;
	std::string username;
	std::string clantag;
	uint32_t conv;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketRequestPlayerJoin,metadata,sessionToken,username,clantag,conv)
};

struct WebSocketResponsePlayerJoin
{
	WebSocketMetadata metadata;

	bool success;
	std::optional<WebSocketError> error;
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketResponsePlayerJoin,metadata,success,error)
};


class WebSocketManager
{
public:
	static WebSocketManager& instance();

private:
	WebSocketManager();
	bool InitialiseWebSocket();
	bool SendRegistrationRequest();
	void EstablishMasterServerConnection();
	void AddMessageCallback(int);
	void SendJsonRequest(nlohmann::json);
	void SendJsonResponse(nlohmann::json);
	CURL* curl;
	int webSocketState;
	bool supressWebSocket = false;
	std::map<int, std::function<bool(nlohmann::json)>> messageCallbacks;
	std::mutex webSocketMtx;
	std::thread updateThread;
	std::thread stateThread;
	std::stop_token updateThreadStopToken;
};

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
