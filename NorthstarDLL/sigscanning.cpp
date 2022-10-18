#include "pch.h"
#include "sigscanning.h"
#include <map>

// note: sigscanning is only really intended to be used for resolving stuff like shared function definitions
// we mostly use raw function addresses for stuff

size_t GetModuleLength(HMODULE moduleHandle)
{
	// based on sigscan code from ttf2sdk, which is in turn based on CSigScan from https://wiki.alliedmods.net/Signature_Scanning
	MEMORY_BASIC_INFORMATION mem;
	VirtualQuery(moduleHandle, &mem, sizeof(mem));

	IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mem.AllocationBase;
	IMAGE_NT_HEADERS* pe = (IMAGE_NT_HEADERS*)((unsigned char*)dos + dos->e_lfanew);

	return pe->OptionalHeader.SizeOfImage;
}
int DecToHex(int p_intValue)
{
	int l_intResult;
	char* l_pCharRes = new (char);
	sprintf(l_pCharRes, "%X", p_intValue);
	// spdlog::warn("Size:{}",l_pCharRes);
	std::stringstream ss;
	ss << l_pCharRes;
	ss >> l_intResult;
	return l_intResult;
}
MODULEINFO GetModuleInfo(std::string szModule)
{
	MODULEINFO modinfo = {0};
	std::wstring stemp = std::wstring(szModule.begin(), szModule.end());
	LPCWSTR tempname = stemp.c_str();
	HMODULE hModule = GetModuleHandle(tempname);
	if (hModule == 0)
		return modinfo;
	GetModuleInformation(GetCurrentProcess(), hModule, &modinfo, sizeof(MODULEINFO));
	return modinfo;
}
void* FindSignature(std::string dllName, const char* sig, const char* mask)
{
	HMODULE dllAddress = GetModuleHandleA(dllName.c_str());
	MODULEINFO moduleinfo = GetModuleInfo(dllName);
	DWORD dllsize = moduleinfo.SizeOfImage;
	char* dllEnd = (char*)(dllAddress + DecToHex(dllsize));
	// spdlog::warn("Dll start:{}", dllAddress);
	// spdlog::warn("Dll size:{}", DecToHex(dllsize));
	size_t sigLength = strlen(mask);
	if (dllsize < sigLength)
		return nullptr;

	for (char* i = (char*)dllAddress; i < dllEnd - sigLength; i++)
	{
		int j = 0;
		for (; j < sigLength; j++)
			if (mask[j] != '?' && sig[j] != i[j])
				break;

		if (j == sigLength) // loop finished of its own accord
			return (void*)i;
	}

	return nullptr;
}
