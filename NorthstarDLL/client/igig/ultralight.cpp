#include "core/hooks.h"
#include "client/igig/igig.h"

#include <Ultralight/Ultralight.h>
#include <AppCore/Platform.h>
#include <d3d11.h>
#include "imgui/imgui.h"
#include <imgui/imgui_internal.h>

using namespace ultralight;

ConVar* Cvar_ul_test;

static bool ul_inited = false;

AUTOHOOK_INIT()

RefPtr<Renderer> renderer;
static std::unordered_map<ImGuiMouseButton, MouseEvent::Button> mouseButtonMap;

void Init()
{
	Config config;
	Platform::instance().set_config(config);
	Platform::instance().set_font_loader(GetPlatformFontLoader());
	Platform::instance().set_file_system(GetPlatformFileSystem("."));
	Platform::instance().set_logger(GetDefaultLogger("ultralight.log"));

	renderer = Renderer::Create();

	mouseButtonMap.insert({ImGuiMouseButton_Left, MouseEvent::Button::kButton_Left});
	mouseButtonMap.insert({ImGuiMouseButton_Middle, MouseEvent::Button::kButton_Middle});
	mouseButtonMap.insert({ImGuiMouseButton_Right, MouseEvent::Button::kButton_Right});
}

RefPtr<View> view;

void CreateView()
{
	ViewConfig view_config;
	view_config.is_accelerated = false;
	view_config.enable_images = true;
	view_config.enable_javascript = true;

	view = renderer->CreateView(1024, 768, view_config, nullptr);

	view->LoadURL("file:///inspector/Main.html");
	//view->LoadURL("https://www.oschina.net/");
}

winrt::com_ptr<ID3D11ShaderResourceView> srv;
uint32_t srvWidth, srvHeight;

static void draw_ul()
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
	ImGui::SetWindowSize(ImVec2(1200, 780));

	if (srv)
	{
		srv = nullptr;
	}

	renderer->Render();

	BitmapSurface* surface = (BitmapSurface*)view->surface();

	if (!surface->dirty_bounds().IsEmpty())
	{
		RefPtr<Bitmap> bitmap = surface->bitmap();
		auto pixels = bitmap->LockPixelsSafe();

		auto& igig = ImGuiManager::instance();

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

		winrt::com_ptr<ID3D11Texture2D> pTexture;
		D3D11_SUBRESOURCE_DATA subResource;
		ZeroMemory(&subResource, sizeof(subResource));
		subResource.pSysMem = pixels.data();
		subResource.SysMemPitch = desc.Width * 4;
		subResource.SysMemSlicePitch = 0;
		winrt::check_hresult(igig.pDevice->CreateTexture2D(&desc, &subResource, pTexture.put()));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.MostDetailedMip = 0;

		srv = nullptr;
		igig.pDevice->CreateShaderResourceView(pTexture.get(), &srvDesc, srv.put());
		srvWidth = desc.Width;
		srvHeight = desc.Height;
	}

	if (srv)
	{
		ImGui::ImageButtonEx(
			ImGui::GetID("UL_TEST"),
			srv.get(),
			ImVec2(srvWidth, srvHeight),
			ImVec2(0, 0),
			ImVec2(1, 1),
			ImVec4(0, 0, 0, 0),
			ImVec4(1, 1, 1, 1),
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);

		auto mousePosAbs = ImGui::GetMousePos();
		auto screenPosAbs = ImGui::GetItemRectMin();
		auto mousePosRel = ImVec2(mousePosAbs.x - screenPosAbs.x, mousePosAbs.y - screenPosAbs.y);

		if (ImGui::IsItemHovered())
		{
			view->FireMouseEvent(
				{MouseEvent::Type::kType_MouseMoved, (int)mousePosRel.x, (int)mousePosRel.y, MouseEvent::Button::kButton_None});
		}

		if (ImGui::IsItemFocused())
		{
			for (const auto& entry : mouseButtonMap)
			{
				if (ImGui::IsMouseClicked(entry.first))
				{
					view->FireMouseEvent({MouseEvent::Type::kType_MouseDown, (int)mousePosRel.x, (int)mousePosRel.y, entry.second});
				}
				else if (ImGui::IsMouseReleased(entry.first))
				{
					view->FireMouseEvent({MouseEvent::Type::kType_MouseUp, (int)mousePosRel.x, (int)mousePosRel.y, entry.second});
				}
			}
		}
		else
		{
			for (const auto& entry : mouseButtonMap)
			{
				view->FireMouseEvent({MouseEvent::Type::kType_MouseUp, (int)mousePosRel.x, (int)mousePosRel.y, entry.second});
			}
		}
	}

	ImGui::End();
}

ON_DLL_LOAD("client.dll", ULTRALIGHT, (CModule module))
{
	Cvar_ul_test = new ConVar("ul_test", "0", FCVAR_NONE, "ultralight test");

	ImGuiManager::instance().addDrawFunction(draw_ul);
}
