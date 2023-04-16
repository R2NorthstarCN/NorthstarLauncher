#include "pch.h"
#include "vguihooks.h"
#include "dedicated/dedicated.h"

AUTOHOOK_INIT()

SourceInterface<vgui::ISurface>* m_vguiSurface;
SourceInterface<IMatSystemSurface>* m_matSystemSurface;
SourceInterface<vgui::IPanel>* m_iPanel;

std::wstring utf8_to_wide(const std::string_view& str)
{
	int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), nullptr, 0);
	std::wstring utf16_str(size, '\0');
	MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()), &utf16_str[0], size);
	return std::move(utf16_str);
}

std::string wide_to_utf8(const std::wstring_view& wstr)
{
	int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), nullptr, 0, nullptr, nullptr);
	std::string utf8_str(utf8_size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.length()), (char*)&utf8_str[0], utf8_size, nullptr, nullptr);
	return std::move(utf8_str);
}

class CGameSettings
{
  public:
	char unknown1[92];
	int isChatEnabled;
};

CGameSettings** localGameSettings;

AUTOHOOK(CClientVGUI__ShowFPS, client.dll + 0x34D980, __int64, __fastcall, (__int64 a1))
{
	auto result = CClientVGUI__ShowFPS(a1);
	return result;
}

AUTOHOOK(CEngineVGUI__Paint, engine.dll + 0x248C60, __int64, __fastcall, (__int64 a1, int a2))
{
	auto result = CEngineVGUI__Paint(a1, a2);
	return result;
}

ON_DLL_LOAD_CLIENT_RELIESON("vguimatsurface.dll", VGUIHOOKS, ConVar, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(vguimatsurface.dll);
	m_vguiSurface = new SourceInterface<vgui::ISurface>("vguimatsurface.dll", "VGUI_Surface031");
	m_matSystemSurface = new SourceInterface<IMatSystemSurface>("vguimatsurface.dll", "VGUI_Surface031");
}
ON_DLL_LOAD_CLIENT("vgui2.dll", IPANELHOOKS, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(vgui2.dll);
	m_iPanel = new SourceInterface<vgui::IPanel>("vgui2.dll", "VGUI_Panel009");
}

ON_DLL_LOAD_CLIENT("engine.dll", CENGINEVGUI_PAINT, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(engine.dll);
}

ON_DLL_LOAD_CLIENT("client.dll", GETCHATSTATUS, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(client.dll);
	localGameSettings = module.Offset(0x11BAA48).As<CGameSettings**>();
}
