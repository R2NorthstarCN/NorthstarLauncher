#pragma once

#include <d3d11.h>

#include "imgui/kiero.h"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

typedef HRESULT(__stdcall* Present)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR;

namespace ImGui
{
	void TableCellHeaderBg();
	void TableCellValueBg();
} // namespace ImGui

typedef void imgui_draw(ID3D11Device*  pDevice);

extern std::vector<imgui_draw*> draw_functions;

void imgui_setup();

void imgui_add_draw(imgui_draw* func);
