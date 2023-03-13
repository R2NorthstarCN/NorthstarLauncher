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

// FEC

struct fec_packet
{
	char* buf;

	IUINT32 seqid();
	IUINT16 flag();
	char* data();

	~fec_packet()
	{

	}
};

struct fec_element
{
	fec_packet pkt;
	IUINT32 ts;
};

struct fec_decoder
{
	IUINT32 rxlimit;
	IUINT32 data_shards, parity_shards, shard_size;

	
};

// Function Definitions

std::string ntop(const sockaddr* addr);

// Custom Type Definitions

const int KCP_NOT_ALTERED = -114;

struct udp_output_userdata
{
	SOCKET socket;
	sockaddr_in6 remote_addr;
};

struct kcp_manager;

struct kcp_connection
{
	std::mutex* mutex;
	ikcpcb* kcpcb;
	itimer_evt* update_timer;
	IUINT32 last_input;

	IUINT32 last_stats_rotate;
	IUINT64 last_out_segs, last_lost_segs, last_retrans_segs;

	kcp_connection(kcp_manager* kcp_manager, const sockaddr_in6& remote_addr, IUINT32 conv);

	~kcp_connection();
};

struct kcp_stats
{
	IINT32 srtt, rto;

	IUINT64 out_segs, lost_segs, retrans_segs;
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

	IUINT32 timer_interval;

	kcp_manager(IUINT32 timer_interval);
	~kcp_manager();

	bool is_initialized();

	int intercept_bind(SOCKET s, const sockaddr* name, int namelen);
	int intercept_sendto(SOCKET socket, const char* buf, int len, const sockaddr* to, int tolen);
	int intercept_recvfrom(SOCKET socket, char* buf, int len, sockaddr* from, int* fromlen);

	std::vector<std::pair<sockaddr_in6, kcp_stats>> get_stats();
};

extern kcp_manager* g_kcp_manager;

bool g_kcp_initialized();
