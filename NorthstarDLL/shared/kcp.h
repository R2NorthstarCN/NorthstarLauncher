#pragma once

#include <mutex>
#include <shared_mutex>
#include <winsock2.h>
#include <unordered_map>
#include <unordered_set>
#include <concurrent_queue.h>
#include "core/memalloc.h"
#include "ikcp.h"
#include "itimer.h"

const IUINT32 timer_interval = 10;

// Hash and Equal Function Definitions
namespace std
{
	template <> struct equal_to<in6_addr>
	{
		bool operator()(const in6_addr& l, const in6_addr& r) const
		{
			for (int i = 0; i < 8; ++i)
			{
				if (l.u.Word[i] != r.u.Word[i])
				{
					return false;
				}
			}
			return true;
		}
	};

	template <> struct hash<in6_addr>
	{
		size_t operator()(const in6_addr& k) const
		{
			size_t res = 17;
			for (int i = 0; i < 8; ++i)
			{
				res = res * 31 + hash<USHORT>()(k.u.Word[i]);
			}
			return res;
		}
	};

	template <> struct equal_to<sockaddr_in6>
	{
		bool operator()(const sockaddr_in6& l, const sockaddr_in6& r) const
		{
			return equal_to<USHORT>()(l.sin6_port, r.sin6_port) && equal_to<in6_addr>()(l.sin6_addr, r.sin6_addr);
		}
	};

	template <> struct hash<sockaddr_in6>
	{
		size_t operator()(const sockaddr_in6& k) const
		{
			size_t res = 17;
			res = res * 31 + hash<USHORT>()(k.sin6_port);
			res = res * 31 + hash<in6_addr>()(k.sin6_addr);
			return res;
		}
	};
} // namespace std

// Function Definitions

std::string ntop(const sockaddr* addr);

// Custom Type Definitions

const int KCP_NOT_ALTERED = -114;

struct udp_output_userdata
{
	SOCKET socket;
	sockaddr_in6 remote_addr;
};

struct kcp_connection
{
	std::mutex* mutex;
	ikcpcb* kcpcb;
	itimer_evt* update_timer;
};

struct kcp_manager
{
	std::shared_mutex established_connections_mutex;
	std::unordered_map<sockaddr_in6, kcp_connection*> established_connections;

	std::mutex pending_connections_mutex;
	std::unordered_map<in6_addr, std::unordered_set<IUINT32>> pending_connections;

	concurrency::concurrent_queue<std::pair<sockaddr_in6, std::vector<char>>> unaltered_data;

	std::jthread select_thread;

	std::mutex timer_mgr_mutex;
	itimer_mgr timer_mgr;

	SOCKET local_socket = 0;
	unsigned int local_socket_max_msg_size = 0;

	kcp_manager();
	~kcp_manager();

	bool is_initialized();

	int intercept_bind(SOCKET s, const sockaddr* name, int namelen);
	int intercept_sendto(SOCKET socket, const char* buf, int len, const sockaddr* to, int tolen);
	int intercept_recvfrom(SOCKET socket, char* buf, int len, sockaddr* from, int* fromlen);
};

kcp_manager* g_kcp_manager = nullptr;

bool g_kcp_initialized();

kcp_connection* kcp_setup(kcp_manager* kcp_manager, const sockaddr_in6& remote_addr, IUINT32 conv);
