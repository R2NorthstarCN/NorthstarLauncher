#include "imgui.h"
#include "core/memalloc.h"
#include "imgui/implot.h"
#include "imgui/imgui_internal.h"
#include <span>
#include "fontawesome.h"


std::vector<imgui_draw*> draw_functions;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool imgui_isready = false;
Present oPresent;
HWND window = NULL;
WNDPROC oWndProc;
ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView;

ImFont* IMGUI_FONT_MSYH_13 = NULL;
ImFont* IMGUI_FONT_MSYH_16 = NULL;
ImFont* IMGUI_FONT_MSYH_22 = NULL;
ImFont* IMGUI_FONT_MSYH_36 = NULL;
ImFont* IMGUI_FONT_MSYHBD_13 = NULL;
ImFont* IMGUI_FONT_MSYHBD_16 = NULL;
ImFont* IMGUI_FONT_MSYHBD_22 = NULL;
ImFont* IMGUI_FONT_MSYHBD_28 = NULL;

ImFont* FONT_FONTAWESOME = NULL;

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;
	if (io.WantCaptureMouse)
		return true;
	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

static const ImWchar GLYPH_FUCK_YOU[] = {
	0xE005, 0xF8FF,
	0,
};

void InitImGui()
{
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
	io.Fonts->AddFontDefault();
	// Base font
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 13.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	// Bold headings H2 and H3
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 22.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	//IMGUI_FONT_MSYHBD_22 =
	//	io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 22.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	//IMGUI_FONT_MSYHBD_28 =
	//	io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 28.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	/*IMGUI_FONT_MSYH_13 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 13.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	IMGUI_FONT_MSYH_16 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	
	IMGUI_FONT_MSYH_36 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 36.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	IMGUI_FONT_MSYHBD_13 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 13.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	IMGUI_FONT_MSYHBD_16 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 15.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	IMGUI_FONT_MSYHBD_22 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 22.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	IMGUI_FONT_MSYHBD_36 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 36.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());*/

	ImFontConfig icons_config;
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;

	FONT_FONTAWESOME =
		io.Fonts->AddFontFromMemoryCompressedBase85TTF(FontAwesome_compressed_data_base85, 16.0f, &icons_config, &GLYPH_FUCK_YOU[0]);
	io.Fonts->GetGlyphRangesDefault();
}


HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	static bool init = false;
	if (!init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice)))
		{
			pDevice->GetImmediateContext(&pContext);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			window = sd.OutputWindow;
			ID3D11Texture2D* pBackBuffer;
			pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
			pBackBuffer->Release();
			oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
			InitImGui();
			init = true;
		}

		else
			return oPresent(pSwapChain, SyncInterval, Flags);
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetShortcutRouting(ImGuiMod_Ctrl | ImGuiKey_Tab, ImGuiKeyOwner_None);
	ImGui::SetShortcutRouting(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab, ImGuiKeyOwner_None);

	for (const auto& func : draw_functions)
	{
		func(pDevice);
	}

	ImGui::EndFrame();
	ImGui::Render();

	pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	return oPresent(pSwapChain, SyncInterval, Flags);
}

void setup_thread_func()
{
	while (true)
	{
		if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			continue;
		}
		if (kiero::bind(8, (void**)&oPresent, hkPresent) != kiero::Status::Success)
		{
			spdlog::error("[ImGui] bind failed");
			break;
		}
	}
}

void ImGui::TableCellHeaderBg()
{
	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(60, 60, 60, 140));
}

void ImGui::TableCellValueBg()
{
	ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(0, 0, 0, 140));
}

void ImPlot::PushAvgLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 255, 255, 160));
}

void ImPlot::PushPurpleLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(127, 0, 255, 255));
}

void ImPlot::PushRedLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 0, 0, 255));
}

void ImPlot::PushOrangeLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 128, 0, 255));
}

void ImPlot::PushYellowLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(255, 255, 0, 255));
}

void ImPlot::PushLimeLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(128, 255, 0, 255));
}

void ImPlot::PushGreenLineColor()
{
	ImPlot::PushStyleColor(ImPlotCol_Line, IM_COL32(0, 255, 0, 255));
}

void imgui_add_draw(imgui_draw* func)
{
	draw_functions.push_back(func);
}

void imgui_setup()
{
	// ImGui::GetAllocatorFunctions(imgui_malloc, (ImGuiMemFreeFunc*)imgui_free, nullptr);
	if (strstr(GetCommandLineA(), "-disableimgui"))
	{
		spdlog::info("[IMGUI] IMGUI is disabled!");
		return;
	}
	spdlog::info("[IMGUI] IMGUI is enabled! use argument -disableimgui if you find any compatibility issues!");
	std::jthread setup_thread(setup_thread_func);
	setup_thread.detach();
}


