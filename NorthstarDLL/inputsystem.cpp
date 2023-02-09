#include "pch.h"
#include "inputsystem.h"
#include "logging/sourceconsole.h"
#include "dedicated/dedicated.h"
#include "core/tier0.h"
AUTOHOOK_INIT()

bool m_fullscreen = true;
bool m_alphaMode = true;

typedef unsigned char byte;
// typedef __int64 (*InputStuffType)(__int64 a1, int a2, int a3, int a4, int a5, int a6);
//  InputStuffType RandomInputFunction;
CandidateList m_CandidateList {};
void onCandidateList(CandidateList* list)
{
	// spdlog::info("[IME] Candidate size: {}", list->m_lPageSize);
	// for (int i = 0; i < list->m_lPageSize; i++)
	//{
	//	std::wstring cand = list->m_pCandidates[i];
	//  spdlog::info("[IME] Candidate list[{}]: {}", i, wide_to_utf8(cand));
	//}
}

void handleCandlist(HIMC m_context)
{
	// m_CandidateList->Reset();
	std::vector<std::wstring> newCands;
	DWORD size = ImmGetCandidateListW(m_context, 0, NULL, 0);

	if (size)
	{
		LPCANDIDATELIST candlist = (LPCANDIDATELIST)_malloca(size);
		if (candlist == nullptr)
		{
			return;
		}
		size = ImmGetCandidateListW(m_context, 0, candlist, size);

		DWORD pageSize = candlist->dwPageSize;
		DWORD candCount = candlist->dwCount; // if greater than one, then contains vaild strings

		if (pageSize > 0 && candCount > 1)
		{
			DWORD pageStart = candlist->dwPageStart;
			DWORD pageEnd = pageStart + pageSize;

			newCands.resize(pageSize);

			for (size_t i = 0; i < pageSize; i++)
			{
				LONG_PTR pStrStart = (LONG_PTR)candlist + candlist->dwOffset[i + pageStart];
				LONG_PTR pStrEnd = (LONG_PTR)candlist + (i + pageStart + 1 < candCount ? candlist->dwOffset[i + pageStart + 1] : size);
				auto len = pStrEnd - pStrStart;

				newCands[i].assign(reinterpret_cast<wchar_t*>(pStrStart), len / sizeof(wchar_t));
			}
		}
	}
	// m_CandidateList->m_pCandidates.swap(newCands);
	m_CandidateList.m_mutex.lock();
	m_CandidateList.m_pCandidates = newCands;
	m_CandidateList.m_mutex.unlock();
	// m_sigCandidateList(m_CandidateList);
}

ON_DLL_LOAD("inputsystem.dll", IMESUPPORT, (CModule module))
{
	if (IsDedicatedServer() || !Tier0::CommandLine()->CheckParm("-experimentalimesupport"))
	{
		spdlog::info("[IME] Disabling inputsystem hooks");
		m_CandidateList.ImeEnabled = false;
		return;
	}
	// RandomInputFunction = module.Offset(0x7EC0).As<InputStuffType>();
	AUTOHOOK_DISPATCH();
	m_CandidateList.ImeEnabled = true;
}

// clang-format off
AUTOHOOK(WndProc, inputsystem.dll + 0x8B80,
LRESULT,__fastcall, (__int64 a1, HWND hwnd, UINT msg, WPARAM wparam, unsigned __int64 lparam))
// clang-format on
{
	if (msg == WM_IME_NOTIFY)
	{
		HIMC m_context = ImmGetContext(hwnd);
		if (!m_context)
		{
			spdlog::error("[IME] oh no");
			return WndProc(a1, hwnd, msg, wparam, lparam);
		}
		// spdlog::error("[IME] HWND IS {}");
		if (g_pSourceGameConsole && (*g_pSourceGameConsole)->IsConsoleVisible())
		{
			ImmReleaseContext(hwnd, m_context);
			return WndProc(a1, hwnd, msg, wparam, lparam);
		}
		switch (wparam)
		{
		case IMN_CHANGECANDIDATE:
			handleCandlist(m_context);
			break;
		case IMN_OPENCANDIDATE:
			spdlog::info("[IME] IMN_OPENCANDIDATE");
			m_CandidateList.Reset();
			spdlog::info("[IME] m_CandidateList.Reset()");
			break;
		case IMN_CLOSECANDIDATE:
			spdlog::info("[IME] IMN_CLOSECANDIDATE");
			m_CandidateList.Reset();
			break;
		default:
			break;
		}
		ImmReleaseContext(hwnd, m_context);
		// spdlog::info("[IME] ImmReleaseContext()");
	}
	else if (msg == WM_IME_ENDCOMPOSITION)
	{
		m_CandidateList.Reset();
	}
	else if (msg == WM_IME_SETCONTEXT && wparam == 1) // 1 means window is active
	{
		HIMC m_context = ImmGetContext(hwnd);
		if (m_context)
		{
			ImmAssociateContext(hwnd, m_context);
			ImmReleaseContext(hwnd, m_context);
		}
		else
		{
			spdlog::error("[IME] oh no, no cuntext in WM_IME_SETCONTEXT");
			// ImmReleaseContext(hwnd, m_context);
		}
	}
	// spdlog::info("[IME] WndProc");
	return WndProc(a1, hwnd, msg, wparam, lparam);
}
