#pragma once
#include <winsock2.h>
#include <string>
#include <map>
#include <cstring>

class DohWorker
{
public:
	bool m_bDohAvailable = false;
	bool is_resolving = false;
	std::map<std::string, std::string> localresolvcache;
	void ExecuteDefaults();
	std::string ResolveDomain(std::string domainname);
	std::string GetDOHResolve(std::string domainname);
};

extern DohWorker* g_DohWorker;
