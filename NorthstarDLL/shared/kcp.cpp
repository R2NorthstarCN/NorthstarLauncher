#include "kcp.h"
#include "core/hooks.h"
#include <unordered_set>
#include "dedicated/dedicated.h"
#include "core/convar/concommand.h"
#include "engine/r2engine.h"
#include "client/imgui.h"

kcp_manager* g_kcp_manager = nullptr;

AUTOHOOK_INIT()

// Hooked Original Function Definitions

int(WSAAPI* org_bind)(_In_ SOCKET s, _In_reads_bytes_(namelen) const struct sockaddr FAR* name, _In_ int namelen) = nullptr;
int(WSAAPI* org_sendto)(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen) = nullptr;
int(WSAAPI* org_recvfrom)(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen) = nullptr;

// Hooked Function Definitions

int WSAAPI de_bind(_In_ SOCKET s, _In_reads_bytes_(namelen) const struct sockaddr FAR* name, _In_ int namelen)
{
	auto result = org_bind(s, name, namelen);
	if (result != SOCKET_ERROR)
	{
		if (g_kcp_manager != nullptr)
		{
			auto intercept_result = g_kcp_manager->intercept_bind(s, name, namelen);
			if (intercept_result != KCP_NOT_ALTERED)
			{
				return intercept_result;
			}
		}
	}
	return result;
}

int WSAAPI de_sendto(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen)
{
	if (g_kcp_initialized())
	{
		auto intercept_result = g_kcp_manager->intercept_sendto(s, buf, len, to, tolen);
		if (intercept_result != KCP_NOT_ALTERED)
		{
			return intercept_result;
		}
	}
	auto result = org_sendto(s, buf, len, flags, to, tolen);
	return result;
}

int WSAAPI de_recvfrom(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen)
{
	if (g_kcp_initialized())
	{
		auto intercept_result = g_kcp_manager->intercept_recvfrom(s, buf, len, from, fromlen);
		if (intercept_result != KCP_NOT_ALTERED)
		{
			return intercept_result;
		}
	}
	auto result = org_recvfrom(s, buf, len, flags, from, fromlen);
	return result;
}

bool create_and_enable_hook(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID* ppOriginal)
{
	LPVOID ppTarget = NULL;
	if (MH_CreateHookApiEx(pszModule, pszProcName, pDetour, ppOriginal, &ppTarget) != MH_OK)
	{
		return false;
	}
	if (MH_EnableHook(ppTarget) != MH_OK)
	{
		return false;
	}
	return true;
}

bool enable_wsa_hooks()
{
	return create_and_enable_hook(L"Ws2_32", "bind", de_bind, (LPVOID*)&org_bind) &&
		   create_and_enable_hook(L"Ws2_32", "sendto", de_sendto, (LPVOID*)&org_sendto) &&
		   create_and_enable_hook(L"Ws2_32", "recvfrom", de_recvfrom, (LPVOID*)&org_recvfrom);
}

std::vector<std::string> split(std::string s, std::string delimiter)
{
	size_t pos_start = 0, pos_end, delim_len = delimiter.length();
	std::string token;
	std::vector<std::string> res;

	while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos)
	{
		token = s.substr(pos_start, pos_end - pos_start);
		pos_start = pos_end + delim_len;
		res.push_back(token);
	}

	res.push_back(s.substr(pos_start));
	return res;
}

