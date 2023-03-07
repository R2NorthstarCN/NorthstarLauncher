#include "core/hooks.h"
#include <windows.h>
#include <winsock2.h>
AUTOHOOK_INIT()

int(WSAAPI* psendto)(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen) = NULL;
int(WSAAPI* precvfrom)(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen) = NULL;

std::string addr2str(const sockaddr* addr)
{
	switch (addr->sa_family)
	{
	case AF_INET:
	{
		auto realAddr = (sockaddr_in*)addr;
		char addrStr[17] {'/0'};
		inet_ntop(AF_INET, (const void*)(&realAddr->sin_addr), addrStr, 17);
		return std::string(addrStr) + ":" + std::to_string(ntohs(realAddr->sin_port));
	}
	case AF_INET6:
	{
		auto realAddr = (sockaddr_in6*)addr;
		char addrStr[47] {'/0'};
		inet_ntop(AF_INET6, (const void*)(&realAddr->sin6_addr), addrStr, 47);
		return "[" + std::string(addrStr) + "]::" + std::to_string(ntohs(realAddr->sin6_port));
	}
	default:
		return fmt::format("unknown sa_family: {}", addr->sa_family);
	}
}

int WSAAPI mysendto(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen)
{
	auto result = psendto(s, buf, len, flags, to, tolen);
	if (result != 0 && result != SOCKET_ERROR)
	{
		printf("[SOCKET - %d] send to %s\n", s, addr2str(to).c_str());
	}
	return result;
}


int WSAAPI myrecvfrom(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen)
{
	auto result = precvfrom(s, buf, len, flags, from, fromlen);
	if (result != 0 && result != SOCKET_ERROR)
	{
		printf("[SOCKET - %d] recv from %s\n", s, addr2str(from).c_str());
	}
	return result;
}

bool EnableWSAHooks()
{
	LPVOID sendtoptr = NULL;
	// Create a hook for Ws2_32.send
	if (MH_CreateHookApiEx(L"Ws2_32", "sendto", mysendto, (LPVOID*)&psendto,&sendtoptr) != MH_OK)
	{
		return false;
	}
	if (MH_EnableHook(sendtoptr) != MH_OK)
	{
		return false;
	}
	LPVOID recvfromptr = NULL;
	// Create a hook for Ws2_32.recv
	if (MH_CreateHookApiEx(L"Ws2_32", "recvfrom", myrecvfrom, (LPVOID*)&precvfrom,&recvfromptr) != MH_OK)
	{
		return false;
	}
	if (MH_EnableHook(recvfromptr) != MH_OK)
	{
		return false;
	}
	// Enable the hooks.
	

	return true;
}

ON_DLL_LOAD("engine.dll", WSAHOOKS, (CModule module))
{
	spdlog::info("[WSA] init hooks called!");
	bool success = EnableWSAHooks();

	spdlog::info("[WSA] WSAHOOKSPLACEMENT: {} ", success);
}
