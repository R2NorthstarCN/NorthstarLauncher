#pragma once
#include <fstream>
#include <string>
class TempReadWrite
{
  private:
	DWORD m_origProtection;
	void* m_ptr;

  public:
	TempReadWrite(void* ptr);
	~TempReadWrite();
};
class ClientAnticheatSystem
{
  private:
	bool ScanWindowProofAlreadyUploaded = false;
	bool isDllSignatureLoaded = false;
	std::vector<std::string> blacklistedDlls;

  public:
	void ScanModuleHacks();
	void NoFindWindowHack(uintptr_t baseAddress);
	std::string IsDllSignatureSafe(std::string dllname);
	void LoadDllSignatures();
	void CheckDllBlacklist(LPCSTR lpLibFileName);
	void CheckDllBlacklistW(LPCWSTR lpLibFileNameW);
	void FindMaliciousWindow();
	void SendSelfReportToMasterServer(char* info);
	void InitWindowListenerThread();
	void PrintModuleHash();
	char* DumpModule(HMODULE module);
	unsigned long long GetModuleHash(HANDLE proc, HMODULE module);
};

extern ClientAnticheatSystem g_ClientAnticheatSystem;
