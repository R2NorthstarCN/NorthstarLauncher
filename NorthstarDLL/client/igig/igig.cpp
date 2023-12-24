#include "igig.h"
#include "imgui/kiero.h"

typedef HRESULT(__stdcall* fpPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* fpWndProc)(HWND, UINT, WPARAM, LPARAM);

IgIg::IgIg() {}

IgIg::~IgIg()
{
	mainRenderTargetView->Release();
}

IgIg& IgIg::instance()
{
	static IgIg IGIG;
	return IGIG;
}

fpPresent originalPresent;
fpWndProc originalWndProc;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT __stdcall hookedWndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
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

static HRESULT __stdcall hookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	auto& igig = IgIg::instance();
	if (!igig.init)
	{
		if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&igig.pDevice)))
		{
			igig.pDevice->GetImmediateContext(&igig.pContext);
			DXGI_SWAP_CHAIN_DESC sd;
			pSwapChain->GetDesc(&sd);
			igig.window = sd.OutputWindow;
			ID3D11Texture2D* pBackBuffer;
			pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			igig.pDevice->CreateRenderTargetView(pBackBuffer, NULL, &igig.mainRenderTargetView);
			pBackBuffer->Release();



			originalWndProc = (WNDPROC)SetWindowLongPtr(igig.window, GWLP_WNDPROC, (LONG_PTR)hookedWndProc);

			igig.init = true;
		}
		else
		{
			return originalPresent(pSwapChain, SyncInterval, Flags);
		}
			
	}
}

void IgIg::startInitThread()
{
	std::jthread initThread(
		[]()
		{
			while (true)
			{
				if (kiero::init(kiero::RenderType::D3D11) != kiero::Status::Success)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1000));
					continue;
				}
				if (kiero::bind(8, (void**)&originalPresent, hookedPresent) != kiero::Status::Success)
				{
					spdlog::error("[IGIG] Kiero bind failed");
					break;
				}
			}
		});
	initThread.detach();
}

void IgIg::addDrawFunction(std::function<void()>&& f)
{
	drawFunctions.push_back(std::move(f));
}
