#include "pch.h"
#include "vguihooks.h"
#include "inputsystem.h"
#include "shared/netgraph.h"
#include "dedicated/dedicated.h"
AUTOHOOK_INIT()
SourceInterface<vgui::ISurface>* m_vguiSurface;
SourceInterface<IMatSystemSurface>* m_matSystemSurface;
SourceInterface<vgui::IPanel>* m_iPanel;
bool lastChatboxState;
ConVar* Cvar_ns_ime_chatbox_fontidx;
ConVar* Cvar_ns_netgraph;
std::wstring utf8_to_wide(const std::string_view& str)
{
	int size = MultiByteToWideChar(CP_UTF8, MB_COMPOSITE, str.data(), static_cast<int>(str.length()), nullptr, 0);
	std::wstring utf16_str(size, '\0');
	MultiByteToWideChar(CP_UTF8, MB_COMPOSITE, str.data(), static_cast<int>(str.length()), &utf16_str[0], size);
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
void DrawTextOnScreen(std::string text)
{
	// spdlog::info("DRAW");
	(*m_matSystemSurface)->DrawColoredText(5, 960, 500, 255, 255, 255, 255, text.c_str());
}
void RenderIMECandidateList()
{
	if (!localGameSettings || !(*localGameSettings)->isChatEnabled)
	{
		return;
	}
	// spdlog::info("[IME] READ  m_CandidateList.m_pCandidates");
	auto candidateList = m_CandidateList.m_pCandidates;
	if (candidateList.size() == 0)
		return;
	// spdlog::info("[IME] READ VGUI INTERFACE POINTERS");
	auto* ipanel = &**m_iPanel; // (xD)wild pointers
	auto* isurface = &**m_matSystemSurface; // m_vguiSurface; // (xD)less typing later ftw xd true
	auto main_panel = (*m_vguiSurface)->GetEmbeddedPanel();

	int mw, mh;
	ipanel->GetSize(main_panel, mw, mh);
	if (mw == 64 && mh == 24)
		return; // not ready yet

	if (!main_panel)
		return;
	// spdlog::info("[IME] READ  main_panel");
	int count = isurface->GetPopupCount();

	vgui::VPANEL panel = nullptr;
	int x, y, w, h;
	bool found = false;

	for (int i = 0; i < count; i++)
	{
		panel = isurface->GetPopup(i);
		if (!panel)
			continue;
		if (!ipanel->IsVisible(panel))
			continue;
		if (!ipanel->IsPopup(panel))
			continue;
		if (!ipanel->IsKeyBoardInputEnabled(panel))
			continue;
		// spdlog::info("[IME] IPANEL IS VALID");
		auto* name = ipanel->GetName(panel); // IngameTextChat
		auto* classname = ipanel->GetClassName(panel); // CBaseHudChat

		if (strcmp(classname, "CBaseHudChat") == 0) // looks like a single = over the network lol xD
		{
			// spdlog::info("那么： {}, classname： {}", name, classname);

			ipanel->GetPos(panel, x, y);
			ipanel->GetSize(panel, w, h);
			// makes no sense it's negative but okay
			x = std::abs(x);
			y = std::abs(y);
			// spdlog::info("child panel x:{} y:{} w:{} h:{}", x, y, w, h);

			found = true;
			break;
		}
	}

	if (!found)
		return;

	const auto fontIndex = Cvar_ns_ime_chatbox_fontidx->GetInt();
	auto singleCharacterWidth = isurface->DrawTextLen(fontIndex, "A");
	auto prefixWidth = isurface->DrawTextLen(fontIndex, "8. ");

	int x_ = x;
	int y_ = y + h;
	for (int i = 0; i < candidateList.size(); i++)
	{
		// as the function DrawTextLen works properly only for ASCII characters, we instead take the length of widestring bytes and divide
		// by 2 to get amount of characters, then multiply it by standard width of a single "A" character (and also by 1.2 for good measure)
		// auto visualLength = strlen(reinterpret_cast<const char*>(m_CandidateList->m_pCandidates[i].c_str())) / sizeof(wchar_t);
		auto visualLength = candidateList[i].size();
		auto widthPx = singleCharacterWidth * (visualLength * 1.4) + prefixWidth;
		auto str = (fmt::format("{}. ", i + 1) + wide_to_utf8(candidateList[i]));
		// spdlog::info("visuallength:{}", visualLength);
		// spdlog::info("[IME] CAND:{}", str.c_str());
		// spdlog::info("[IME] isurface:{}", (void*)isurface);
		isurface->DrawColoredText(fontIndex, x_, y_, 255, 255, 255, 255, str.c_str());
		// spdlog::info("[IME]DRAWCALLED");
		x_ += 2 + widthPx;
	}
}
void RenderNetGraph()
{
	if (Cvar_ns_netgraph->GetInt() == 0)
		return;
	fmt::format("[PING]")
	(*m_matSystemSurface)->DrawColoredText(5, 960, 500, 255, 255, 255, 255, netgui_str.c_str());
}

AUTOHOOK(CEngineVGUI__Paint, engine.dll + 0x248C60, __int64, __fastcall, (__int64 a1, int a2))
{
	auto result = CEngineVGUI__Paint(a1, a2);
	// add custom vgui draw calls here
	if (m_CandidateList.ImeEnabled)
		RenderIMECandidateList();

	RenderNetGraph();
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
	Cvar_ns_ime_chatbox_fontidx = new ConVar("ns_ime_chatbox_fontidx", "17", FCVAR_NONE, "");
	Cvar_ns_netgraph = new ConVar("net_graph", "0", FCVAR_NONE, "");
}
