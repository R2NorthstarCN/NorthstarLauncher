
#include "dohworker.h"
#include "nlohmann/json.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"
#include <curl/curl.h>
#include <chrono>
#include <cstring>
#include "masterserver/masterserver.h"
AUTOHOOK_INIT()

DohWorker* g_DohWorker = new(DohWorker);

size_t DOHCurlWriteToStringBufferCallback(char* contents, size_t size, size_t nmemb, void* userp)
{
	((std::string*)userp)->append((char*)contents, size * nmemb);
	return size * nmemb;
}
std::string DohWorker::ExtractDomain(const std::string& url)
{
	// Regex to match the domain name
	std::regex domainRegex(R"(^(?:https?:\/\/)?([^\/]+))");
	std::smatch match;

	// Apply regex to the URL
	if (std::regex_search(url, match, domainRegex) && match.size() > 1)
	{
		return match[1].str(); // Extract the first capturing group
	}
	return ""; // Return empty string if no match
}

std::string DohWorker::GetDOHResolve(std::string url)
{
	std::string domainname = ExtractDomain(url);
	if (localresolvcache.find(domainname) != localresolvcache.end())
	{
		//spdlog::info("[DOH] Found cache for {}", domainname);
		return localresolvcache[domainname];
	}
	else
	{
		return ResolveDomain(domainname);
	}
}

std::string DohWorker::ResolveDomain(std::string url)
{
	while(is_resolving)
		Sleep(100);

	//spdlog::info("[DOH] Resolving {}", domainname);
	CURL* curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	std::string readBuffer;
	std::string domainname = ExtractDomain(url);
	char* domainnameescaped = curl_easy_escape(curl, domainname.c_str(), domainname.length());
	curl_easy_setopt(
		curl,
		CURLOPT_URL,
		fmt::format(
			"https://223.6.6.6/resolve?name={}&type=1&short=1",
			domainnameescaped)
		.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DOHCurlWriteToStringBufferCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

	CURLcode result = curl_easy_perform(curl);

	if (result == CURLcode::CURLE_OK)
	{
		//spdlog::info(readBuffer);

		nlohmann::json resjson = nlohmann::json::parse(readBuffer);
				
		std::string match = resjson[0].get<std::string>();

		if (!readBuffer.empty())
		{
			g_DohWorker->localresolvcache.insert_or_assign(domainname, readBuffer);
			spdlog::info("[DOH] Successfully resolved {} , result: {}", domainname, match);
			curl_easy_cleanup(curl);
			m_bDohAvailable = true;
			is_resolving = false;
			return match;
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
		if(m_bDohAvailable)
			spdlog::error("[DOH] CURL failed : error {}", curl_easy_strerror(result));
		m_bDohAvailable = false;
	}

	is_resolving = false;
	curl_easy_cleanup(curl);
	return "";
}

ON_DLL_LOAD("client.dll", DohWorkerInitialize, (CModule module))
{
	g_DohWorker->ResolveDomain("nscn.wolf109909.top");
}
