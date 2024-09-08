#include "filesystem.h"
#include "core/sourceinterface.h"
#include "mods/modmanager.h"

#include <iostream>
#include <sstream>

bool bReadingOriginalFile = false;
std::string sCurrentModPath;

ConVar* Cvar_ns_fs_log_reads;

SourceInterface<IFileSystem>* g_pFilesystem;

std::string ReadVPKFile(const char* path)
{
	// read scripts.rson file, todo: check if this can be overwritten
	FileHandle_t fileHandle = (*g_pFilesystem)->m_vtable2->Open(&(*g_pFilesystem)->m_vtable2, path, "rb", "GAME", 0);

	std::stringstream fileStream;
	int bytesRead = 0;
	char data[4096];
	do
	{
		bytesRead = (*g_pFilesystem)->m_vtable2->Read(&(*g_pFilesystem)->m_vtable2, data, (int)std::size(data), fileHandle);
		fileStream.write(data, bytesRead);
	} while (bytesRead == std::size(data));

	(*g_pFilesystem)->m_vtable2->Close(*g_pFilesystem, fileHandle);

	return fileStream.str();
}

std::string ReadVPKOriginalFile(const char* path)
{
	// todo: should probably set search path to be g_pModName here also

	bReadingOriginalFile = true;
	std::string ret = ReadVPKFile(path);
	bReadingOriginalFile = false;

	return ret;
}

static void(__fastcall* o_pAddSearchPath)(IFileSystem* fileSystem, const char* pPath, const char* pathID, SearchPathAdd_t addType) =
	nullptr;
static void __fastcall h_AddSearchPath(IFileSystem* fileSystem, const char* pPath, const char* pathID, SearchPathAdd_t addType)
{
	o_pAddSearchPath(fileSystem, pPath, pathID, addType);

	// make sure current mod paths are at head
	if (!strcmp(pathID, "GAME") && sCurrentModPath.compare(pPath) && addType == PATH_ADD_TO_HEAD)
	{
		o_pAddSearchPath(fileSystem, sCurrentModPath.c_str(), "GAME", PATH_ADD_TO_HEAD);
		o_pAddSearchPath(fileSystem, GetCompiledAssetsPath().string().c_str(), "GAME", PATH_ADD_TO_HEAD);
	}
}

void SetNewModSearchPaths(Mod* mod)
{
	// put our new path to the head if we need to read from a different mod path
	// in the future we could also determine whether the file we're setting paths for needs a mod dir, or compiled assets
	//NS::log::fs->info("SetNewModSearchPaths {}", mod->Name);
	if (mod != nullptr)
	{

		if ((fs::absolute(mod->m_ModDirectory) / MOD_OVERRIDE_DIR).string().compare(sCurrentModPath))
		{
			o_pAddSearchPath(
				&*(*g_pFilesystem), (fs::absolute(mod->m_ModDirectory) / MOD_OVERRIDE_DIR).string().c_str(), "GAME", PATH_ADD_TO_HEAD);
			sCurrentModPath = (fs::absolute(mod->m_ModDirectory) / MOD_OVERRIDE_DIR).string();
		}
		//NS::log::fs->info("SetNewModSearchPaths done {}", mod->Name);
	}
	else // push compiled to head
		o_pAddSearchPath(&*(*g_pFilesystem), fs::absolute(GetCompiledAssetsPath()).string().c_str(), "GAME", PATH_ADD_TO_HEAD);
}

bool TryReplaceFile(const char* pPath, bool shouldCompile)
{
	//NS::log::fs->info("tryreplacefile {}", pPath);
	if (bReadingOriginalFile)
		return false;

	if (shouldCompile)
		g_pModManager->CompileAssetsForFile(pPath);

	// idk how efficient the lexically normal check is
	// can't just set all /s in path to \, since some paths aren't in writeable memory
	auto file = g_pModManager->m_ModFiles.find(g_pModManager->NormaliseModFilePath(fs::path(pPath)));
	if (file != g_pModManager->m_ModFiles.end())
	{
		SetNewModSearchPaths(file->second.m_pOwningMod);
		return true;
	}
	//NS::log::fs->info("tryreplacefile done {}", pPath);
	return false;
}

std::string ConvertWideToANSI(const std::wstring& wstr)
{
    int count = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), wstr.length(), NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
    return str;
}

std::wstring ConvertAnsiToWide(const std::string& str)
{
    int count = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.length(), NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), str.length(), &wstr[0], count);
    return wstr;
}

std::string ConvertWideToUtf8(const std::wstring& wstr)
{
    int count = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), NULL, 0, NULL, NULL);
    std::string str(count, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], count, NULL, NULL);
    return str;
}

std::wstring ConvertUtf8ToWide(const std::string& str)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.length(), &wstr[0], count);
    return wstr;
}

