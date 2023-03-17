#pragma once

#include "ns_version.h"

#if !NORTHSTAR_DEDICATED_ONLY

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

namespace ImPlot
{
	void PushAvgLineColor();
	void PushPurpleLineColor();
	void PushRedLineColor();
	void PushOrangeLineColor();
	void PushYellowLineColor();
	void PushLimeLineColor();
	void PushGreenLineColor();
} // namespace ImPlot

typedef void imgui_draw();

extern std::vector<imgui_draw*> draw_functions;

void imgui_setup();

void imgui_add_draw(imgui_draw* func);

#endif
