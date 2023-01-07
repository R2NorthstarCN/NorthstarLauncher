#pragma once
#include <string>
#include <cstring>


const std::string MatchMakingStatus[] =
{
	"#MATCH_NOTHING",
	"#MATCHMAKING_SEARCHING_FOR_MATCH",
	"#MATCHMAKING_QUEUED",
	"#MATCHMAKING_ALLOCATING_SERVER",
	"#MATCHMAKING_MATCH_CONNECTING"
};

struct DummyMatchmakingParamData
{
	std::string playlistName = "ps";
	std::string etaSeconds = "30";
	std::string mapIdx = "1";
	std::string modeIdx = "1";
	std::string PlaylistList = "ps,aitdm";
};


class MatchmakeStatus
{
public:
	std::string playlistName;
	std::string etaSeconds;
	std::string mapIdx;
	std::string modeIdx;
	std::string PlaylistList;
	std::string GetByParam(int idx);
	void UpdateValues(std::string playlistname, std::string etaseconds, std::string mapidx, std::string modeidx, std::string playlistlist);
};

class Matchmaker
{
  private:
	MatchmakeStatus* status;
	std::string playlistlist;
  public:
	Matchmaker(std::string playlistlist);
	//~Matchmaker();
};
