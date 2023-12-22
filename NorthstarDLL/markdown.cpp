#define STB_IMAGE_IMPLEMENTATION
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "client/imgui.h"
#include "imgui_markdown.h"
#include <Windows.h>
#include "Shellapi.h"
#include <string>
#include "stb_image.h"
#include "httplib.h"
#include <unordered_set>
ConVar* Cvar_markdown_draw;
ID3D11Device* g_pd3dDevice = NULL;
void LinkCallback(ImGui::MarkdownLinkCallbackData data_);
inline ImGui::MarkdownImageData ImageCallback(ImGui::MarkdownLinkCallbackData data_);

ImFont* H1 = NULL;
ImFont* H2 = NULL;
ImFont* H3 = NULL;

static ImGui::MarkdownConfig mdConfig;

struct TextureResource
{
	ID3D11ShaderResourceView* texture;
	int size_x;
	int size_y;
};

bool g_RequestingMarkDownUrl = false;

std::map<std::string, TextureResource*> g_StaticTextureResourceCache;
std::mutex g_StaticTextureResourceCacheMutex;

std::unordered_set<std::string> g_StaticImageDownloadHistory;

std::queue<std::string> g_ImageDownloadQueue;
std::mutex g_ImageDownloadQueueMutex;
std::condition_variable g_ImageDownloadQueueCondition;

std::string g_TextCache;

void QueueUrlForDownload(std::string url)
{
	std::thread queue_thread(
		[url]()
		{
			std::lock_guard<std::mutex> lock(g_ImageDownloadQueueMutex);
			g_ImageDownloadQueue.emplace(url);
			g_ImageDownloadQueueCondition.notify_one();
		});
	queue_thread.detach();
}

TextureResource* GetStaticTextureFromUrl(std::string url)
{
	std::vector<unsigned char> emptydata;
	g_StaticTextureResourceCacheMutex.lock();

	if (g_StaticTextureResourceCache.contains(url))
	{
		g_StaticTextureResourceCacheMutex.unlock();
		return g_StaticTextureResourceCache[url];
	}
	else
	{
		g_StaticTextureResourceCacheMutex.unlock();
		// if an image has been downloaded before but not in cache, its likely still in download or failed.
		if (g_StaticImageDownloadHistory.contains(url))
		{
			return NULL;
		}
		spdlog::info("[Markdown] Image {} is not cached. Downloading!", url);
		QueueUrlForDownload(url);
		g_StaticImageDownloadHistory.emplace(url);
		return NULL;
	}
}
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::vector<unsigned char>* data)
{
	size_t totalSize = size * nmemb;
	data->insert(data->end(), static_cast<unsigned char*>(contents), static_cast<unsigned char*>(contents) + totalSize);
	return totalSize;
}
size_t CurlWriteStringBufferCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
	static_cast<std::string*>(userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}
std::string DownloadMOTDFromUrl(const std::string& url)
{
	g_RequestingMarkDownUrl = true;

	if (!g_TextCache.empty())
	{
		return g_TextCache;
	}
	std::string text;
	CURL* curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteStringBufferCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &text);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		struct curl_slist* headers = nullptr;
		headers = curl_slist_append(headers, "User-Agent: AwesomeApp");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			spdlog::error("[Markdown] Failed downloading text from url: {} , Error:{}", url, curl_easy_strerror(res));
			return text;
		}

		curl_easy_cleanup(curl);
	}
	spdlog::info("[Markdown] Successfully downloaded text from url: {}", url);
	g_TextCache = text;
	g_RequestingMarkDownUrl = false;
	return text;
}

std::vector<unsigned char> DownloadPNGFromUrl(const std::string& url)
{
	std::vector<unsigned char> pngData;
	CURL* curl = curl_easy_init();
	if (curl)
	{
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pngData);
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK)
		{
			spdlog::error("[Markdown] Failed downloading PNG from url: {} , Error:{}", url, curl_easy_strerror(res));
			return pngData;
		}

		curl_easy_cleanup(curl);
	}
	spdlog::info("[Markdown] Successfully downloaded PNG from url: {}", url);
	return pngData;
}

bool CreateTextureFromImage(std::vector<unsigned char> pngData, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
	if (pngData.empty())
	{
		return false;
	}
	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load_from_memory(pngData.data(), pngData.size(), &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;

	// Create texture
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = image_width;
	desc.Height = image_height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	ID3D11Texture2D* pTexture = NULL;
	D3D11_SUBRESOURCE_DATA subResource;
	subResource.pSysMem = image_data;
	subResource.SysMemPitch = desc.Width * 4;
	subResource.SysMemSlicePitch = 0;
	g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

	// Create texture view
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	ZeroMemory(&srvDesc, sizeof(srvDesc));

	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Texture2D.MostDetailedMip = 0;
	g_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
	pTexture->Release();

	*out_width = image_width;
	*out_height = image_height;
	stbi_image_free(image_data);

	return true;
}

void InitStaticImageDownloadThread()
{
	std::thread downloader_thread(
		[]()
		{
			while (true)
			{

				std::unique_lock<std::mutex> lock(g_ImageDownloadQueueMutex);
				while (g_ImageDownloadQueue.empty())
				{
					g_ImageDownloadQueueCondition.wait(lock);
				}

				std::string nexturl = g_ImageDownloadQueue.front();
				g_ImageDownloadQueue.pop();
				spdlog::info("[Markdown] Processing next image: {}", nexturl);

				std::thread process_thread(
					[nexturl]()
					{
						std::vector<unsigned char> image = DownloadPNGFromUrl(nexturl);

						if (!image.empty())
						{
							ID3D11ShaderResourceView* my_texture = NULL;
							int my_image_width = 0;
							int my_image_height = 0;

							CreateTextureFromImage(image, &my_texture, &my_image_width, &my_image_height);

							TextureResource* tex = new TextureResource();
							tex->texture = my_texture;
							tex->size_x = my_image_width;
							tex->size_y = my_image_height;
							while (!g_StaticTextureResourceCacheMutex.try_lock())
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(100));
							}
							g_StaticTextureResourceCache.emplace(std::make_pair(nexturl, tex));
							g_StaticTextureResourceCacheMutex.unlock();
						}
						else
						{
							spdlog::error("[Markdown] Error Processing image: {}", nexturl);
						}
					});

				process_thread.detach();
			}
		});

	downloader_thread.detach();
}

