#include "client/igig/igig.h"
#include "imgui.h"
#include "core/hooks.h"
#include "client/fontawesome.h"
#include "client/igig/ime.h"
#include "core/memalloc.h"
#include "shared/keyvalues.h"

AUTOHOOK_INIT()

#define _QWORD __int64;

typedef void (*MessageFunc_t)(void);
typedef __int64(__fastcall* CInputWin32__InternalKeyTyped_t)(__int64, unsigned __int16);
typedef __int64(__fastcall* VGUI__FindOrAddPanelMessageMap_t)(const char*);
typedef __int64(__fastcall* CUtlVector__MessageMapItem_t__AddToTail_t)(__int64, void*);
typedef __int64(__fastcall* CInputWin32__PostKeyMessage_t)(__int64, KeyValues*);
typedef __int64(__fastcall* Panel__LocalToScreen_t)(__int64, DWORD*, DWORD*);
typedef __int64(__fastcall* Panel__GetSize_t)(__int64, DWORD*, DWORD*);
typedef __int64(__fastcall* TextEntry__CursorToPixelSpace_t)(__int64, DWORD, DWORD*, DWORD*);

bool bShouldDrawCandidateList = false;
__int64 CInputWin32__Instance = 0;

CInputWin32__InternalKeyTyped_t CInputWin32__InternalKeyTyped;
VGUI__FindOrAddPanelMessageMap_t VGUI__FindOrAddPanelMessageMap;
CUtlVector__MessageMapItem_t__AddToTail_t CUtlVector__MessageMapItem_t__AddToTail;
CInputWin32__PostKeyMessage_t CInputWin32__PostKeyMessage;
Panel__LocalToScreen_t Panel__LocalToScreen;
Panel__GetSize_t Panel__GetSize;
TextEntry__CursorToPixelSpace_t TextEntry__CursorToPixelSpace;

std::vector<std::wstring> candidates;
std::wstring composite;
DWORD candiX, candiY;

enum DataType_t
{
	DATATYPE_VOID,
	DATATYPE_CONSTCHARPTR,
	DATATYPE_INT,
	DATATYPE_FLOAT,
	DATATYPE_PTR,
	DATATYPE_BOOL,
	DATATYPE_KEYVALUES,
	DATATYPE_CONSTWCHARPTR,
	DATATYPE_UINT64,
	DATATYPE_HANDLE, // It's an int, really
};

struct MessageMapItem_t
{
	const char* name;

	// VC6 aligns this to 16-bytes.  Since some of the code has been compiled with VC6,
	// we need to enforce the alignment on later compilers to remain compatible.
	char pad1[8] = {0};
	MessageFunc_t func;
	char pad2[8] = {0};

	int numParams;

	DataType_t firstParamType;
	const char* firstParamName;

	DataType_t secondParamType;
	const char* secondParamName;

	int nameSymbol;
	int firstParamSymbol;
	int secondParamSymbol;
};

std::string convert_from_wstring(const std::wstring& wstr)
{
	int num_chars = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), NULL, 0, NULL, NULL);
	std::string strTo;
	if (num_chars > 0)
	{
		strTo.resize(num_chars);
		int real_nums = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.length(), &strTo[0], num_chars, NULL, NULL);
		strTo.resize(real_nums);
	}
	return strTo;
}

bool ImeWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_IME_COMPOSITION:
	{
		HIMC hIMC = ImmGetContext(hWnd);
		if (!hIMC)
		{
			spdlog::error("[IME] Cannot get IMC context");
			return false;
		}
		if (lParam & GCS_RESULTSTR)
		{
			LONG dwSize = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, NULL, 0);
			if (dwSize % 2 != 0)
			{
				dwSize += 1;
			}
			dwSize /= 2;

			WCHAR* hStr = new WCHAR[dwSize];
			LONG realSize = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, hStr, dwSize * 2);
			realSize /= 2;

			if (ImGui::GetIO().WantCaptureKeyboard)
			{
				for (int i = 0; i < realSize; i++)
				{
					ImGui::GetIO().AddInputCharacterUTF16(hStr[i]);
				}
			}
			else
			{
				for (int i = 0; i < realSize; i++)
				{
					CInputWin32__InternalKeyTyped(CInputWin32__Instance, hStr[i]);
				}
			}

			delete[] hStr;
		}
		if (lParam & GCS_COMPSTR)
		{
			LONG dwSize = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, NULL, 0);
			if (dwSize % 2 != 0)
			{
				dwSize += 1;
			}
			dwSize /= 2;

			WCHAR* hStr = new WCHAR[dwSize];
			LONG realSize = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, hStr, dwSize * 2);
			realSize /= 2;

			composite = std::wstring(hStr, hStr + realSize);

			delete[] hStr;
		}
		ImmReleaseContext(hWnd, hIMC);
		return true;
	}
	case WM_IME_NOTIFY:
	{
		switch (wParam)
		{
		case IMN_OPENCANDIDATE:
		case IMN_CHANGECANDIDATE:
		{
			bShouldDrawCandidateList = true;

			HIMC hIMC = ImmGetContext(hWnd);
			if (!hIMC)
			{
				spdlog::error("[IME] Cannot get IMC context");
				return false;
			}

			LPCANDIDATELIST lpCandList = NULL;
			DWORD dwIndex = 0;
			DWORD dwBufLen = ImmGetCandidateList(hIMC, dwIndex, NULL, 0);
			if (dwBufLen)
			{
				lpCandList = (LPCANDIDATELIST)malloc(dwBufLen);
				dwBufLen = ImmGetCandidateList(hIMC, dwIndex, lpCandList, dwBufLen);
			}

			if (lpCandList)
			{
				candidates.clear();

				DWORD dwSelection = lpCandList->dwSelection;
				DWORD dwCount = lpCandList->dwCount;
				DWORD dwPageStart = lpCandList->dwPageStart;
				DWORD dwPageSize = lpCandList->dwPageSize;
				for (int i = dwPageStart; i < dwPageStart + dwPageSize; i++)
				{
					LPWSTR lpCandiString = (LPWSTR)((uintptr_t)lpCandList + lpCandList->dwOffset[i]);
					candidates.push_back(std::wstring(lpCandiString));
				}
				free(lpCandList);
			}

			ImmReleaseContext(hWnd, hIMC);

			CInputWin32__PostKeyMessage(CInputWin32__Instance, new KeyValues("DoIMEUpdateCandidateWindowPos"));

			return true;
		}
		case IMN_CLOSECANDIDATE:
			// Ignored due to overlapping with WM_IME_ENDCOMPOSITION
			// when the composite string is partially selected.
			return true;
		}
		return true;
	}
	case WM_IME_STARTCOMPOSITION:
		return true;
	case WM_IME_ENDCOMPOSITION:
		bShouldDrawCandidateList = false;
		candidates.clear();
		composite = L"";
		return true;
	case WM_IME_SETCONTEXT:
		lParam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
		lParam &= ~ISC_SHOWUIGUIDELINE;
		lParam &= ~ISC_SHOWUIALLCANDIDATEWINDOW;
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	case WM_IME_CONTROL:
	case WM_IME_COMPOSITIONFULL:
	case WM_IME_SELECT:
	case WM_IME_CHAR:
	case WM_IME_REQUEST:
	case WM_IME_KEYDOWN:
	case WM_IME_KEYUP:
		return true;
	default:
		return false;
	}
	return false;
}

