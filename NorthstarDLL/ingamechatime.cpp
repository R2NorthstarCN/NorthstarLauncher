#include "pch.h"
#include "ingamechatime.h"
#include "inputsystem.h"
#include "dedicated/dedicated.h"
AUTOHOOK_INIT()
SourceInterface<vgui::ISurface>* m_vguiSurface;
SourceInterface<IMatSystemSurface>* m_matSystemSurface;
SourceInterface<vgui::IPanel>* m_iPanel;
bool lastChatboxState;
ConVar* Cvar_ns_ime_chatbox_fontidx;
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
	auto candidateList = m_CandidateList.m_pCandidates.load();
	if (candidateList->size() == 0)
		return;
	// int startx = 0;
	/*int y = 850;
	for (int i = 0; i < m_CandidateList->m_lPageSize; i++)
	{
		(*m_matSystemSurface)
			->DrawColoredText(
				8,
				90,
				y + i * 17,
				255,
				255,
				255,
				255,
				(fmt::format("{}. ", i + 1) + wide_to_utf8(m_CandidateList->m_pCandidates[i])).c_str());
	}*/
	// DrawTextOnScreen(
	// fmt::format("CLASSNAME: {}", (*m_iPanel)->GetClassNameW((*m_iPanel)->GetCurrentKeyFocus((*m_vguiSurface)->GetEmbeddedPanel())))); //
	// 摈斥莅临 延伸 小米 操蛋 华为

	// spdlog::info("vguimatsurface.dll = {}", (void*)GetModuleHandleA("vguimatsurface"));
	// spdlog::info("m_vguiSurface = {}", (void*)((uintptr_t)(&*(*m_vguiSurface)) - (uintptr_t)GetModuleHandleA("vguimatsurface")));
	// spdlog::info("*m_vguiSurface = {}", (void*)(*(uintptr_t*)(&*(*m_vguiSurface)) - (uintptr_t)GetModuleHandleA("vguimatsurface")));

	auto* ipanel = &**m_iPanel; // (xD)wild pointers
	auto* isurface = &**m_matSystemSurface; // m_vguiSurface; // (xD)less typing later ftw xd true
	auto main_panel = (*m_vguiSurface)->GetEmbeddedPanel();

	int mw, mh;
	ipanel->GetSize(main_panel, mw, mh);
	if (mw == 64 && mh == 24)
		return; // not ready yet

	if (!main_panel)
		return;

	/* auto keyFocus = (*m_iPanel)->GetCurrentKeyFocus(main_panel);
	spdlog::info("keyFocus = {}", (void*)keyFocus);
	if (keyFocus)
	{
		spdlog::info("那么： {}, classname： {}", (*m_iPanel)->GetName(keyFocus), (*m_iPanel)->GetClassNameW(keyFocus));
		int x, y;
		(*m_iPanel)->GetPos(keyFocus, x, y);
		spdlog::info("x:{}, y{}", x, y);
	}*/

	int count = isurface->GetPopupCount();
	// spdlog::info("popup count: {}", count);

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

		auto* name = ipanel->GetName(panel); // IngameTextChat
		auto* classname = ipanel->GetClassName(panel); // CBaseHudChat
		/*spdlog::info(
			"那么： {}, classname： {} | vis:{} pop:{} kbd:{}",
			name,
			classname,
			ipanel->IsVisible(panel),
			ipanel->IsPopup(panel),
			ipanel->IsKeyBoardInputEnabled(panel));*/

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
	for (int i = 0; i < candidateList->size(); i++)
	{
		// as the function DrawTextLen works properly only for ASCII characters, we instead take the length of widestring bytes and divide
		// by 2 to get amount of characters, then multiply it by standard width of a single "A" character (and also by 1.2 for good measure)
		// auto visualLength = strlen(reinterpret_cast<const char*>(m_CandidateList->m_pCandidates[i].c_str())) / sizeof(wchar_t);
		auto visualLength = candidateList->at(i).size();
		auto widthPx = singleCharacterWidth * (visualLength * 1.4) + prefixWidth;
		auto str = (fmt::format("{}. ", i + 1) + wide_to_utf8(candidateList->at(i)));
		// spdlog::info("visuallength:{}", visualLength);
		isurface->DrawColoredText(fontIndex, x_, y_, 255, 255, 255, 255, str.c_str());
		x_ += 2 + widthPx;
	}
}

AUTOHOOK(CEngineVGUI__Paint, engine.dll + 0x248C60, __int64, __fastcall, (__int64 a1, int a2))
{
	auto result = CEngineVGUI__Paint(a1, a2);
	RenderIMECandidateList();
	return result;
}
ON_DLL_LOAD_CLIENT_RELIESON("vguimatsurface.dll", VGUIHOOKS, ConVar, (CModule module))
{
	if (IsDedicatedServer())
	{
		spdlog::info("[IME] Disabling vguimatsurface hooks for DEDICATED");
		return;
	}
	AUTOHOOK_DISPATCH();
	m_vguiSurface = new SourceInterface<vgui::ISurface>("vguimatsurface.dll", "VGUI_Surface031");
	m_matSystemSurface = new SourceInterface<IMatSystemSurface>("vguimatsurface.dll", "VGUI_Surface031");
}
ON_DLL_LOAD_CLIENT("vgui2.dll", IPANELHOOKS, (CModule module))
{
	if (IsDedicatedServer())
	{
		spdlog::info("[IME] Disabling vguiipanel hooks for DEDICATED");
		return;
	}
	m_iPanel = new SourceInterface<vgui::IPanel>("vgui2.dll", "VGUI_Panel009");
}

ON_DLL_LOAD_CLIENT("client.dll", GETCHATSTATUS, (CModule module))
{
	if (IsDedicatedServer())
	{
		spdlog::info("[IME] Disabling ime registration for DEDICATED");
		return;
	}
	localGameSettings = module.Offset(0x11BAA48).As<CGameSettings**>();
	Cvar_ns_ime_chatbox_fontidx = new ConVar("ns_ime_chatbox_fontidx", "17", FCVAR_NONE, "");
}
