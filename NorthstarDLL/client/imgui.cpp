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

LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ImGuiIO& io = ImGui::GetIO();
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;
	if (io.WantCaptureMouse)
		return true;
	return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

const ImWchar* GetGlyphRangesFontAwesome()
{
	static const ImWchar ranges[] = {
		0xE005,
		0xF8FF,
		0,
	};
	return &ranges[0];
}

void InitImGui()
{
	ImGui::CreateContext();
	ImPlot::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX11_Init(pDevice, pContext);

	io.Fonts->AddFontDefault();

	ImFontConfig icons_config; 
	icons_config.MergeMode = true;
	icons_config.PixelSnapH = true;
	icons_config.OversampleH = 1;
	io.Fonts->AddFontFromMemoryCompressedBase85TTF(FontAwesome_compressed_data_base85, 13.0f, &icons_config, GetGlyphRangesFontAwesome());
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
			spdlog::error("[ImGui] Kiero bind failed, disabling ImGui");
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

void imgui_add_draw(imgui_draw* func)
{
	draw_functions.push_back(func);
}

void imgui_setup()
{
	if (strstr(GetCommandLineA(), "-disableimgui"))
	{
		return;
	}

	std::jthread setup_thread(setup_thread_func);
	setup_thread.detach();
}
