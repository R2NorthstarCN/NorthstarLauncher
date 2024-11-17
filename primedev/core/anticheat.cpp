#include "pch.h"
#include "hooks.h"
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

AUTOHOOK_INIT()

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

ON_DLL_LOAD("engine.dll", ACinit, (CModule module))
{
	g_ClientAnticheatSystem.NoFindWindowHack(module.GetModuleBase());
}
