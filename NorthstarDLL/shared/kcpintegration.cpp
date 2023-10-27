#include "shared/kcpintegration.h"
#include "core/hooks.h"
#include <dedicated/dedicated.h>

ConVar* Cvar_kcp_timer_resolution;
ConVar* Cvar_kcp_select_timeout;
ConVar* Cvar_kcp_conn_timeout;
ConVar* Cvar_kcp_fec_timeout;
ConVar* Cvar_kcp_fec_rx_multi;
ConVar* Cvar_kcp_fec_send_data_shards;
ConVar* Cvar_kcp_fec_send_parity_shards;

AUTOHOOK_INIT()

int(WSAAPI* orig_bind)(_In_ SOCKET s, _In_reads_bytes_(namelen) const struct sockaddr FAR* name, _In_ int namelen) = nullptr;

int(WSAAPI* orig_sendto)(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen) = nullptr;

int(WSAAPI* orig_recvfrom)(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen) = nullptr;

int WSAAPI de_bind(_In_ SOCKET s, _In_reads_bytes_(namelen) const struct sockaddr FAR* name, _In_ int namelen)
{
	auto result = orig_bind(s, name, namelen);
	if (result != SOCKET_ERROR)
	{
		if (name == nullptr || name->sa_family != AF_INET6 || namelen < sizeof(sockaddr_in6))
		{
			return result;
		}

		auto clientPort = (USHORT)R2::g_pCVar->FindVar("clientport")->GetInt();
		auto serverPort = (USHORT)R2::g_pCVar->FindVar("hostport")->GetInt();
		auto localPort = IsDedicatedServer() ? serverPort : clientPort;

		auto bindAddr = (const sockaddr_in6*)name;

		if (ntohs(bindAddr->sin6_port) == localPort)
		{
			UdpSource::instance()->bindSocket(s);
			NS::log::NEW_NET.get()->info("[UdpSource] Bind on localhost:{}@{}", localPort, s);
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
	auto gsResult = GameSink::instance()->sendto(s, buf, len, flags, to, tolen);
	if (gsResult != NET_HOOK_NOT_ALTERED)
	{
		return gsResult;
	}
	auto result = orig_sendto(s, buf, len, flags, to, tolen);
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
	auto gsResult = GameSink::instance()->recvfrom(s, buf, len, flags, from, fromlen);
	if (gsResult != NET_HOOK_NOT_ALTERED)
	{
		return gsResult;
	}
	auto result = orig_recvfrom(s, buf, len, flags, from, fromlen);
	return result;
}

bool createAndEnableHook(LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID* ppOriginal)
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

bool enableWsaHooks()
{
	return createAndEnableHook(L"Ws2_32", "bind", de_bind, (LPVOID*)&orig_bind) &&
		   createAndEnableHook(L"Ws2_32", "sendto", de_sendto, (LPVOID*)&orig_sendto) &&
		   createAndEnableHook(L"Ws2_32", "recvfrom", de_recvfrom, (LPVOID*)&orig_recvfrom);
}

ON_DLL_LOAD_RELIESON("engine.dll", WSAHOOKS, ConVar, (CModule module))
{
	AUTOHOOK_DISPATCH()

	ikcp_allocator(_malloc_base, _free_base);

	fec_init();

	NS::log::NEW_NET.get()->info("[Hook] Initialize result: {}", enableWsaHooks());

	Cvar_kcp_timer_resolution =
		new ConVar("kcp_timer_resolution", "5", FCVAR_NONE, "miliseconds between each thread wake, lower is better but consumes more CPU.");

	Cvar_kcp_select_timeout =
		new ConVar("kcp_select_timeout", "5", FCVAR_NONE, "miliseconds select has to wait, lower is better but consumes more CPU.");

	Cvar_kcp_conn_timeout = new ConVar("kcp_conn_timeout", "5000", FCVAR_NONE, "miliseconds before a connection is dropped.");

	Cvar_kcp_fec_timeout = new ConVar(
		"kcp_fec_timeout", "5000", FCVAR_NONE, "miliseconds before FEC drops unused packets, higher is better but consumes more memory.");

	Cvar_kcp_fec_rx_multi =
		new ConVar("kcp_fec_rx_multi", "3", FCVAR_NONE, "multiplier of FEC receive queue, higher is better but consumes more memory.");

	Cvar_kcp_fec_send_data_shards = new ConVar("kcp_fec_send_data_shards", "3", FCVAR_NONE, "number of FEC data shards.");

	Cvar_kcp_fec_send_parity_shards = new ConVar("kcp_fec_send_parity_shards", "2", FCVAR_NONE, "number of FEC parity shards.");
}

NetBuffer::NetBuffer(const char* buf, int len, int headerExtraLen)
{
	inner = std::vector<char>(headerExtraLen + len, 0);
	currentOffset = headerExtraLen;
	memcpy(inner.data() + headerExtraLen, buf, len);
}

char* NetBuffer::data() const
{
	return (char*)inner.data() + currentOffset;
}

size_t NetBuffer::size() const
{
	return inner.size() - currentOffset;
}

void NetBuffer::resize(size_t newSize, char def)
{
	inner.resize(newSize + currentOffset, def);
}

char NetBuffer::getU8H()
{
	char result = *data();
	currentOffset += 1;
	return result;
}

uint16_t NetBuffer::getU16H()
{
	uint16_t result = 0;
	ikcp_decode16u(data(), &result);
	currentOffset += 2;
	return result;
}

uint32_t NetBuffer::getU32H()
{
	uint32_t result = 0;
	ikcp_decode32u(data(), &result);
	currentOffset += 4;
	return result;
}

void NetBuffer::putU8H(char c)
{
	currentOffset -= 1;
	inner.at(currentOffset) = c;
}

void NetBuffer::putU16H(uint16_t c)
{
	currentOffset -= 2;
	ikcp_encode16u(data(), c);
}

void NetBuffer::putU32H(uint32_t c)
{
	currentOffset -= 4;
	ikcp_encode32u(data(), c);
}

void recycleThreadPayload(std::stop_token stoken)
{
	while (!stoken.stop_requested())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		std::unique_lock routingTableLock(NetManager::instance()->routingTableMutex);
		std::vector<NetContext> removes;
		auto current = iclock64();
		for (const auto& entry : NetManager::instance()->routingTable)
		{
			if (itimediff64(current, entry.second.second) > Cvar_kcp_conn_timeout->GetInt())
			{
				removes.push_back(entry.first);
			}
		}
		for (const auto& removal : removes)
		{
			NS::log::NEW_NET.get()->info("[NetManager] Disconnecting with {}", removal);
			NetManager::instance()->routingTable.unsafe_erase(removal);
		}
	}
}

NetManager::NetManager()
{
	recycleThread = std::jthread(recycleThreadPayload);
	recycleThread.detach();
}

NetManager::~NetManager()
{
	recycleThread.request_stop();
	recycleThread.join();
}

NetManager* NetManager::instance()
{
	static NetManager* singleton = new NetManager();
	return singleton;
}

std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> connectionInitDefault(const SOCKET& s, const sockaddr_in6& addr)
{
	std::shared_ptr<FecLayer> fec =
		std::shared_ptr<FecLayer>(new FecLayer(Cvar_kcp_fec_send_data_shards->GetInt(), Cvar_kcp_fec_send_parity_shards->GetInt(), 1, 1));
	fec->bindTop(std::static_pointer_cast<NetSink>(GameSink::instance()));
	fec->bindBottom(std::static_pointer_cast<NetSource>(UdpSource::instance()));
	return std::make_pair(std::static_pointer_cast<NetSink>(fec), std::static_pointer_cast<NetSource>(fec));
}

std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> NetManager::initAndBind(const NetContext& ctx)
{
	return initAndBind(ctx, connectionInitDefault);
}

std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> NetManager::initAndBind(
	const NetContext& ctx,
	std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> (*connectionInitFunc)(const SOCKET& s, const sockaddr_in6& addr))
{
	std::shared_lock routingTableLock(routingTableMutex);
	auto result = connectionInitFunc(ctx.socket, ctx.addr);
	routingTable.insert(std::make_pair(ctx, std::make_pair(result, iclock64())));
	return result;
}

void NetManager::bind(const NetContext& ctx, std::shared_ptr<NetSink> inboundDst, std::shared_ptr<NetSource> outboundDst)
{
	std::shared_lock routingTableLock(routingTableMutex);
	routingTable.insert(std::make_pair(ctx, std::make_pair(std::make_pair(inboundDst, outboundDst), iclock64())));
}

std::optional<std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>>> NetManager::route(const NetContext& ctx)
{
	std::shared_lock routingTableLock(routingTableMutex);
	auto it = routingTable.find(ctx);
	if (it == routingTable.end())
	{
		return std::optional<std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>>>();
	}
	(*it).second.second = iclock64();
	return std::optional<std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>>>((*it).second.first);
}

void selectThreadPayload(std::stop_token stoken)
{
	while (!stoken.stop_requested())
	{
		if (!UdpSource::instance()->initialized())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		fd_set sockets {};
		timeval timeout {0, Cvar_kcp_select_timeout->GetInt() * 1000};

		FD_ZERO(&sockets);
		FD_SET(UdpSource::instance()->socket, &sockets);

		auto selectResult = select(NULL, &sockets, NULL, NULL, &timeout);

		if (selectResult == SOCKET_ERROR)
		{
			NS::log::NEW_NET.get()->error("[UdpSource] select error @ {}: {}", UdpSource::instance()->socket, WSAGetLastError());
			continue;
		}

		if (!FD_ISSET(UdpSource::instance()->socket, &sockets))
		{
			continue;
		}

		std::vector<char> buf(NET_UDP_MAX_MESSAGE_SIZE, 0);
		sockaddr_in6 from {};
		int fromlen = sizeof(sockaddr_in6);

		auto recvfromResult = orig_recvfrom(UdpSource::instance()->socket, buf.data(), buf.size(), 0, (sockaddr*)&from, &fromlen);

		if (recvfromResult == SOCKET_ERROR)
		{
			auto lastError = WSAGetLastError();
			if (lastError != WSAEWOULDBLOCK)
			{
				NS::log::NEW_NET.get()->error("[UdpSource] recvfrom error @ {} : {}", UdpSource::instance()->socket, lastError);
			}
			continue;
		}

		buf.resize(recvfromResult);

		NetContext ctx {UdpSource::instance()->socket, from};
		auto route = NetManager::instance()->route(ctx);

		if (route.has_value())
		{
			if (!route->first->initialized())
			{
				NS::log::NEW_NET.get()->warn("[UdpSource] Routed {} to uninitalized NetSink*", ctx);
				continue;
			}
			auto inputResult = route->first->input(NetBuffer(buf), ctx);
			if (inputResult != 0)
			{
				NS::log::NEW_NET.get()->error("[UdpSource] NetSink*->input {} error: {}", ctx, inputResult);
			}
			continue;
		}

		// New connection

		NS::log::NEW_NET.get()->info("[UdpSource] Accepting new connection from {}", ctx);
		auto newRoute = NetManager::instance()->initAndBind(ctx);

		if (!newRoute.first->initialized())
		{
			NS::log::NEW_NET.get()->warn("[UdpSource] Routed {} to uninitalized NetSink*", ctx);
			continue;
		}
		auto inputResult = newRoute.first->input(NetBuffer(buf), ctx);
		if (inputResult != 0)
		{
			NS::log::NEW_NET.get()->error("[UdpSource] NetSink*->input {} error: {}", ctx, inputResult);
		}
	}
}

UdpSource::UdpSource()
{
	selectThread = std::jthread(selectThreadPayload);
	selectThread.detach();
}

UdpSource::~UdpSource()
{
	selectThread.request_stop();
	selectThread.join();
}

int UdpSource::sendto(const NetBuffer& buf, const NetContext& ctx)
{
	if (ctx.socket != socket)
	{
		return NET_HOOK_NOT_ALTERED;
	}
	auto sendtoResult = orig_sendto(socket, buf.data(), buf.size(), 0, (const sockaddr*)&ctx.addr, sizeof(sockaddr_in6));
	if (sendtoResult == SOCKET_ERROR)
	{
		NS::log::NEW_NET.get()->error("[UdpSource] sendto {} error: {}", ctx, WSAGetLastError());
	}
	return sendtoResult;
}

bool UdpSource::initialized()
{
	return socket != NULL;
}

void UdpSource::bindSocket(const SOCKET& s)
{
	socket = s;
}

std::shared_ptr<UdpSource> UdpSource::instance()
{
	static std::shared_ptr<UdpSource> singleton = std::shared_ptr<UdpSource>(new UdpSource());
	return singleton;
}

GameSink::GameSink() {}

int GameSink::input(const NetBuffer& buf, const NetContext& ctx)
{
	pendingData.push(std::make_pair(buf, ctx));
	return 0;
}

bool GameSink::initialized()
{
	return true;
}

int GameSink::recvfrom(
	_In_ SOCKET s,
	_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
	_Inout_opt_ int FAR* fromlen)
{
	std::pair<NetBuffer, NetContext> data;
	if (pendingData.try_pop(data))
	{
		if (data.second.socket != s)
		{
			pendingData.push(data);
			return NET_HOOK_NOT_ALTERED;
		}
		if (from == nullptr || fromlen == nullptr || *fromlen < sizeof(sockaddr_in6))
		{
			WSASetLastError(WSAEFAULT);
			return SOCKET_ERROR;
		}
		memcpy_s(from, *fromlen, &data.second.addr, sizeof(sockaddr_in6));
		if (len < data.first.size())
		{
			memcpy(buf, data.first.data(), len);
			WSASetLastError(WSAEMSGSIZE);
			return SOCKET_ERROR;
		}
		else
		{
			memcpy(buf, data.first.data(), data.first.size());
			return data.first.size();
		}
	}

	if (UdpSource::instance()->initialized() && UdpSource::instance()->socket == s)
	{
		WSASetLastError(WSAEWOULDBLOCK);
		return SOCKET_ERROR;
	}
	else
	{
		return NET_HOOK_NOT_ALTERED;
	}
}

int GameSink::sendto(
	_In_ SOCKET s,
	_In_reads_bytes_(len) const char FAR* buf,
	_In_ int len,
	_In_ int flags,
	_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
	_In_ int tolen)
{
	if (to == nullptr || tolen < sizeof(sockaddr_in6))
	{
		WSASetLastError(WSAEFAULT);
		return SOCKET_ERROR;
	}

	auto converted = *(sockaddr_in6*)to;
	NetContext ctx {s, converted};
	auto route = NetManager::instance()->route(ctx);

	if (route.has_value())
	{
		if (!route->second->initialized())
		{
			NS::log::NEW_NET.get()->warn("[GameSink] Routed {} to uninitalized NetSource*", ctx);
			return NET_HOOK_NOT_ALTERED;
		}
		auto sendtoResult = route->second->sendto(NetBuffer(buf, len), ctx);
		if (sendtoResult < 0)
		{
			NS::log::NEW_NET.get()->error("[GameSink] NetSource*->sendto {} error: {}", ctx, sendtoResult);
		}
		return sendtoResult;
	}

	// New connection

	if (s == UdpSource::instance()->socket)
	{
		NS::log::NEW_NET.get()->info("[GameSink] Initiating new connection to {}", ctx);
		auto newRoute = NetManager::instance()->initAndBind(ctx);

		if (!newRoute.first->initialized())
		{
			NS::log::NEW_NET.get()->warn("[GameSink] Routed {} to uninitalized NetSource*", ctx);
			return NET_HOOK_NOT_ALTERED;
		}
		auto sendtoResult = newRoute.second->sendto(NetBuffer(buf, len), ctx);
		if (sendtoResult != 0)
		{
			NS::log::NEW_NET.get()->error("[GameSink] NetSource*->sendto {} error: {}", ctx, sendtoResult);
		}
		return sendtoResult;
	}

	return NET_HOOK_NOT_ALTERED;
}

std::shared_ptr<GameSink> GameSink::instance()
{
	static std::shared_ptr<GameSink> singleton = std::shared_ptr<GameSink>(new GameSink());
	return singleton;
}

FecLayer::FecLayer(int encoderDataShards, int encoderParityShards, int decoderDataShards, int decoderParityShards)
{
	decoderCodec = reed_solomon_new(decoderDataShards, decoderParityShards);
	rxlimit = Cvar_kcp_fec_rx_multi->GetInt() * decoderCodec->shards;

	encoderCodec = reed_solomon_new(encoderDataShards, encoderParityShards);
	ePaws = 0xffffffff / ((IUINT32)encoderCodec->shards * (IUINT32)encoderCodec->shards);
	eNext = 0;
	eShardCount = 0;
	eMaxSize = 0;
	eShardCache = std::vector<NetBuffer>(encoderCodec->shards);
}

FecLayer::~FecLayer()
{
	reed_solomon_release(decoderCodec);
	reed_solomon_release(encoderCodec);
}

int FecLayer::sendto(const NetBuffer& buf, const NetContext& ctx)
{
	auto encoded = encode(buf);
	for (const auto& nBuf : encoded)
	{
		auto sendtoResult = bottom.lock()->sendto(nBuf, ctx);
		if (sendtoResult == SOCKET_ERROR)
		{
			NS::log::NEW_NET.get()->error("[FEC] bottom->sendto {} error: {}", ctx, sendtoResult);
		}
	}
	return 0;
}

int FecLayer::input(const NetBuffer& buf, const NetContext& ctx)
{
	if (buf.size() < FEC_MIN_SIZE)
	{
		NS::log::NEW_NET.get()->warn("[FEC] input {} sliently dropping non-FEC packet: insufficient length", ctx);
		return 0;
	}

	IUINT16 flag = 0;
	ikcp_decode16u(buf.data() + 4, &flag);

	if (flag == FEC_TYPE_DATA || flag == FEC_TYPE_PARITY)
	{
		auto reconstructed = reconstruct(buf, ctx);
		if (flag == FEC_TYPE_DATA)
		{
			NetBuffer nBuf = buf;
			nBuf.getU32H(); // drop seqid
			nBuf.getU16H(); // drop flag
			auto fecSize = nBuf.getU16H(); // drop size
			nBuf.resize(fecSize - 2, 0);

			auto inputResult = top->input(nBuf, ctx);
			if (inputResult == SOCKET_ERROR)
			{
				NS::log::NEW_NET.get()->error("[FEC] top->input {} error: {}", ctx, inputResult);
			}
		}
		for (auto& rBuf : reconstructed)
		{
			auto fecSize = rBuf.getU16H();
			rBuf.resize(fecSize - 2, 0);

			auto inputResult = top->input(rBuf, ctx);
			if (inputResult == SOCKET_ERROR)
			{
				NS::log::NEW_NET.get()->error("[FEC] top->input {} error: {}", ctx, inputResult);
			}
		}
	}
	else
	{
		NS::log::NEW_NET.get()->warn("[FEC] {} sliently dropping non-FEC packet: invalid flag", ctx);
	}

	return 0;
}

bool FecLayer::initialized()
{
	return top && !bottom.expired() && decoderCodec != nullptr && encoderCodec != nullptr && top->initialized() &&
		   bottom.lock()->initialized();
}

void FecLayer::bindTop(std::shared_ptr<NetSink> top)
{
	this->top = top;
}

void FecLayer::bindBottom(std::weak_ptr<NetSource> bottom)
{
	this->bottom = bottom;
}

std::vector<NetBuffer> FecLayer::reconstruct(const NetBuffer& buf, const NetContext& ctx)
{
	std::vector<NetBuffer> result;
	auto element = FecElement(buf);

	if (element.flag == FEC_TYPE_DATA)
	{
		autoTuner.sample(true, element.seqid);
	}
	else
	{
		autoTuner.sample(false, element.seqid);
	}

	auto shouldTune = false;
	if (element.seqid % decoderCodec->shards < decoderCodec->data_shards)
	{
		if (element.flag != FEC_TYPE_DATA)
		{
			shouldTune = true;
		}
	}
	else
	{
		if (element.flag != FEC_TYPE_PARITY)
		{
			shouldTune = true;
		}
	}

	if (shouldTune)
	{
		auto ps = autoTuner.findPeriods();
		if (ps.first > 0 && ps.second > 0 && ps.first < 256 && ps.second < 256)
		{
			if (ps.first != decoderCodec->data_shards || ps.second != decoderCodec->parity_shards)
			{
				reed_solomon_release(decoderCodec);
				decoderCodec = reed_solomon_new(ps.first, ps.second);
				rxlimit = Cvar_kcp_fec_rx_multi->GetInt() * decoderCodec->shards;
				NS::log::NEW_NET.get()->info("[FEC] {} autotuned to D:P = {}:{}", ctx, ps.first, ps.second);
			}
		}
	}

	int n = rx.size() - 1;
	int insertIdx = 0;
	for (int i = n; i >= 0; i--)
	{
		if (element.seqid == rx[i].seqid)
		{
			return result;
		}
		else if (itimediff(element.seqid, rx[i].seqid) > 0)
		{
			insertIdx = i + 1;
			break;
		}
	}

	rx.insert(rx.begin() + insertIdx, element);

	IUINT32 shardBegin = element.seqid - element.seqid % (IUINT32)(decoderCodec->shards);
	IUINT32 shardEnd = shardBegin + (IUINT32)(decoderCodec->shards) - 1;

	int searchBegin = std::max(insertIdx - (int)(element.seqid % (IUINT32)(decoderCodec->shards)), 0);
	int searchEnd = std::min(searchBegin + decoderCodec->shards - 1, ((int)rx.size()) - 1);

	if (searchEnd - searchBegin + 1 >= decoderCodec->data_shards)
	{
		int numShards = 0, numDataShards = 0, first = 0;
		size_t maxSize = 0;

		for (int i = searchBegin; i <= searchEnd; i++)
		{
			auto seqid = rx[i].seqid;
			if (itimediff(seqid, shardEnd) > 0)
			{
				break;
			}
			else if (itimediff(seqid, shardBegin) >= 0)
			{
				numShards++;
				if (rx[i].flag == FEC_TYPE_DATA)
				{
					numDataShards++;
				}
				if (numShards == 1)
				{
					first = i;
				}
				maxSize = std::max(maxSize, rx[i].buf.size());
			}
		}

		if (numDataShards == decoderCodec->data_shards)
		{
			rx.erase(rx.begin() + first, rx.begin() + first + numShards);
		}
		else if (numShards >= decoderCodec->data_shards)
		{
			std::vector<std::vector<char>> shards(decoderCodec->shards, std::vector<char>());
			std::vector<char> shardsFlag(decoderCodec->shards, true);

			for (int i = searchBegin; i <= searchEnd; i++)
			{
				auto seqid = rx[i].seqid;
				if (itimediff(seqid, shardEnd) > 0)
				{
					break;
				}
				else if (itimediff(seqid, shardBegin) >= 0)
				{
					shards[seqid % (IUINT32)(decoderCodec->shards)] =
						std::vector<char>(rx[i].buf.data(), rx[i].buf.data() + rx[i].buf.size());
					shardsFlag[seqid % (IUINT32)(decoderCodec->shards)] = false;
				}
			}

			std::vector<char*> temp(shards.size());
			for (int i = 0; i < shards.size(); ++i)
			{
				shards[i].resize(maxSize, 0);
				temp[i] = shards[i].data();
			}

			auto err = reed_solomon_reconstruct(
				decoderCodec, (unsigned char**)temp.data(), (unsigned char*)shardsFlag.data(), shards.size(), maxSize);
			if (err >= 0)
			{
				for (int k = 0; k < decoderCodec->data_shards; k++)
				{
					if (shardsFlag[k])
					{
						result.push_back(NetBuffer(shards[k]));
					}
				}
			}
			rx.erase(rx.begin() + first, rx.begin() + first + numShards);
		}
	}

	auto current = iclock();
	int numExpired = 0;
	for (int i = 0; i < rx.size(); i++)
	{
		if (itimediff(current, rx[i].ts) > Cvar_kcp_fec_timeout->GetInt())
		{
			numExpired++;
			continue;
		}
		break;
	}
	rx.erase(rx.begin(), rx.begin() + numExpired);

	if (rx.size() > rxlimit)
	{
		rx.erase(rx.begin(), rx.begin() + 1);
	}

	return result;
}

std::vector<NetBuffer> FecLayer::encode(const NetBuffer& buf)
{
	std::vector<NetBuffer> result;

	NetBuffer newBuf = buf;
	newBuf.putU16H(buf.size() + 2);
	newBuf.putU16H(FEC_TYPE_DATA); // flag
	newBuf.putU32H(eNext); // seqid

	result.push_back(newBuf);

	// drop non-data part
	newBuf.getU32H();
	newBuf.getU16H();

	eShardCache[eShardCount] = newBuf;
	eShardCount++;

	eMaxSize = std::max(eMaxSize, newBuf.size());

	if (eShardCount == encoderCodec->data_shards)
	{
		std::vector<std::vector<char>> temp(eShardCache.size());
		std::vector<char*> tempPtrs(eShardCache.size());

		for (int i = 0; i < eShardCache.size(); i++)
		{
			temp[i] = std::vector<char>(eShardCache[i].data(), eShardCache[i].data() + eShardCache[i].size());
			temp[i].resize(eMaxSize, 0);
			tempPtrs[i] = temp[i].data();
		}

		auto err = reed_solomon_encode2(encoderCodec, (unsigned char**)tempPtrs.data(), eShardCache.size(), eMaxSize);
		if (err >= 0)
		{
			for (int i = encoderCodec->data_shards; i < eShardCache.size(); i++)
			{
				NetBuffer pBuf(temp[i]);
				pBuf.putU16H(FEC_TYPE_PARITY); // flag
				pBuf.putU32H(eNext); // seqid
				eNext = (eNext + 1) % ePaws;
				result.push_back(std::move(pBuf));
			}
		}
		eShardCount = 0;
		eMaxSize = 0;
	}
	return result;
}

FecLayer::FecElement::FecElement(const NetBuffer& ebuf) : buf(ebuf)
{
	ts = iclock();
	seqid = buf.getU32H();
	flag = buf.getU16H();
}

void FecLayer::AutoTuner::sample(bool bit, IUINT32 seq)
{
	std::rotate(std::rbegin(pulses), std::rbegin(pulses) + 1, std::rend(pulses));
	pulses[0] = {bit, seq};
}

std::pair<int, int> FecLayer::AutoTuner::findPeriods()
{
	std::unordered_map<int, int> dataShardsInterval;
	std::unordered_map<int, int> parityShardsInterval;

	bool currentFinding = pulses[0].bit;
	int currentFinds = 1;

	for (int i = 1; i < FEC_MAX_AUTO_TUNE_SAMPLE; i++)
	{
		if (pulses[i].bit == currentFinding)
		{
			currentFinds += 1;
		}
		else
		{
			if (currentFinding)
			{
				dataShardsInterval[currentFinds] += 1;
			}
			else
			{
				parityShardsInterval[currentFinds] += 1;
			}
			currentFinding = pulses[i].bit;
			currentFinds = 1;
		}
	}

	auto ds = std::max_element(
		dataShardsInterval.begin(),
		dataShardsInterval.end(),
		[](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.second < b.second; });

	auto ps = std::max_element(
		parityShardsInterval.begin(),
		parityShardsInterval.end(),
		[](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.second < b.second; });

	return std::pair<int, int>(ds->first, ps->first);
}

void updateThreadPayload(std::stop_token stoken) {}

KcpLayer::KcpLayer()
{
	cb = ikcp_create(0, );
}

KcpLayer::~KcpLayer() {}

bool KcpLayer::initialized()
{
	return top && !bottom.expired() && top->initialized() && bottom.lock()->initialized() && updateThread.joinable() && cb != nullptr && cb->user != nullptr;
}
