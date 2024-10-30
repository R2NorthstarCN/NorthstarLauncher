#include "igig.h"
#include "kiero.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "implot.h"
#include "dedicated/dedicated.h"
#include "client/igig/ime.h"

typedef HRESULT(__stdcall* fpPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* fpWndProc)(HWND, UINT, WPARAM, LPARAM);

ImGuiManager::ImGuiManager() {}

ImGuiManager& ImGuiManager::instance()
{
	static ImGuiManager IGIG;
	return IGIG;
}

fpPresent originalPresent;
fpWndProc originalWndProc;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT __stdcall hookedWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	
	if (ImeWndProc(hWnd, uMsg, wParam, lParam))
	{
		return true;
	}
	if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
	{
		return true;
	}
	if (ImGui::GetIO().WantCaptureMouse)
	{
		return true;
	}
	return CallWindowProc(originalWndProc, hWnd, uMsg, wParam, lParam);
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

static HRESULT __stdcall hookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	//return originalPresent(pSwapChain, SyncInterval, Flags);
	auto& igig = ImGuiManager::instance();
	if (!igig.init)
	{
		if (igig.pDevice.try_capture(pSwapChain, &IDXGISwapChain::GetDevice))
		{
			igig.pDevice->GetImmediateContext(igig.pContext.put());

			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			igig.window = sd.OutputWindow;

			
			ImGui::CreateContext();
			ImPlot::CreateContext();

			ImGuiIO& io = ImGui::GetIO();
			io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;

			ImGui_ImplWin32_Init(igig.window);
			ImGui_ImplDX11_Init(igig.pDevice.get(), igig.pContext.get());
			

			io.Fonts->AddFontDefault();
			ImFontConfig font_config_chs;
			strcpy_s(font_config_chs.Name, "MicrosoftYaHei");
			font_config_chs.GlyphOffset = ImVec2(0.0, 1.0);
			font_config_chs.MergeMode = true;
			font_config_chs.PixelSnapH = true;
			font_config_chs.OversampleH = 2;
			font_config_chs.OversampleV = 1;
			io.Fonts->AddFontFromFileTTF("c:\\windows\\fonts\\msyh.ttc", 15.0f, &font_config_chs, io.Fonts->GetGlyphRangesChineseFull());

			// ImFontConfig icons_config;
			// strcpy_s(icons_config.Name, "FontAwesome");
			// icons_config.GlyphOffset = ImVec2(0.0, 2.0);
			// icons_config.MergeMode = true;
			// icons_config.PixelSnapH = true;
			// icons_config.OversampleH = 1;
			// io.Fonts->AddFontFromMemoryCompressedBase85TTF(
			// 	FontAwesome_compressed_data_base85, 13.0f, &icons_config, GetGlyphRangesFontAwesome());
			ImGui_ImplDX11_CreateDeviceObjects();
			originalWndProc = (WNDPROC)SetWindowLongPtr(igig.window, GWLP_WNDPROC, (LONG_PTR)hookedWndProc);

			igig.init = true;
		}
		else
		{
			return originalPresent(pSwapChain, SyncInterval, Flags);
		}
	}
	
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	ImGui::SetShortcutRouting(ImGuiMod_Ctrl | ImGuiKey_Tab, 0, ImGuiKeyOwner_NoOwner);
	ImGui::SetShortcutRouting(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Tab, 0, ImGuiKeyOwner_NoOwner);
	
	for (const auto& func : igig.drawFunctions)
	{
		func();
	}
	
	ImGui::EndFrame();
	ImGui::Render();

	

	
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	
	return originalPresent(pSwapChain, SyncInterval, Flags);
}

void ImGuiManager::startInitThread()
{
	std::jthread initThread(
		[]()
		{
			while (!IsDedicatedServer())
			{
				if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					continue;
				}
				if (kiero::bind(8, (void**)&originalPresent, (void*)hookedPresent) != kiero::Status::Success)
				{
					spdlog::error("[IGIG] Kiero bind failed");
					break;
				}
				else
				{
					spdlog::info("[IGIG] Kiero bind succeeded");
					break;
				}
				break;
			}
		});
	initThread.detach();
}

void ImGuiManager::addDrawFunction(std::function<void()>&& f)
{
	drawFunctions.push_back(std::move(f));
}