void ConCommand_kcp_connect(const CCommand& args)
{
	if (!g_kcp_initialized())
	{
		spdlog::warn("kcp not initialzed");
		return;
	}
	auto splited = split(args.ArgS(), " ");
	if (splited.size() < 2)
	{
		spdlog::warn("not enough args");
		return;
	}
	else if (splited.size() == 2)
	{
		splited.push_back("1");
	}
	in6_addr parsed {};
	if (inet_pton(AF_INET6, splited[0].c_str(), &parsed) != 1)
	{
		spdlog::warn("invalid addr");
		return;
	}
	USHORT port = atoi(splited[1].c_str());
	if (port == 0)
	{
		spdlog::warn("invalid port");
		return;
	}
	IUINT32 conv = atoi(splited[2].c_str());
	if (conv == 0)
	{
		spdlog::warn("invalid conv");
		return;
	}

	sockaddr_in6 remote_addr {};
	remote_addr.sin6_family = AF_INET6;
	remote_addr.sin6_addr = parsed;
	remote_addr.sin6_port = htons(port);
	remote_addr.sin6_scope_struct = scopeid_unspecified;

	{
		std::unique_lock lock3(g_kcp_manager->established_connections_mutex);
		if (g_kcp_manager->established_connections.contains(remote_addr))
		{
			spdlog::warn("[KCP] Control block is already created");
		}
		else
		{
			auto connection = new kcp_connection(g_kcp_manager, remote_addr, conv);
			g_kcp_manager->established_connections[remote_addr] = connection;
			spdlog::info("[KCP] Established local <--> {}%{}", ntop((const sockaddr*)&remote_addr), conv);
		}
	}

	R2::Cbuf_AddText(
		R2::Cbuf_GetCurrentPlayer(),
		fmt::format("connect {}", ntop((const sockaddr*)&remote_addr)).c_str(),
		R2::cmd_source_t::kCommandSrcCode);
}

void ConCommand_kcp_listen(const CCommand& args)
{
	if (!g_kcp_initialized())
	{
		spdlog::warn("kcp not initialzed");
		return;
	}

	auto splited = split(args.ArgS(), " ");
	if (splited.size() < 1)
	{
		spdlog::warn("not enough args");
		return;
	}
	else if (splited.size() == 1)
	{
		splited.push_back("1");
	}

	in6_addr parsed {};
	if (inet_pton(AF_INET6, splited[0].c_str(), &parsed) != 1)
	{
		spdlog::warn("invalid addr");
		return;
	}

	IUINT32 conv = atoi(splited[1].c_str());
	if (conv == 0)
	{
		spdlog::warn("invalid conv");
		return;
	}

	std::unique_lock lock(g_kcp_manager->pending_connections_mutex);
	g_kcp_manager->pending_connections[parsed].insert(conv);

	spdlog::info("[KCP] Pending local <--> {}%{}", splited[0], conv);
}

ConVar* Cvar_kcp_timer_interval;
ConVar* Cvar_kcp_timeout;

ON_DLL_LOAD("engine.dll", WSAHOOKS, (CModule module))
{
	ikcp_allocator(_malloc_base, _free_base);
	reed_solomon_allocator(_malloc_base, _free_base, _calloc_base);

	fec_init();

	spdlog::info("[KCP] WSA Hooks Initialized: {}", enable_wsa_hooks());

	RegisterConCommand("kcp_connect", ConCommand_kcp_connect, "connect to kcp server", FCVAR_CLIENTDLL);
	RegisterConCommand("kcp_listen", ConCommand_kcp_listen, "listen new kcp conn", FCVAR_CLIENTDLL);

	Cvar_kcp_timer_interval =
		new ConVar("kcp_timer_interval", "10", FCVAR_NONE, "miliseconds between each kcp update, lower is better but consumes more CPU.");
	Cvar_kcp_timeout = new ConVar("kcp_timeout", "5000", FCVAR_NONE, "miliseconds to clean up the kcp connection.");

	if (g_kcp_manager == nullptr)
	{
		g_kcp_manager = new kcp_manager(Cvar_kcp_timer_interval->GetInt());
		spdlog::info("[KCP] KCP manager created");
	}
}

