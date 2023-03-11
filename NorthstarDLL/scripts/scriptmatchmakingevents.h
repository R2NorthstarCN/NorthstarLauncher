#pragma once
#include <string>
#include <cstring>

struct MatchmakeConnectionInfo
{
  public:
	char authToken[32];

	in_addr ip;
	unsigned short port;
};

const std::string MatchMakingStatus[] = {
	"#MATCH_NOTHING",
	"#MATCHMAKING_SEARCHING_FOR_MATCH",
	"#MATCHMAKING_QUEUED",
	"#MATCHMAKING_ALLOCATING_SERVER",
	"#MATCHMAKING_MATCH_CONNECTING"};

class MatchmakeInfo
{
  public:
	std::string status;
	std::string playlistName;
	std::string etaSeconds;
	std::string mapIdx;
	std::string modeIdx;
	std::string playlistListstr;
	std::vector<std::string> playlistList;
	std::string timeout;
	bool serverReady;
	MatchmakeConnectionInfo* connectionInfo;
	std::string GetByParam(int idx);
};

class MatchmakeManager
{
  private:
	std::thread* requestthread;

  public:
	/*
	 * state:
	 * 0: idle
	 * 1: new matchmake event
	 * 2: matchmaking
	 */
	MatchmakeInfo* info;
	int LocalState = 0;
	MatchmakeManager();
	void StartMatchmake(std::string playlistlist);
	void CancelMatchmake();
};

extern MatchmakeManager* g_pMatchmakerManager;
