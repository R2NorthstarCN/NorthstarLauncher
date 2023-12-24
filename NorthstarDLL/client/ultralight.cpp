#include "core/hooks.h"
#include "client/imgui.h"

#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include <d3d11.h>

using namespace ultralight;

ConVar* Cvar_ul_test;

static bool ul_inited = false;

AUTOHOOK_INIT()

RefPtr<Renderer> renderer;

void Init() {
	Config config;
	Platform::instance().set_config(config);
	Platform::instance().set_font_loader(GetPlatformFontLoader());
	Platform::instance().set_file_system(GetPlatformFileSystem("."));
	Platform::instance().set_logger(GetDefaultLogger("ultralight.log"));

	renderer = Renderer::Create();
}

RefPtr<View> view;

void CreateView()
{
	ViewConfig view_config;
	view_config.is_accelerated = false;

	view = renderer->CreateView(500, 500, view_config, nullptr);

	view->LoadURL("https://www.baidu.com");
}

ID3D11ShaderResourceView* srv;

static void draw_ul(ID3D11Device* device)
{
	if (!Cvar_ul_test->GetBool())
	{
		return;
	}

	if (!ul_inited)
	{
		Init();
		CreateView();

		ul_inited = true;
	}

	renderer->Update();

	if (!ImGui::Begin("UL"))
	{
		ImGui::End();
		return;
	}

	if (srv != NULL)
	{
		srv->Release();
	}

	renderer->Render();

	BitmapSurface* surface = (BitmapSurface*)view->surface();
	RefPtr<Bitmap> bitmap = surface->bitmap();
	auto pixels = bitmap->LockPixelsSafe();

	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = bitmap->width();
	desc.Height = bitmap->height();
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = pixels.data();
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	auto textureResult = device->CreateTexture2D(&desc, &subResource, &pTexture);

	if (textureResult != S_OK || pTexture == NULL)
	{
		spdlog::error("[UL] CreateTexture2D failed: 0x{:X}", (unsigned long long)textureResult);
		
		return;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	device->CreateShaderResourceView(pTexture, &srvDesc, &srv);
	pTexture->Release();

	ImGui::Image((void*)srv, ImVec2(desc.Width, desc.Height));

	ImGui::End();
}

ON_DLL_LOAD("client.dll", ULTRALIGHT, (CModule module))
{
	Cvar_ul_test = new ConVar("ul_test", "0", FCVAR_NONE, "ultralight test");

	imgui_add_draw(draw_ul);
}