/* encode 8 bits unsigned int */
static inline char* ikcp_encode8u(char* p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char* ikcp_decode8u(const char* p, unsigned char* c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char* ikcp_encode16u(char* p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	memcpy(p, &w, 2);
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char* ikcp_decode16u(const char* p, unsigned short* w)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	memcpy(w, p, 2);
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char* ikcp_encode32u(char* p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*(unsigned char*)(p + 0) = (unsigned char)((l >> 0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >> 8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	memcpy(p, &l, 4);
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char* ikcp_decode32u(const char* p, IUINT32* l)
{
#if IWORDS_BIG_ENDIAN || IWORDS_MUST_ALIGN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else
	memcpy(l, p, 4);
#endif
	p += 4;
	return p;
}

std::string ntop(const sockaddr* addr)
{
	switch (addr->sa_family)
	{
	case AF_INET:
	{
		auto converted = (sockaddr_in*)addr;
		char addrStr[17] {'/0'};
		inet_ntop(AF_INET, &converted->sin_addr, addrStr, 17);
		return fmt::format("{}:{}", addrStr, ntohs(converted->sin_port));
	}
	case AF_INET6:
	{
		auto converted = (sockaddr_in6*)addr;
		char addrStr[47] {'/0'};
		inet_ntop(AF_INET6, &converted->sin6_addr, addrStr, 47);
		return fmt::format("[{}]:{}", addrStr, ntohs(converted->sin6_port));
	}
	default:
		return fmt::format("unsupported sa_family: {}", addr->sa_family);
	}
}

/* get system time */
static inline void itimeofday(long* sec, long* usec)
{
	static long mode = 0, addsec = 0;
	BOOL retval;
	static IINT64 freq = 1;
	IINT64 qpc;
	if (mode == 0)
	{
		retval = QueryPerformanceFrequency((LARGE_INTEGER*)&freq);
		freq = (freq == 0) ? 1 : freq;
		retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
		addsec = (long)time(NULL);
		addsec = addsec - (long)((qpc / freq) & 0x7fffffff);
		mode = 1;
	}
	retval = QueryPerformanceCounter((LARGE_INTEGER*)&qpc);
	retval = retval * 2;
	if (sec)
		*sec = (long)(qpc / freq) + addsec;
	if (usec)
		*usec = (long)((qpc % freq) * 1000000 / freq);
}

/* get clock in millisecond 64 */
static inline IINT64 iclock64(void)
{
	long s, u;
	IINT64 value;
	itimeofday(&s, &u);
	value = ((IINT64)s) * 1000 + (u / 1000);
	return value;
}

static inline IUINT32 iclock()
{
	return (IUINT32)(iclock64() & 0xfffffffful);
}

static inline IINT32 itimediff(IUINT32 later, IUINT32 earlier)
{
	return ((IINT32)(later - earlier));
}

bool g_kcp_initialized()
{
	return g_kcp_manager != nullptr && g_kcp_manager->is_initialized();
}

int udp_output(const char* buf, int len, ikcpcb* kcp, void* user)
{
	auto userdata = (udp_output_userdata*)user;
	std::vector<std::vector<char>> encoded = userdata->connection->encoder->encode(buf, len);
	for (const auto& pkt : encoded)
	{
		auto sendto_result =
			org_sendto(userdata->socket, pkt.data(), pkt.size(), 0, (const sockaddr*)&userdata->remote_addr, sizeof(sockaddr_in6));
		if (sendto_result < 0)
		{
			return sendto_result;
		}
	}
	return 0;
}

void kcp_update_timer_cb(void* data, void* user)
{
	auto connection = (kcp_connection*)data;
	auto manager = (kcp_manager*)user;

	std::unique_lock lock1(*connection->mutex);
	auto current = iclock();
	if (itimediff(current, connection->last_input) > Cvar_kcp_timeout->GetInt())
	{
		ikcp_flush(connection->kcpcb);
		{
			std::unique_lock lock2(manager->established_connections_mutex);
			manager->established_connections.erase(((const udp_output_userdata*)(connection->kcpcb->user))->remote_addr);
		}
		itimer_evt_stop(&manager->timer_mgr, connection->update_timer);
		delete connection;

		return;
	}

	ikcp_update(connection->kcpcb, current);
	current = iclock();
	auto next = ikcp_check(connection->kcpcb, current) - current;

	// doesn't need lock here cause it could be only run under select thread which locks timer_mgr_mutex
	itimer_evt_stop(&manager->timer_mgr, connection->update_timer);
	itimer_evt_start(&manager->timer_mgr, connection->update_timer, next, 1);
}

void kcp_fec_aware_input(kcp_connection* connection, std::vector<char>& buf)
{
	IUINT16 flag = fec_flag(buf.data());

	if ((flag == FEC_TYPE_DATA || flag == FEC_TYPE_PARITY) && buf.size() > FEC_HEADER_OFFSET_DATA + 2)
	{
		auto recovered = connection->decoder->decode(buf.data(), buf.size());
		if (flag == FEC_TYPE_DATA)
		{
			auto input_result =
				ikcp_input(connection->kcpcb, buf.data() + FEC_HEADER_OFFSET_DATA + 2, buf.size() - FEC_HEADER_OFFSET_DATA - 2);
			if (input_result < 0)
			{
				spdlog::error("[KCP] Input error 1: {} {}", input_result, ikcp_getconv(buf.data() + FEC_HEADER_OFFSET_DATA + 2));
			}
		}
		for (const auto& rpkt : recovered)
		{
			if (rpkt.size() < 2)
			{
				continue;
			}
			IUINT16 sz = 0;
			ikcp_decode16u(rpkt.data(), &sz);
			if (sz > rpkt.size() || sz < 2)
			{
				continue;
			}
			auto input_result = ikcp_input(connection->kcpcb, rpkt.data() + 2, sz - 2);
			if (input_result < 0)
			{
				spdlog::error("[KCP] Input error 2: {}", input_result);
			}
		}
	}
	else
	{
		auto input_result = ikcp_input(connection->kcpcb, buf.data(), buf.size());
		if (input_result < 0)
		{
			spdlog::error("[KCP] Input error 3: {}", input_result);
		}
	}
}

kcp_manager::kcp_manager(IUINT32 timer_interval)
{
	this->timer_interval = timer_interval;
	itimer_mgr_init(&timer_mgr, timer_interval);
	select_thread = std::jthread(
		[this, timer_interval](std::stop_token stop_token)
		{
			fd_set sockets;
			timeval timeout {0, timer_interval * 1000};

			while (true)
			{
				if (stop_token.stop_requested())
				{
					break;
				}
				{
					std::unique_lock lock(this->timer_mgr_mutex);
					itimer_mgr_run(&this->timer_mgr, iclock());
				}
				if (!this->is_initialized())
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(timer_interval));
				}
				else
				{
					FD_ZERO(&sockets);
					FD_SET(this->local_socket, &sockets);

					auto select_result = select(NULL, &sockets, NULL, NULL, &timeout);

					if (!FD_ISSET(this->local_socket, &sockets))
					{
						continue;
					}

					std::vector<char> buf(this->local_socket_max_msg_size);
					sockaddr_in6 from {};
					int fromlen = sizeof(sockaddr_in6);

					auto recvfrom_result = org_recvfrom(this->local_socket, buf.data(), buf.size(), 0, (sockaddr*)&from, &fromlen);

					if (recvfrom_result == SOCKET_ERROR)
					{
						auto last_error = WSAGetLastError();
						if (last_error != WSAEWOULDBLOCK)
						{
							spdlog::error("[KCP] UDP recvfrom failed: {}", last_error);
						}
						continue;
					}

					buf.resize(recvfrom_result);

					if (recvfrom_result < 24)
					{
						unaltered_data.push(std::make_pair(from, buf));
						continue;
					}

					std::shared_lock lock1(this->established_connections_mutex);
					if (established_connections.contains(from))
					{
						const auto& connection = established_connections[from];
						std::scoped_lock lock2(*connection->mutex, this->timer_mgr_mutex);
						kcp_fec_aware_input(connection, buf);
						connection->last_input = iclock();
						itimer_evt_stop(&this->timer_mgr, connection->update_timer);
						itimer_evt_start(&this->timer_mgr, connection->update_timer, 0, 1);
					}
					else
					{
						lock1.unlock();
						auto recv_conv = ikcp_getconv(buf.data());
						std::unique_lock lock3(this->pending_connections_mutex);
						if (pending_connections.contains(from.sin6_addr) && pending_connections[from.sin6_addr].contains(recv_conv))
						{
							pending_connections[from.sin6_addr].erase(recv_conv);
							auto connection = new kcp_connection(this, from, recv_conv);
							kcp_fec_aware_input(connection, buf);
							std::unique_lock lock4(this->established_connections_mutex);
							this->established_connections[from] = connection;
							spdlog::info("[KCP] Established local <--> {}%{}", ntop((const sockaddr*)&from), recv_conv);
						}
						else
						{
							lock3.unlock();
							unaltered_data.push(std::make_pair(from, buf));
						}
					}
				}
			}
		});
	select_thread.detach();
}

kcp_manager::~kcp_manager()
{
	select_thread.request_stop();
	select_thread.join();
	{
		std::unique_lock lock(pending_connections_mutex);
		pending_connections.clear();
	}
	{
		std::unique_lock lock(established_connections_mutex);
		for (auto& entry : established_connections)
		{
			itimer_evt_stop(&this->timer_mgr, entry.second->update_timer);
			delete entry.second;
		}
		established_connections.clear();
	}
	itimer_mgr_destroy(&timer_mgr);
}

bool kcp_manager::is_initialized()
{
	return local_socket != 0 && local_socket_max_msg_size != 0;
}

int kcp_manager::intercept_bind(SOCKET socket, const sockaddr* name, int namelen)
{
	if (name == nullptr || name->sa_family != AF_INET6 || namelen < sizeof(sockaddr_in6))
	{
		return KCP_NOT_ALTERED;
	}

	auto client_port = (USHORT)R2::g_pCVar->FindVar("clientport")->GetInt();
	auto server_port = (USHORT)R2::g_pCVar->FindVar("hostport")->GetInt();

	auto local_port = IsDedicatedServer() ? server_port : client_port;

	auto bind_addr = (const sockaddr_in6*)name;
	if (ntohs(bind_addr->sin6_port) == local_port)
	{
		int optlen = sizeof(unsigned int);
		auto getsockopt_result = getsockopt(socket, SOL_SOCKET, SO_MAX_MSG_SIZE, (char*)&local_socket_max_msg_size, &optlen);

		if (getsockopt_result != 0)
		{
			auto last_error = WSAGetLastError();
			spdlog::error("[KCP] Initialize failed: getsockopt failed: {}", last_error);
			return KCP_NOT_ALTERED;
		}

		local_socket = socket;

		spdlog::info("[KCP] KCP manager initialized on socket {} with port {}", local_socket, local_port);
	}
	return KCP_NOT_ALTERED;
}

int kcp_manager::intercept_sendto(SOCKET socket, const char* buf, int len, const sockaddr* to, int tolen)
{
	if (!is_initialized())
	{
		return KCP_NOT_ALTERED;
	}

	if (socket != local_socket)
	{
		return KCP_NOT_ALTERED;
	}

	if (to == nullptr || to->sa_family != AF_INET6 || tolen < sizeof(sockaddr_in6))
	{
		return KCP_NOT_ALTERED;
	}

	auto converted_to = (const sockaddr_in6*)to;
	std::shared_lock lock1(established_connections_mutex);
	if (!established_connections.contains(*converted_to))
	{
		return KCP_NOT_ALTERED;
	}
	const auto& connection = established_connections[*converted_to];
	std::scoped_lock lock2(*connection->mutex, timer_mgr_mutex);
	auto send_result = ikcp_send(connection->kcpcb, buf, len);
	switch (send_result)
	{
	case -1: // len < 0
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	case -2: // alloc failure or msg too big
		WSASetLastError(WSAEMSGSIZE);
		return SOCKET_ERROR;
	}
	itimer_evt_stop(&timer_mgr, connection->update_timer);
	itimer_evt_start(&timer_mgr, connection->update_timer, 0, 1);
	return len;
}

int kcp_manager::intercept_recvfrom(SOCKET socket, char* buf, int len, sockaddr* from, int* fromlen)
{
	if (!is_initialized())
	{
		return KCP_NOT_ALTERED;
	}

	if (socket != local_socket)
	{
		return KCP_NOT_ALTERED;
	}

	if (from == nullptr || fromlen == nullptr || *fromlen < sizeof(sockaddr_in6))
	{
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}

	std::shared_lock lock1(established_connections_mutex);
	for (const auto& entry : established_connections)
	{
		std::unique_lock lock2(*entry.second->mutex);
		auto recv_result = ikcp_recv(entry.second->kcpcb, buf, len);
		switch (recv_result)
		{
		case -1: // EWOULDBLOCK
		case -2: // rcv_que doesnt have enough fragments
			break; // continue to next connection
		case -3: // buf too small
		{
			// simulate as the real udp to partially drop data
			auto peek_size = ikcp_peeksize(entry.second->kcpcb);
			char* new_buf = new char[peek_size];
			ikcp_recv(entry.second->kcpcb, new_buf, peek_size);
			if (len < peek_size)
			{
				memcpy(buf, new_buf, len);
			}
			else
			{
				memcpy(buf, new_buf, peek_size);
			}
			memcpy_s(from, *fromlen, &entry.first, sizeof(sockaddr_in6));
			WSASetLastError(WSAEMSGSIZE);
			return SOCKET_ERROR;
		}
		default: // >= 0
			memcpy_s(from, *fromlen, &entry.first, sizeof(sockaddr_in6));
			return recv_result;
		}
	}
	std::pair<sockaddr_in6, std::vector<char>> data;
	if (unaltered_data.try_pop(data))
	{
		memcpy_s(from, *fromlen, &data.first, sizeof(sockaddr_in6));
		if (len < data.second.size())
		{
			memcpy(buf, data.second.data(), len);
			WSASetLastError(WSAEMSGSIZE);
			return SOCKET_ERROR;
		}
		else
		{
			memcpy(buf, data.second.data(), data.second.size());
			return data.second.size();
		}
	}
	return KCP_NOT_ALTERED;
}

std::vector<std::pair<sockaddr_in6, kcp_stats>> kcp_manager::get_stats()
{
	std::vector<std::pair<sockaddr_in6, kcp_stats>> result;
	std::shared_lock lock(this->established_connections_mutex);
	for (const auto& entry : this->established_connections)
	{
		std::unique_lock lock2(*entry.second->mutex);
		kcp_stats stats {};
		auto& connection = entry.second;
		stats.srtt = connection->kcpcb->rx_srtt;
		stats.rto = connection->kcpcb->rx_rto;
		stats.out_segs = connection->kcpcb->out_segs;
		stats.lost_segs = connection->kcpcb->lost_segs;
		stats.retrans_segs = connection->kcpcb->retrans_segs;
		connection->kcpcb->out_segs = 0;
		connection->kcpcb->lost_segs = 0;
		connection->kcpcb->retrans_segs = 0;
		result.push_back(std::make_pair(entry.first, stats));
	}
	return result;
}

kcp_connection::kcp_connection(kcp_manager* kcp_manager, const sockaddr_in6& remote_addr, IUINT32 conv)
{
	udp_output_userdata* userdata = new udp_output_userdata {kcp_manager->local_socket, remote_addr, this};
	ikcpcb* kcpcb = ikcp_create(conv, userdata);
	ikcp_setoutput(kcpcb, udp_output);
	ikcp_nodelay(kcpcb, 1, kcp_manager->timer_interval, 2, 1);

	this->kcpcb = kcpcb;
	this->mutex = new std::mutex;

	itimer_evt* update_timer = new itimer_evt;
	this->update_timer = update_timer;
	itimer_evt_init(update_timer, kcp_update_timer_cb, this, kcp_manager);

	auto current = iclock();
	this->last_input = current;

	encoder = new fec_encoder(1, 1);
	decoder = new fec_decoder(1, 1);

	std::unique_lock lock1(kcp_manager->timer_mgr_mutex);
	itimer_evt_start(&kcp_manager->timer_mgr, update_timer, 0, 1);
}

kcp_connection::~kcp_connection()
{
	spdlog::info(
		"[KCP] Disconnecting local <--> {}%{}",
		ntop((const sockaddr*)(&((const udp_output_userdata*)(kcpcb->user))->remote_addr)),
		kcpcb->conv);
	delete mutex;
	delete kcpcb->user;
	ikcp_release(kcpcb);
	itimer_evt_destroy(update_timer);

	delete encoder;
	delete decoder;
}

IUINT32 fec_seqid(const char* buf)
{
	IUINT32 result = 0;
	ikcp_decode32u(buf + FEC_HEADER_OFFSET_SEQID, &result);
	return result;
}

IUINT16 fec_flag(const char* buf)
{
	IUINT16 result = 0;
	ikcp_decode16u(buf + FEC_HEADER_OFFSET_FLAG, &result);
	return result;
}

char* fec_data(char* buf)
{
	return buf + FEC_HEADER_OFFSET_DATA;
}

fec_decoder::fec_decoder(int data_shards, int parity_shards)
{
	rxlimit = FEC_RX_MULTI * (data_shards + parity_shards);
	codec = reed_solomon_new(data_shards, parity_shards);

	decode_cache = std::vector<std::vector<char>>(codec->shards);
	flag_cache = std::vector<char>(codec->shards);
}

fec_decoder::~fec_decoder()
{
	reed_solomon_release(codec);
}

std::vector<std::vector<char>> fec_decoder::decode(const char* buf, int len)
{
	std::vector<std::vector<char>> recovered;
	if (len < FEC_HEADER_OFFSET_DATA)
	{
		return recovered;
	}

	int n = rx.size() - 1;
	int insert_idx = 0;
	for (int i = n; i >= 0; i--)
	{
		if (fec_seqid(buf) == fec_seqid(rx[i].buf.data()))
		{
			return recovered;
		}
		else if (itimediff(fec_seqid(buf), fec_seqid(rx[i].buf.data())) > 0)
		{
			insert_idx = i + 1;
			break;
		}
	}

	fec_element elem = {std::vector<char>(buf, buf + len), iclock()};

	if (insert_idx == n + 1)
	{
		rx.push_back(elem);
	}
	else
	{
		rx.push_back({std::vector<char>(), 0});
		memmove_s(rx.data() + insert_idx + 1, rx.size() - (insert_idx + 1), rx.data() + insert_idx, rx.size() - (insert_idx + 1));
		rx[insert_idx] = elem;
	}

	IUINT32 shard_begin = fec_seqid(buf) - fec_seqid(buf) % (IUINT32)(codec->shards);
	IUINT32 shard_end = shard_begin + (IUINT32)(codec->shards) - 1;

	int search_begin = insert_idx - (int)(fec_seqid(buf) % (IUINT32)(codec->shards));
	if (search_begin < 0)
	{
		search_begin = 0;
	}
	int search_end = search_begin + codec->shards - 1;
	if (search_end >= rx.size())
	{
		search_end = rx.size() - 1;
	}

	if (search_end - search_begin + 1 >= codec->data_shards)
	{
		int num_shards = 0, num_data_shards = 0, first = 0, max_size = 0;

		auto& shards = decode_cache;
		auto& shards_flag = flag_cache;

		for (int i = 0; i < shards.size(); i++)
		{
			shards[i] = std::vector<char>();
			shards_flag[i] = false;
		}

		for (int i = search_begin; i <= search_end; i++)
		{
			auto seqid = fec_seqid(rx[i].buf.data());
			if (itimediff(seqid, shard_end) > 0)
			{
				break;
			}
			else if (itimediff(seqid, shard_begin) >= 0)
			{
				shards[seqid % (IUINT32)(codec->shards)] =
					std::vector<char>(fec_data(rx[i].buf.data()), fec_data(rx[i].buf.data()) + rx[i].buf.size() - FEC_HEADER_OFFSET_DATA);
				shards_flag[seqid % (IUINT32)(codec->shards)] = true;
				num_shards++;
				if (fec_flag(rx[i].buf.data()) == FEC_TYPE_DATA)
				{
					num_data_shards++;
				}
				if (num_shards == 1)
				{
					first = i;
				}
				if (shards[seqid % (IUINT32)(codec->shards)].size() > max_size)
				{
					max_size = shards[seqid % (IUINT32)(codec->shards)].size();
				}
			}
		}

		if (num_data_shards == codec->data_shards)
		{
			rx.erase(rx.begin() + first, rx.begin() + first + num_shards);
		}
		else if (num_shards >= codec->data_shards)
		{
			std::vector<char*> temp(shards.size());
			for (int i = 0; i < shards.size(); ++i)
			{
				shards[i].resize(max_size, 0);
				temp[i] = shards[i].data();
			}
			auto err =
				reed_solomon_reconstruct(codec, (unsigned char**)temp.data(), (unsigned char*)shards_flag.data(), shards.size(), max_size);
			if (err >= 0)
			{
				for (int k = 0; k < codec->data_shards; k++)
				{
					if (!shards_flag[k])
					{
						recovered.push_back(shards[k]);
					}
				}
			}
			rx.erase(rx.begin() + first, rx.begin() + first + num_shards);
		}
	}

	if (rx.size() > rxlimit)
	{
		rx.erase(rx.begin(), rx.begin() + 1);
	}

	auto current = iclock();
	int num_expired = 0;

	for (int i = 0; i < rx.size(); ++i)
	{
		if (itimediff(current, rx[i].ts) > FEC_EXPIRE)
		{
			num_expired++;
			continue;
		}
		break;
	}

	if (num_expired > 0)
	{
		rx.erase(rx.begin(), rx.begin() + num_expired);
	}

	return recovered;
}

fec_encoder::fec_encoder(int data_shards, int parity_shards)
{
	codec = reed_solomon_new(data_shards, parity_shards);
	paws = 0xffffffff / ((IUINT32)codec->shards * (IUINT32)codec->shards);
	next = 0;

	shard_count = 0;
	max_size = 0;

	shard_cache = std::vector<std::vector<char>>(codec->shards);
}

fec_encoder::~fec_encoder()
{
	reed_solomon_release(codec);
}

std::vector<std::vector<char>> fec_encoder::encode(const char* buf, int len)
{
	std::vector<std::vector<char>> result;

	std::vector<char> new_buf(len + FEC_HEADER_OFFSET_DATA + 2);

	ikcp_encode32u(new_buf.data() + FEC_HEADER_OFFSET_SEQID, next);
	ikcp_encode16u(new_buf.data() + FEC_HEADER_OFFSET_FLAG, FEC_TYPE_DATA);
	next++;

	ikcp_encode16u(new_buf.data() + FEC_HEADER_OFFSET_DATA, len + 2);

	memcpy(new_buf.data() + FEC_HEADER_OFFSET_DATA + 2, buf, len);

	shard_cache[shard_count] = new_buf;
	shard_count++;

	result.push_back(new_buf);

	if (new_buf.size() > max_size)
	{
		max_size = new_buf.size();
	}

	if (shard_count == codec->data_shards)
	{
		std::vector<char*> temp(shard_cache.size());

		for (int i = 0; i < shard_cache.size(); ++i)
		{
			shard_cache[i].resize(max_size, 0);
			temp[i] = shard_cache[i].data() + FEC_HEADER_OFFSET_DATA;
		}

		auto err = reed_solomon_encode2(codec, (unsigned char**)temp.data(), shard_cache.size(), max_size - FEC_HEADER_OFFSET_DATA);
		if (err >= 0)
		{
			for (int i = codec->data_shards; i < shard_cache.size(); ++i)
			{
				ikcp_encode32u(shard_cache[i].data() + FEC_HEADER_OFFSET_SEQID, next);
				ikcp_encode16u(shard_cache[i].data() + FEC_HEADER_OFFSET_FLAG, FEC_TYPE_PARITY);
				next = (next + 1) % paws;
				result.push_back(shard_cache[i]);
			}
		}

		shard_count = 0;
		max_size = 0;
	}

	return result;
}
