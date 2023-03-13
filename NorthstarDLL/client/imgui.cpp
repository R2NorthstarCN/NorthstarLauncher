#include "imgui.h"
#include <d3d11.h>

static WNDPROC oWndProc = NULL;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
typedef long(__stdcall* Present)(IDXGISwapChain*, UINT, UINT);
static Present oPresent = NULL;

LRESULT CALLBACK hkWindowProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam) > 0)
		return 1L;
	return ::CallWindowProcA(oWndProc, hwnd, uMsg, wParam, lParam);
}

long __stdcall hkPresent11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
	static bool init = false;

	if (!init)
	{
		DXGI_SWAP_CHAIN_DESC desc;
		pSwapChain->GetDesc(&desc);

		ID3D11Device* device;
		pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&device);

		ID3D11DeviceContext* context;
		device->GetImmediateContext(&context);

		oWndProc = (WNDPROC)::SetWindowLongPtr((HWND)desc.OutputWindow, GWLP_WNDPROC, (LONG)hkWindowProc);

		ImGui::CreateContext();
		ImGui_ImplWin32_Init(desc.OutputWindow);
		ImGui_ImplDX11_Init(device, context);

		init = true;
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	char buffer[128];
	memset(buffer, 0, 128);
	sprintf(buffer, "Kiero Dear ImGui Example (%s)", "D3D11");

	ImGui::Begin(buffer);

	ImGui::Text("Hello");
	ImGui::Button("World!");

	ImGui::End();

	ImGui::EndFrame();
	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	return oPresent(pSwapChain, SyncInterval, Flags);
}

void kiero_setup()
{
	std::jthread setup_thread(
		[]()
		{
			while (true)
			{
				if (kiero::init(kiero::RenderType::Auto) == kiero::Status::Success)
				{
					switch (kiero::getRenderType())
					{
					case kiero::RenderType::D3D11:
						if (kiero::bind(8, (void**)&oPresent, hkPresent11) != kiero::Status::Success)
						{
							spdlog::error("[ImGui] Initialize Failed 2");
							return;
						}
						break;
					default:
						spdlog::error("[ImGui] Initialize Failed 1");
						return;
					}
				}
			}
		});
	setup_thread.detach();
}
