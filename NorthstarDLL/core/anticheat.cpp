#include "pch.h"
#include "hooks.h"
#include "util/sigscanning.h"
#include <string>
#include "anticheat.h"
#include <wchar.h>
#include <iostream>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <Psapi.h>
#include "masterserver/masterserver.h"
#include <windows.h>
#include <tchar.h>
#include "nlohmann/json.hpp"
AUTOHOOK_INIT()
//#include "xorstr.hpp"
/*
	Module Scanning
	1.Create a Json object containing module names and modules hashes.
	2.Scan Modules in memory for its hash.
	3.Push each module name with its hash into the json object.
	4.Send Json object to server with libcurl.
	5.Clean up the Json document and libcurl shits.

*/

/*
 *	We don't want to take too much bandwidth on sending module info,
 *	instead, we should only send delta info to server.
 *	this will massively reduce masterserver proxy load as we still have to make sure
 *	the server is not being abused.
 *
 *	Plan:
 *	having a vector to store all the found hashes locally. which is the ones that were scanned before.
 *	we still perform full module scan in a certain interval, but we don't send everything completely.
 *	ideal: when we get masterserver response during origin auth, we initialize a module scan that sends
 *	everything that's present to the client. after that, we send only the modules that were new. (not present in scan history)
 */

// these are the data we need for identifying a client
// std::string uid = R2::g_pLocalPlayerUserID;
// std::string token = g_pMasterServerManager->m_sOwnClientAuthToken;

// we put all previously scanned modules's hash here, to keep traking the new ones that are not in the list.
std::vector<std::string> ModuleScanHistory;

std::vector<std::string> v_mScannedModules;
std::vector<std::string> v_mSafeModules;
	// put current scanned modules in here, so we can exclude all found modules from ModuleScanHistory.
// std::vector<std::string> ModuleScanTemp;

int ModuleScanUploadCount = 0;


//ClientAnticheatSystem* g_ClientAnticheatSystem;

TempReadWrite::TempReadWrite(void* ptr)
{
	m_ptr = ptr;
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery(m_ptr, &mbi, sizeof(mbi));
	VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &mbi.Protect);
	m_origProtection = mbi.Protect;
}

TempReadWrite::~TempReadWrite()
{
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery(m_ptr, &mbi, sizeof(mbi));
	VirtualProtect(mbi.BaseAddress, mbi.RegionSize, m_origProtection, &mbi.Protect);
}

std::wstring StringToWString(const std::string& s)
{
	std::wstring temp(s.length(), L' ');
	std::copy(s.begin(), s.end(), temp.begin());
	return temp;
}

std::string WStringToString(const std::wstring& s)
{
	std::string temp(s.length(), ' ');
	std::copy(s.begin(), s.end(), temp.begin());
	return temp;
}
void ClientAnticheatSystem::NoFindWindowHack(uintptr_t baseAddress)
{
	unsigned seed = time(0);
	srand(seed);
	char ObfChar[3];
	int ObfuscateNum = 100 + rand() % 899;
	sprintf(ObfChar, "%d", ObfuscateNum);
	std::cout << ObfuscateNum << std::endl;
	char* ptr = ((char*)baseAddress + 0x607BD0);
	TempReadWrite rw(ptr);
	*(ptr + 14) = (char)ObfChar[0];
	*(ptr + 16) = (char)ObfChar[1];
	*(ptr + 18) = (char)ObfChar[2];
}
std::string ClientAnticheatSystem::IsDllSignatureSafe(std::string dllname)
{
	if (dllname.substr(dllname.find_last_of(".") + 1) == "dll")
	{
		{
			//spdlog::info("Attempting to sigscan:{}", dllname);
			void* ptr = FindSignature(dllname, "\x60\x7A\x24\x51\x67\x6A\x61\x64\x6F\x6C\x63\x00\x00\x00\x00\x00\x29\xC5\xEF\xE5\xEA\xEA\xE0\xC5", "xxxxxxxxxxxxxxxxxxxxxxxx");
			if (ptr != nullptr)
			{
				return "UC_titan2hook_v1.3";
			}
				
		}

		{
			//spdlog::info("Attempting to sigscan:{}", dllname);
			void* ptr = FindSignature(dllname, "\x48\x8B\xD1\x48\x8B\x52\x28\x48\x8D\x0D\x68\x83\x00\x00\x48\x8B\x92\x80\x96\x84\x0F\xE8\xFC\x01", "xxxxxxxxxxxxxxxxxxxxxxxx");
			if (ptr != nullptr)
			{
				return "OS_Titanfall2.dll";
			}
				
		}
	
	
	
		return "none";
	}
	else
	{
		return "none";
	}
}

void ClientAnticheatSystem::LoadDllSignatures()
{
	// blacklist
	blacklistedDlls.push_back("titan2hook_v1.3_[unknowncheats.me]_.dll");
	blacklistedDlls.push_back("Titanfall2.dll");


	isDllSignatureLoaded = true;
}

