#include "pch.h"
#include "../IngameIME/BaseIME.h"
#include "../IngameIME/IMM.cpp"
#include "../IngameIME/TSF.cpp"
LONG lCaretPos;
LONG pageCount;
std::shared_ptr<std::wstring[]> candidates;
std::wstring wstrComposition;
std::wstring wstrTextContent;
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
void CALLBACK onCandidateList(libtf::CandidateList* list)
{
	pageCount = list->m_lPageSize;
	candidates = list->m_pCandidates;
	for (int i = 0; i <= pageCount; i++)
	{
		std::wstring line = candidates[i];
		spdlog::info("[IME] Received candidate: {}", wide_to_utf8(line));
	}
}

void CALLBACK onComposition(libtf::CompositionEventArgs* args)
{
	switch (args->m_state)
	{
	case libtf::CompositionState::StartComposition:
	case libtf::CompositionState::EndComposition:
		wstrComposition.clear();
		lCaretPos = 0;
		break;
	case libtf::CompositionState::Commit:
		wstrTextContent += args->m_strCommit;
		break;
	case libtf::CompositionState::Composing:
		wstrComposition = args->m_strComposition;
		lCaretPos = args->m_lCaretPos;
		break;
	default:
		break;
	}
}

void CALLBACK onGetTextExt(PRECT prect)
{
	// i don't know (cry)
}

void CALLBACK onAlphaMode(BOOL isAlphaMode)
{
	// what even is this
	//  notify if ime in Alphanumeric input mode
	//  InvalidateRect(hWnd, NULL, NULL);
}
ON_DLL_LOAD_CLIENT("client.dll", IngameIMEInitialize, (CModule module))
{
	spdlog::info("[IME] Registering IME");
	IngameIME::BaseIME* api = new IngameIME::IMM();
	api->m_sigAlphaMode = onAlphaMode;
	api->m_sigComposition = onComposition;
	api->m_sigCandidateList = onCandidateList;
	api->m_sigGetTextExt = onGetTextExt;
	api->Initialize(GetActiveWindow());
	api->setState(TRUE);
	api->setFullScreen(TRUE);
}