std::string SanitizeEncodings(const char* buf)
{
	std::wstring ws = ConvertAnsiToWide(buf);
	return ConvertWideToUtf8(ws);
}
// force modded files to be read from mods, not cache
static bool(__fastcall* o_pReadFromCache)(IFileSystem* filesystem, char* pPath, void* result) = nullptr;
static bool __fastcall h_ReadFromCache(IFileSystem* filesystem, char* pPath, void* result)
{
	if (TryReplaceFile(SanitizeEncodings(pPath).c_str(), true))
		return false;

	return o_pReadFromCache(filesystem, pPath, result);
}

static FileHandle_t(__fastcall* o_pReadFileFromVPK)(VPKData* vpkInfo, uint64_t* b, char* filename) = nullptr;
static FileHandle_t __fastcall h_ReadFileFromVPK(VPKData* vpkInfo, uint64_t* b, char* filename)
{
	// don't compile here because this is only ever called from OpenEx, which already compiles
	if (TryReplaceFile(SanitizeEncodings(filename).c_str(), false))
	{
		*b = -1;
		return b;
	}

	return o_pReadFileFromVPK(vpkInfo, b, filename);
}

static FileHandle_t(__fastcall* o_pCBaseFileSystem__OpenEx)(
	IFileSystem* filesystem, const char* pPath, const char* pOptions, uint32_t flags, const char* pPathID, char** ppszResolvedFilename) =
	nullptr;
static FileHandle_t __fastcall h_CBaseFileSystem__OpenEx(
	IFileSystem* filesystem, const char* pPath, const char* pOptions, uint32_t flags, const char* pPathID, char** ppszResolvedFilename)
{
	TryReplaceFile(pPath, true);
	return o_pCBaseFileSystem__OpenEx(filesystem, pPath, pOptions, flags, pPathID, ppszResolvedFilename);
}

static VPKData* (*o_pMountVPK)(IFileSystem* fileSystem, const char* pVpkPath) = nullptr;
static VPKData* h_MountVPK(IFileSystem* fileSystem, const char* pVpkPath)
{
	NS::log::fs->info("MountVPK {}", pVpkPath);
	VPKData* ret = o_pMountVPK(fileSystem, pVpkPath);

	for (Mod mod : g_pModManager->m_LoadedMods)
	{
		if (!mod.m_bEnabled)
			continue;

		for (ModVPKEntry& vpkEntry : mod.Vpks)
		{
			// if we're autoloading, just load no matter what
			if (!vpkEntry.m_bAutoLoad)
			{
				// resolve vpk name and try to load one with the same name
				// todo: we should be unloading these on map unload manually
				//NS::log::fs->info("ProcessMods {} - {}", SanitizeEncodings(pVpkPath).c_str(),vpkEntry.m_sVpkPath);
				//NS::log::FlushLoggers();
				//std::this_thread::sleep_for(std::chrono::milliseconds(100));
				std::string mapName(fs::path(SanitizeEncodings(pVpkPath).c_str()).filename().string());
				std::string modMapName(fs::path(SanitizeEncodings(vpkEntry.m_sVpkPath.c_str()).c_str()).filename().string());
				if (mapName.compare(modMapName))
					continue;
			}

			VPKData* loaded = o_pMountVPK(fileSystem, vpkEntry.m_sVpkPath.c_str());
			if (!ret) // this is primarily for map vpks and stuff, so the map's vpk is what gets returned from here
				ret = loaded;
		}
	}

	return ret;
}

ON_DLL_LOAD("filesystem_stdio.dll", Filesystem, (CModule module))
{
	o_pReadFileFromVPK = module.Offset(0x5CBA0).RCast<decltype(o_pReadFileFromVPK)>();
	HookAttach(&(PVOID&)o_pReadFileFromVPK, (PVOID)h_ReadFileFromVPK);

	o_pCBaseFileSystem__OpenEx = module.Offset(0x15F50).RCast<decltype(o_pCBaseFileSystem__OpenEx)>();
	HookAttach(&(PVOID&)o_pCBaseFileSystem__OpenEx, (PVOID)h_CBaseFileSystem__OpenEx);

	g_pFilesystem = new SourceInterface<IFileSystem>("filesystem_stdio.dll", "VFileSystem017");

	o_pAddSearchPath = reinterpret_cast<decltype(o_pAddSearchPath)>((*g_pFilesystem)->m_vtable->AddSearchPath);
	HookAttach(&(PVOID&)o_pAddSearchPath, (PVOID)h_AddSearchPath);
	o_pReadFromCache = reinterpret_cast<decltype(o_pReadFromCache)>((*g_pFilesystem)->m_vtable->ReadFromCache);
	HookAttach(&(PVOID&)o_pReadFromCache, (PVOID)h_ReadFromCache);
	o_pMountVPK = reinterpret_cast<decltype(o_pMountVPK)>((*g_pFilesystem)->m_vtable->MountVPK);
	HookAttach(&(PVOID&)o_pMountVPK, (PVOID)h_MountVPK);
}
