#include "logging/logging.h"
#include "logging/crashhandler.h"
#include "core/memalloc.h"
#include "core/vanilla.h"
#include "config/profile.h"
#include "plugins/plugins.h"
#include "plugins/pluginmanager.h"
#include "util/version.h"
#include "util/wininfo.h"
#include "squirrel/squirrel.h"
#include "server/serverpresence.h"
#include "client/igig/igig.h"

#include "windows/libsys.h"

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include "curl/curl.h"

#include <string.h>
#include <filesystem>

bool WindowsVersionSupportsUtf8() {
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
    if (hMod) {
        RtlGetVersionPtr fxPtr = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
        if (fxPtr != nullptr) {
            RTL_OSVERSIONINFOW rovi = {0};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (fxPtr(&rovi) == 0) {
                return (rovi.dwMajorVersion < 10) || 
                       (rovi.dwMajorVersion == 10 && rovi.dwBuildNumber < 18362);
            }
        }
    }
    return false; // Default to false if unable to determine
}

bool IsUtf8BetaOptionEnabled() {
    HKEY hKey;
    DWORD utf8Enabled = 0;
    DWORD bufferSize = sizeof(utf8Enabled);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"ACP", nullptr, nullptr, reinterpret_cast<LPBYTE>(&utf8Enabled), &bufferSize);
        RegCloseKey(hKey);
    }

    return utf8Enabled == 65001;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	NOTE_UNUSED(hModule);
	NOTE_UNUSED(lpReserved);
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		g_NorthstarModule = hModule;
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}

void CheckWindowsVersion()
{
	if(strstr(GetCommandLineA(), "-no_windows_version_check") != NULL)
		return;
		
	if (WindowsVersionSupportsUtf8() && !IsUtf8BetaOptionEnabled()) {
		if(strstr(GetCommandLineA(), "-dedicated") == NULL)
		{
			MessageBoxW(nullptr,
			L"由于运行库与旧版Windows存在兼容性问题，北极星CN 1.17只能保证在Windows 10 1903以上的版本在系统默认设置的情况下正常运行，若是Windows10 1903以下的系统版本，需要在控制面板-时钟和区域-区域-管理-更改系统区域设置-勾选使用Unicode UTF-8。如果在旧系统版本中不启用UTF-8，北极星CN将无法正确处理Windows中文字符。\n这可能会导致以下问题：\n(1) 带有任何中文路径的Mod无法正常工作或导致游戏崩溃\n(2) 游戏安装路径中存在中文字符时崩溃。\n(3) 系统用户名或用户目录名称存在中文字符时游戏崩溃\n若您的系统版本无Unicode UTF-8 选项，或是启用此选项后其他应用程序或目录出现乱码问题，可以通过以下方法解决:\n(1) 升级操作系统版本至Windows 10 1903或更高版本。\n(2) 手动重命名系统用户名和“文档”目录、游戏安装目录、Mods目录中所有含中文名称的文件夹（如模型和音频替换Mod等）以避免北极星CN出现故障。",
			L"警告: 您的操作系统可能不受支持",
			MB_OK | MB_ICONWARNING);
			
		}
	std::wcout << L"警告:由于运行库与旧版Windows存在兼容性问题，北极星CN 1.17只能保证在Windows 10 1903以上的版本在系统默认设置的情况下正常运行，若是Windows10 1903以下的系统版本，需要在控制面板-时钟和区域-区域-管理-更改系统区域设置-勾选使用Unicode UTF-8。如果在旧系统版本中不启用UTF-8，北极星CN将无法正确处理Windows中文字符。\n这可能会导致以下问题：\n(1) 带有任何中文路径的Mod无法正常工作或导致游戏崩溃\n(2) 游戏安装路径中存在中文字符时崩溃。\n(3) 系统用户名或用户目录名称存在中文字符时游戏崩溃\n若您的系统版本无Unicode UTF-8 选项，或是启用此选项后其他应用程序或目录出现乱码问题，可以通过以下方法解决:\n(1) 升级操作系统版本至Windows 10 1903或更高版本。\n(2) 手动重命名系统用户名和“文档”目录、游戏安装目录、Mods目录中所有含中文名称的文件夹（如模型和音频替换Mod等）以避免北极星CN出现故障。" << std::endl;
}
}

extern "C" bool InitialiseNorthstar()
{	
	
	static bool bInitialised = false;
	if (bInitialised)
		return false;

	bInitialised = true;

	CheckWindowsVersion();

	InitialiseNorthstarPrefix();

	// initialise the console if needed (-northstar needs this)
	InitialiseConsole();
	// initialise logging before most other things so that they can use spdlog and it have the proper formatting
	InitialiseLogging();
	

	
	InitialiseVersion();
	CreateLogFiles();

	g_pCrashHandler = new CCrashHandler();
	bool bAllFatal = strstr(GetCommandLineA(), "-crash_handle_all") != NULL;
	g_pCrashHandler->SetAllFatal(bAllFatal);

	// determine if we are in vanilla-compatibility mode
	g_pVanillaCompatibility = new VanillaCompatibility();
	g_pVanillaCompatibility->SetVanillaCompatibility(strstr(GetCommandLineA(), "-vanilla") != NULL);

	// Write launcher version to log
	StartupLog();

	// Init minhook
	HookSys_Init();

	// Init loadlibrary callbacks
	LibSys_Init();

	g_pServerPresence = new ServerPresenceManager();

	// we don't really need plugins in nscn
	g_pPluginManager = new PluginManager();
	g_pPluginManager->LoadPlugins();

	InitialiseSquirrelManagers();

	// Fix some users' failure to connect to respawn datacenters
	SetEnvironmentVariableA("OPENSSL_ia32cap", "~0x200000200000000");

	curl_global_init_mem(CURL_GLOBAL_DEFAULT, _malloc_base, _free_base, _realloc_base, _strdup_base, _calloc_base);

	// run callbacks for any libraries that are already loaded by now
	CallAllPendingDLLLoadCallbacks();
	ImGuiManager::instance().startInitThread();

	return true;
}
