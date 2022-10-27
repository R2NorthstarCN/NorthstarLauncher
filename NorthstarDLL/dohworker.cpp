#include "pch.h"
#include "dohworker.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include <chrono>
#include <cstring>
#include "masterserver.h"
AUTOHOOK_INIT()

DohWorker* g_DohWorker = new(DohWorker);

size_t DOHCurlWriteToStringBufferCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}


std::string DohWorker::GetDOHResolve(std::string domainname)
{
	bool stripHeaders = false;
	if ((domainname.find("https") != std::string::npos))
	{
		//spdlog::info("[DOH] Found https like headers in {}", domainname);
		domainname.erase(0, 8);
		stripHeaders = true;
	}
	if ((domainname.find("http") != std::string::npos) && !stripHeaders)
	{
		//spdlog::info("[DOH] Found http like headers in {}", domainname);
		domainname.erase(0, 7);
		stripHeaders = true;
	}
	if (localresolvcache.contains(domainname))
	{
		//spdlog::info("[DOH] Found cache for {}", domainname);
		return localresolvcache[domainname];
	}
	else
	{
		return ResolveDomain(domainname);
	}
}

std::string DohWorker::ResolveDomain(std::string domainname)
{
	while(is_resolving)
		Sleep(100);
	bool stripHeaders = false;
	is_resolving = true;
	if ((domainname.find("https") != std::string::npos))
	{
		//spdlog::info("[DOH] Found https like headers in {}", domainname);
		domainname.erase(0, 8);
		stripHeaders = true;
	}
	if ((domainname.find("http") != std::string::npos) && !stripHeaders)
	{
		//spdlog::info("[DOH] Found http like headers in {}", domainname);
		domainname.erase(0, 7);
		stripHeaders = true;
	}
	//spdlog::info("[DOH] Resolving {}", domainname);
	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	std::string readBuffer;
	char* domainnameescaped = curl_easy_escape(curl, domainname.c_str(), domainname.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format(
			"https://223.5.5.5/resolve?name={}&type=1&short=1",
			domainnameescaped)
		.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DOHCurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		//spdlog::info(readBuffer);
		readBuffer = readBuffer.substr(2, readBuffer.length() - 4);
				
				
		if (!readBuffer.empty())
		{
			g_DohWorker->localresolvcache.insert_or_assign(domainname, readBuffer);
			spdlog::info("[DOH] Successfully resolved {} , result: {}", domainname, readBuffer);
			curl_easy_cleanup(curl);
			m_bDohAvailable = true;
			is_resolving = false;
			return readBuffer;
		}
		else
		{
			spdlog::info("[DOH] Failed resolving {}, Got data error.", domainname);
			curl_easy_cleanup(curl);
			m_bDohAvailable = false;
			is_resolving = false;
			return "";
		}

		//spdlog::error("Failed reading player clantag");
	}
	else
	{
		// DOH failed, we need to fallback to use normal DNS
		spdlog::error("[DOH] CURL failed : error {}", curl_easy_strerror(result));
		m_bDohAvailable = false;
	}

	is_resolving = false;
	curl_easy_cleanup(curl);

}

ON_DLL_LOAD("client.dll", DohWorkerInitialize, (CModule module))
{
	g_DohWorker->ResolveDomain("nscn.wolf109909.top");
}