void MarkdownRequestUrlUpdate(std::string url)
{
	if (g_RequestingMarkDownUrl)
	{
		spdlog::error("[Markdown] Requsting a page while another page is still being load!");
		return;
	}

	g_TextCache.clear();

	g_TextCache = DownloadMOTDFromUrl(url);
}

void LinkCallback(ImGui::MarkdownLinkCallbackData data_)
{
	std::string url(data_.link, data_.linkLength);
	if (url.starts_with("[external]"))
	{
		url = url.erase(0, 10);
		ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
		return;
	}

	if (!data_.isImage)
	{

		MarkdownRequestUrlUpdate(url);
		return;
	}
}

inline ImGui::MarkdownImageData ImageCallback(ImGui::MarkdownLinkCallbackData data_)
{
	ID3D11ShaderResourceView* my_texture = NULL;
	std::string imageUrl(data_.link, data_.linkLength);
	int my_image_width = 0;
	int my_image_height = 0;

	ImGui::MarkdownImageData imageData;
	imageData.isValid = true;
	imageData.useLinkCallback = false;

	auto texture = GetStaticTextureFromUrl(imageUrl);
	if (texture != NULL)
	{
		my_image_width = texture->size_x;
		my_image_height = texture->size_y;
		imageData.user_texture_id = texture->texture;
	}
	else
	{
		// use random font thingey when image is invalid
		imageData.user_texture_id = ImGui::GetIO().Fonts->TexID;
	}

	imageData.size = ImVec2(my_image_width, my_image_height);

	// For image resize when available size.x > image width, add
	ImVec2 const contentSize = ImGui::GetContentRegionAvail();
	if (imageData.size.x > contentSize.x)
	{
		float const ratio = imageData.size.y / imageData.size.x;
		imageData.size.x = contentSize.x;
		imageData.size.y = contentSize.x * ratio;
	}

	return imageData;
}



void ExampleMarkdownFormatCallback(const ImGui::MarkdownFormatInfo& markdownFormatInfo_, bool start_)
{
	// Call the default first so any settings can be overwritten by our implementation.
	// Alternatively could be called or not called in a switch statement on a case by case basis.
	// See defaultMarkdownFormatCallback definition for furhter examples of how to use it.
	ImGui::defaultMarkdownFormatCallback(markdownFormatInfo_, start_);

	switch (markdownFormatInfo_.type)
	{
	// example: change the colour of heading level 2
	case ImGui::MarkdownFormatType::HEADING:
	{
		if (markdownFormatInfo_.level == 2)
		{
			if (start_)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_Text]);
			}
			else
			{
				ImGui::PopStyleColor();
			}
		}
		break;
	}
	default:
	{
		break;
	}
	}
}

void Markdown(const std::string& markdown_)
{
	// You can make your own Markdown function with your prefered string container and markdown config.
	// > C++14 can use ImGui::MarkdownConfig mdConfig{ LinkCallback, NULL, ImageCallback, ICON_FA_LINK, { { H1, true }, { H2, true }, { H3,
	// false } }, NULL };
	mdConfig.linkCallback = LinkCallback;
	mdConfig.tooltipCallback = NULL;
	mdConfig.imageCallback = ImageCallback;
	mdConfig.linkIcon = NULL;
	mdConfig.headingFormats[0] = {IMGUI_FONT_MSYHBD_28, true};
	mdConfig.headingFormats[1] = {IMGUI_FONT_MSYHBD_22, true};
	mdConfig.headingFormats[2] = {IMGUI_FONT_MSYHBD_22, false};
	mdConfig.userData = NULL;
	mdConfig.formatCallback = ExampleMarkdownFormatCallback;
	ImGui::Markdown(markdown_.c_str(), markdown_.length(), mdConfig);
	// std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
char textbuffer[2048];

void MarkdownExample(ID3D11Device* device)
{
	g_pd3dDevice = device;

	if (Cvar_markdown_draw == nullptr || !Cvar_markdown_draw->GetBool() || g_RequestingMarkDownUrl)
	{
		return;
	}

	if (!g_TextCache.empty())
	{
		Markdown(g_TextCache);
	}
	if (ImGui::InputText(
			"##Input", textbuffer, IM_ARRAYSIZE(textbuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory))
	{
	}
}
ON_DLL_LOAD("client.dll", MARKDOWN, (CModule module))
{
	Cvar_markdown_draw = new ConVar("markdown_draw", "0", FCVAR_NONE, "markdown test");
	InitStaticImageDownloadThread();
	g_TextCache = DownloadMOTDFromUrl("https://sv.wolf109909.top:62500/f/e1678b1e636c4a9bbeff/?dl=1");
	imgui_add_draw(MarkdownExample);
}
