#pragma once

#include <d3d11.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

class IgIg
{
  public:
	HWND window = NULL;
	ID3D11Device* pDevice = NULL;
	ID3D11DeviceContext* pContext = NULL;
	ID3D11RenderTargetView* mainRenderTargetView = NULL;
	bool init = false;

	static IgIg& instance();

	~IgIg();

	void startInitThread();
	void addDrawFunction(std::function<void()>&& f);

  private:
	std::vector<std::function<void()>> drawFunctions;

	IgIg();
};