void ClientAnticheatSystem::CheckDllBlacklist(LPCSTR lpLibFileName)
{
	return;
	// spdlog::error(lpLibFileName);
	// GetModuleHash(GetModuleHandleA(lpLibFileName));
	if (strstr(GetCommandLineA(), "-dedicated"))
	{
		return;
	}
	// spdlog::info("checking Dll:{}", lpLibFileName);
	//  Return true if Dll is not in blacklist
	if (isDllSignatureLoaded)
	{

		std::string filepath = lpLibFileName;
		std::size_t dirpos = filepath.find_last_of("\\") + 1;
		std::string file = filepath.substr(dirpos, filepath.length() - dirpos);
		// auto findResult = std::find(blacklistedDlls.begin(), blacklistedDlls.end(), filepath);
		if (std::find(blacklistedDlls.begin(), blacklistedDlls.end(), file) != blacklistedDlls.end())
		{
			// Dangerous Dll name found
			// spdlog::info("Malicious Dll found:{}", filepath);
			SendSelfReportToMasterServer((char*)file.c_str());
			// MessageBoxA(0, "Northstar has crashed! Error code: 0xFFFFFFFF", "Northstar has crashed!", MB_ICONERROR | MB_OK);
			// exit(0);
			return;
		}
		else
		{
			std::string result = IsDllSignatureSafe(file);
			// spdlog::info("safe Dll found:{}", file);
			if (result == "none")
			{
				//spdlog::info("DLL:" + file + "Is safe");
				return;
			}
			else
			{
				SendSelfReportToMasterServer((char*)result.c_str());
				// MessageBoxA(0, "Northstar has crashed! Error code: 0xFFFFFFFE", "Northstar has crashed!", MB_ICONERROR | MB_OK);
				// exit(0);
				//spdlog::info("DLL:" + file + "Is NOT safe");
				return;
			}
		}
	}
}

void ClientAnticheatSystem::CheckDllBlacklistW(LPCWSTR lpLibFileNameW)
{
	if (lpLibFileNameW != NULL)
	{
		// Return true if Dll is not in blacklist
		std::string lpLibFileName = WStringToString(lpLibFileNameW);
		CheckDllBlacklist(lpLibFileName.c_str());
	}
}

void ClientAnticheatSystem::FindMaliciousWindow()
{
	if (ScanWindowProofAlreadyUploaded)
	{
		return;
	}
	// int MaliciousWindowFound = 0;
	LPCSTR title_aige = "艾 格 Tpis 3.X";
	HWND windowN_aige = FindWindowA(NULL, title_aige);
	if (windowN_aige == NULL)
	{
		// Malicious window title not found
	}
	else
	{
		char* info = (char*)"Aige_Tpis_Wnd";
		SendSelfReportToMasterServer(info);
		// MessageBoxA(0, "Northstar has crashed! Error code: 0xFFFFFFFD", "Northstar has crashed!", MB_ICONERROR | MB_OK);
		//  exit(0);
		ScanWindowProofAlreadyUploaded = true;
		return;
	}
	return;


	LPCSTR title_ucexternal = "ttf2 [steam]";
	HWND window_ucexternal = FindWindowA(NULL, title_ucexternal);
	if (window_ucexternal == NULL)
	{
		// Malicious window title not found
	}
	else
	{
		char* info = (char*)"UC_TTF2external";
		SendSelfReportToMasterServer(info);
		// MessageBoxA(0, "Northstar has crashed! Error code: 0xFFFFFFFD", "Northstar has crashed!", MB_ICONERROR | MB_OK);
		// exit(0);
		ScanWindowProofAlreadyUploaded = true;
		return;
	}
	return;
}

void ClientAnticheatSystem::SendSelfReportToMasterServer(char* info)
{

	g_pMasterServerManager->SendCheatingProof(info);

	return;
}

void ClientAnticheatSystem::ScanModuleHacks()
{
	std::map<unsigned long long, std::string> ModuleScanTemp;
	HMODULE hMods[1024];
	HANDLE proc = GetCurrentProcess();
	DWORD cbNeeded;
	unsigned int i;
	if (EnumProcessModules(proc, hMods, sizeof(hMods), &cbNeeded))
	{
		for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
		{
			char szModName[MAX_PATH];
			if (GetModuleFileNameExA(proc, hMods[i], szModName, sizeof(szModName) / sizeof(char)))
			{
				// Print the module name and handle value.
				std::string filepath(szModName);
				std::size_t dirpos = filepath.find_last_of("\\") + 1;
				std::string filename = filepath.substr(dirpos, filepath.length() - dirpos);
				// how to do table in c++ god please save me
				auto cheatlist = std::find(v_mScannedModules.begin(), v_mScannedModules.end(), filename);
				auto safelist = std::find(v_mSafeModules.begin(), v_mSafeModules.end(), filename);
				if (filename == "Northstar.dll")
				{
					continue;
				}
					
				if (cheatlist == v_mScannedModules.end() && safelist == v_mSafeModules.end()) // unsure if its safe or not
				{
					// if the module is not in either of the found module lists
					std::string result = IsDllSignatureSafe(filename);
					if (result != "none")
					{
						v_mScannedModules.push_back(filename);
						SendSelfReportToMasterServer((char*)(result + "|" + filename).c_str());
					}
					else
					{
						//scanned module is safe, do not perform another scan
						v_mSafeModules.push_back(filename);
					}
				}
				
					
				

				// spdlog::error("Name:{}|Hash:{}", file, GetModuleHash(proc, hMods[i]));
			}
		}
		// Send finished info here
		
	}
}

void ClientAnticheatSystem::InitWindowListenerThread()
{

	if (strstr(GetCommandLineA(), "-dedicated"))
	{
		return;
	}
	LoadDllSignatures();
	std::thread WindowListenerThread(
		[this]
		{
			while (true)
			{
				//spdlog::info("running scan job");
				ScanModuleHacks();
				FindMaliciousWindow();
				std::this_thread::sleep_for(std::chrono::milliseconds(10000));
			}
		});

	WindowListenerThread.detach();
}

ON_DLL_LOAD("engine.dll", ACinit, (CModule module))
{
	g_ClientAnticheatSystem.NoFindWindowHack(module.m_nAddress);
}
