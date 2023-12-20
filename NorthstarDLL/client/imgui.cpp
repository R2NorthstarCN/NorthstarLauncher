#include "imgui.h"
#include "core/memalloc.h"
#include "imgui/implot.h"
#include "imgui/imgui_internal.h"
#include <span>

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

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void InitImGui()
{
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);
	// Base font
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 13.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
	// Bold headings H2 and H3
	io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyh.ttc", 22.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	IMGUI_FONT_MSYHBD_22 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 22.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	IMGUI_FONT_MSYHBD_28 =
		io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\msyhbd.ttc", 28.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
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
	
	spdlog::info("[IMGUI] Custom fonts loaded.");

	// gamma-correct style
	const auto ToLinear = [](float x)
	{
		if (x <= 0.04045f)
		{
			return x / 12.92f;
		}
		else
		{
			return std::pow((x + 0.055f) / 1.055f, 2.4f);
		}
	};
	for (auto& c : std::span {ImGui::GetStyle().Colors, size_t(ImGuiCol_COUNT)})
	{
		c = {ToLinear(c.x), ToLinear(c.y), ToLinear(c.z), c.w};
	}

ImGuiStyle* style = &ImGui::GetStyle();

	style->WindowPadding = ImVec2(15, 15);
	style->WindowRounding = 5.0f;
	style->FramePadding = ImVec2(5, 5);
	style->FrameRounding = 4.0f;
	style->ItemSpacing = ImVec2(12, 8);
	style->ItemInnerSpacing = ImVec2(8, 6);
	style->IndentSpacing = 25.0f;
	style->ScrollbarSize = 15.0f;
	style->ScrollbarRounding = 9.0f;
	style->GrabMinSize = 5.0f;
	style->GrabRounding = 3.0f;

	style->Colors[ImGuiCol_Text] = ImVec4(0.80f, 0.80f, 0.83f, 1.00f);
	style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_ChildWindowBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.83f, 0.88f);
	style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
	style->Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
	style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.07f, 0.07f, 0.09f, 1.00f);
	style->Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_ComboBg] = ImVec4(0.19f, 0.18f, 0.21f, 1.00f);
	style->Colors[ImGuiCol_CheckMark] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.80f, 0.83f, 0.31f);
	style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	style->Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.09f, 0.12f, 1.00f);
	style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_Column] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	//style->Colors[ImGuiCol_ColumnHovered] = ImVec4(0.24f, 0.23f, 0.29f, 1.00f);
	//style->Colors[ImGuiCol_ColumnActive] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.56f, 0.56f, 0.58f, 1.00f);
	style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.06f, 0.05f, 0.07f, 1.00f);
	//style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
	//style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
	//style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
	style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
	style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
	style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
	//style->Colors[ImGuiCol_ModalWindowDarkening] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);
	imgui_isready = true;
}

bool imgui_ready()
{
	return imgui_isready;
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
