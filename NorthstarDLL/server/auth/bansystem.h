#pragma once
#include <fstream>

class ServerBanSystem
{
  private:
	std::ofstream m_sBanlistStream;
	std::vector<uint64_t> m_vBannedUids;
	std::vector<std::string> m_vBanMessages;
	int m_CurrentMessageIndex = 0;

  public:
	void OpenBanlist();
	void ReloadBanlist();
	void ClearBanlist();
	void BanUID(uint64_t uid);
	void UnbanUID(uint64_t uid);
	bool IsUIDAllowed(uint64_t uid);
	void InsertBanUID(uint64_t uid);
	void ParseRemoteBanlistString(std::string banlisttring);
	bool ParseLocalBanMessageFile();
	const char* GetRandomBanMessage();
};

extern ServerBanSystem* g_pBanSystem;