static void draw_candidates()
{
	if (!bShouldDrawCandidateList)
	{
		return;
	}

	ImGuiWindowFlags window_flags = 0;
	window_flags |= ImGuiWindowFlags_NoDecoration;
	window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
	window_flags |= ImGuiWindowFlags_NoResize;
	window_flags |= ImGuiWindowFlags_NoMove;

	if (!ImGui::Begin("ChatBox", NULL, window_flags))
	{
		ImGui::End();
	}

	auto current_size = ImGui::GetWindowSize();
	auto main_viewport = ImGui::GetMainViewport();
	auto viewport_pos = main_viewport->WorkPos;
	auto viewport_size = main_viewport->WorkSize;
	ImGui::SetWindowPos(ImVec2(viewport_pos.x + candiX, viewport_pos.y + candiY));

	ImGui::Text("%s", convert_from_wstring(composite).c_str());
	ImGui::Separator();

	for (int i = 0; i < candidates.size(); i++)
	{
		ImGui::Text("%d: %s", i + 1, convert_from_wstring(candidates[i]).c_str());
	}
	ImGui::End();
}

void DoIMEUpdateCandidateWindowPos(__int64 textEntry)
{
	DWORD cursorPos = *(DWORD*)(textEntry + 1128);
	DWORD cursorX = 0, cursorY = 0;
	TextEntry__CursorToPixelSpace(textEntry, cursorPos, &cursorX, &cursorY);

	candiX = 0;
	candiY = 0;
	Panel__GetSize(textEntry, &candiX, &candiY);
	candiX = cursorX;
	Panel__LocalToScreen(textEntry, &candiX, &candiY);
}

ON_DLL_LOAD("client.dll", IMETEXTENTRY, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(client.dll)

	VGUI__FindOrAddPanelMessageMap = module.Offset(0x75ACB0).As<VGUI__FindOrAddPanelMessageMap_t>();
	CUtlVector__MessageMapItem_t__AddToTail = module.Offset(0x34BBE0).As<CUtlVector__MessageMapItem_t__AddToTail_t>();
	Panel__LocalToScreen = module.Offset(0x766840).As<Panel__LocalToScreen_t>();
	Panel__GetSize = module.Offset(0x75D680).As<Panel__GetSize_t>();
	TextEntry__CursorToPixelSpace = module.Offset(0x783BE0).As<TextEntry__CursorToPixelSpace_t>();

	MessageMapItem_t entry;
	entry.name = "DoIMEUpdateCandidateWindowPos";
	entry.func = (MessageFunc_t)DoIMEUpdateCandidateWindowPos;
	entry.numParams = 0;
	entry.firstParamName = 0;
	entry.secondParamName = 0;
	entry.firstParamType = DATATYPE_VOID;
	entry.secondParamType = DATATYPE_VOID;
	entry.nameSymbol = 0;
	entry.firstParamSymbol = 0;
	entry.secondParamSymbol = 0;

	auto map = VGUI__FindOrAddPanelMessageMap("TextEntry");
	CUtlVector__MessageMapItem_t__AddToTail(map, &entry);
}

ON_DLL_LOAD("vgui2.dll", CHATBOX, (CModule module))
{
	AUTOHOOK_DISPATCH_MODULE(vgui2.dll)

	ImGuiManager::instance().addDrawFunction(draw_candidates);
	CInputWin32__Instance = (__int64)(module.m_nAddress + 0x121A10);
	CInputWin32__PostKeyMessage = module.Offset(0xEDA0).As<CInputWin32__PostKeyMessage_t>();
	CInputWin32__InternalKeyTyped = module.Offset(0xCFD0).As<CInputWin32__InternalKeyTyped_t>();
}
