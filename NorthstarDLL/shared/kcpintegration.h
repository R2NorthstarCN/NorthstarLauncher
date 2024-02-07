#pragma once

#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>
#include "shared/ikcp.h"
#include <unordered_map>
#include <shared_mutex>
#include <map>

#define RS_MALLOC _malloc_base
#define RS_FREE _free_base
#define RS_CALLOC _calloc_base
#include "shared/rs.h"

struct NetContext
{
	sockaddr_in6 addr;
	bool recon = false;
};

template <> struct fmt::formatter<NetContext>
{
	constexpr auto parse(format_parse_context& ctx) -> format_parse_context::iterator
	{
		return ctx.end();
	}

	auto format(const NetContext& p, format_context& ctx) const -> format_context::iterator
	{
		char addrStr[47] {'\0'};
		inet_ntop(p.addr.sin6_family, &p.addr.sin6_addr, addrStr, 47);
		return fmt::format_to(ctx.out(), "[{}]:{}", addrStr, ntohs(p.addr.sin6_port));
	}
};

// std::hash and std::equal_to definitions

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

	template <> struct equal_to<NetContext>
	{
		bool operator()(const NetContext& l, const NetContext& r) const
		{
			return equal_to<sockaddr_in6>()(l.addr, r.addr);
		}
	};

	template <> struct hash<NetContext>
	{
		size_t operator()(const NetContext& k) const
		{
			return hash<sockaddr_in6>()(k.addr);
		}
	};
} // namespace std

// Time util functions

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

// get clock in millisecond 64
static inline IINT64 iclock64()
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

static inline IINT64 itimediff64(IINT64 later, IINT64 earlier)
{
	return ((IINT64)(later - earlier));
}

// Convars

extern ConVar* Cvar_kcp_timer_resolution;
extern ConVar* Cvar_kcp_conn_timeout;
extern ConVar* Cvar_kcp_fec;
extern ConVar* Cvar_kcp_fec_timeout;
extern ConVar* Cvar_kcp_fec_rx_multi;
extern ConVar* Cvar_kcp_fec_autotune;
extern ConVar* Cvar_kcp_fec_send_data_shards;
extern ConVar* Cvar_kcp_fec_send_parity_shards;

// Net architecture

const int NET_BUFFER_DEFAULT_HEADER_EXTRA_LEN = 128;

struct NetBuffer
{
	std::vector<char> inner;
	size_t currentOffset;

	inline NetBuffer() : NetBuffer(std::vector<char>()) {}
	NetBuffer(std::vector<char>&& buf);
	inline NetBuffer(const std::vector<char>& buf) : NetBuffer(buf.data(), buf.size()) {}
	inline NetBuffer(const char* buf, const int len) : NetBuffer(buf, len, NET_BUFFER_DEFAULT_HEADER_EXTRA_LEN) {}
	NetBuffer(const char* buf, const int len, const int headerExtraLen);
	NetBuffer(const size_t len, char def, const int headerExtraLen);

	char* data() const;
	size_t size() const;

	void resize(size_t newSize, char def);

	char getU8H();
	uint16_t getU16H();
	uint32_t getU32H();

	void putU8H(char c);
	void putU16H(uint16_t c);
	void putU32H(uint32_t c);
};

//                      ┌────────┐
//                      │  Game  │
//                      └─▲───┬──┘
//                        │   │     Outbound Flow
//                      ┌─┴───▼──┐  Top of the stack
//                      │ Layer  │
//                      └─▲───┬──┘
//                        │   │
//                      ┌─┴───▼──┐
//                      │ Layer  │
//                      └─▲───┬──┘
//        Inbound Flow    │   │
// Bottom of the stack  ┌─┴───▼──┐
//                      │  UDP   │
//                      └────────┘
//
// The "source" initiates the inbound flow, while the "sink" initiates the outbound flow.
// For the source and sink we only expose their non-initiating part in the abstract class,
// and it is called by other layers.
// For the layers, we only expose the arrows/flows pointed towards them,
// and the other flows are called when the inward flows are called.
//
// Naming convention:
//
//           ▲     │
// recvfrom  │     │  sendto
//          ┌┴─────▼┐
//          │ Layer │
//          └▲─────┬┘
//    input  │     │  output
//           │     ▼

const int FROM_CAL = -1;
const int FROM_TOP = 1;
const int FROM_BOT = 2;

class NetSource;
class NetSink;

class NetSource
{
  public:
	virtual int sendto(NetBuffer&& buf, const NetContext& ctx, const NetSink* top) = 0;
	virtual bool initialized(int from) = 0;
};

class NetSink
{
  public:
	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom) = 0;
	virtual bool initialized(int from) = 0;
};

struct NetConnection
{
	std::shared_ptr<NetSink> sink;
	std::shared_ptr<NetSource> source;
	IUINT32 lastSeen;
	bool disconnected;
};

class NetManager
{
  public:
	~NetManager();
	static NetManager* instance();

	NetConnection initAndBind(const NetContext& ctx);
	NetConnection initAndBind(
		const NetContext& ctx,
		std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> (*connectionInitFunc)(const sockaddr_in6& addr));
	std::optional<NetConnection> route(const NetContext& ctx);
	void updateLastSeen(const NetContext& ctx);

	friend void recycleThreadPayload(std::stop_token stoken);

	// Only locked exclusively when disconnecting.
	std::shared_mutex routingTableMutex;
	Concurrency::concurrent_unordered_map<NetContext, NetConnection>
		routingTable;

	SOCKET socket = NULL;
  private:
	std::jthread recycleThread;

	NetManager();
};

const size_t NET_UDP_MAX_MESSAGE_SIZE = 65535;

class GameSink : public NetSink
{
  public:
	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	int recvfrom(
		_In_ SOCKET s,
		_Out_writes_bytes_to_(len, return) __out_data_source(NETWORK) char FAR* buf,
		_In_ int len,
		_In_ int flags,
		_Out_writes_bytes_to_opt_(*fromlen, *fromlen) struct sockaddr FAR* from,
		_Inout_opt_ int FAR* fromlen);

	int sendto(
		_In_ SOCKET s,
		_In_reads_bytes_(len) const char FAR* buf,
		_In_ int len,
		_In_ int flags,
		_In_reads_bytes_(tolen) const struct sockaddr FAR* to,
		_In_ int tolen);

	static std::shared_ptr<GameSink> instance();

	friend void selectThreadPayload(std::stop_token stoken);

  private:
	Concurrency::concurrent_queue<std::pair<NetBuffer, NetContext>> pendingData;

	GameSink();
};

class UdpSource : public NetSource
{
  public:
	~UdpSource();

	virtual int sendto(NetBuffer&& buf, const NetContext& ctx, const NetSink* top);
	virtual bool initialized(int from);

	static std::shared_ptr<UdpSource> instance();

	friend GameSink;
	friend void selectThreadPayload(std::stop_token stoken);

  private:
	std::jthread selectThread;
	std::mutex sendMutex;

	UdpSource();
};

const IUINT16 FEC_TYPE_DATA = 0xf1;
const IUINT16 FEC_TYPE_PARITY = 0xf2;
const size_t FEC_MIN_SIZE = 6;
const size_t FEC_MAX_AUTO_TUNE_SAMPLE = 32;

class FecLayer : public NetSource, public NetSink
{
  public:
	FecLayer(int encoderDataShards, int encoderParityShards, int decoderDataShards, int decoderParityShards);
	~FecLayer();

	virtual int sendto(NetBuffer&& buf, const NetContext& ctx, const NetSink* top);
	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	void bindTop(std::shared_ptr<NetSink> top);
	void bindBottom(std::weak_ptr<NetSource> bottom);

  private:
	std::shared_ptr<NetSink> top;
	std::weak_ptr<NetSource> bottom;

	struct FecElement
	{
		NetBuffer buf;
		IUINT32 ts;

		IUINT32 seqid;
		IUINT16 flag;

		FecElement(const NetBuffer& ebuf);
	};

	struct Pulse
	{
		bool bit = false;
		IUINT32 seq = 0;
	};

	struct AutoTuner
	{
		Pulse pulses[FEC_MAX_AUTO_TUNE_SAMPLE] {};

		void sample(bool bit, IUINT32 seq);
		std::pair<int, int> findPeriods();
	};

	// Decoder part
	IUINT32 rxlimit = 0;
	std::vector<FecElement> rx;
	reed_solomon* decoderCodec = nullptr;
	AutoTuner autoTuner;

	std::vector<NetBuffer> reconstruct(const NetBuffer& buf, const NetContext& ctx);

	// Encoder part
	IUINT32 ePaws = 0;
	IUINT32 eNext = 0;
	IUINT32 eShardCount = 0;
	size_t eMaxSize = 0;
	std::vector<NetBuffer> eShardCache;
	reed_solomon* encoderCodec = nullptr;

	std::vector<NetBuffer> encode(const NetBuffer& buf);
};

class KcpLayer : public NetSource, public NetSink
{
  public:
	KcpLayer(const NetContext& ctx);
	~KcpLayer();

	virtual int sendto(NetBuffer&& buf, const NetContext& ctx, const NetSink* top);
	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	void bindTop(std::shared_ptr<NetSink> top);
	void bindBottom(std::weak_ptr<NetSource> bottom);

	friend void updateThreadPayload(std::stop_token stoken, KcpLayer* layer);
	friend int kcpOutput(const char* buf, int len, ikcpcb* kcp, void* layer);

  private:
	std::shared_ptr<NetSink> top;
	std::weak_ptr<NetSource> bottom;

	std::jthread updateThread;

	std::condition_variable updateCv;
	std::mutex updateCvMutex;

	// Stored cause ikcpcb invokes udp_output callback indefinitely.
	NetContext remoteAddr;

	ikcpcb* cb;
	std::mutex cbMutex;
};

class MuxLayer : public NetSource, public NetSink
{
  public:
	MuxLayer();
	~MuxLayer();

	virtual int sendto(NetBuffer&& buf, const NetContext& ctx, const NetSink* top);
	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	void bindTop(IUINT8 channelId, std::shared_ptr<NetSink> top);
	void bindBottom(std::weak_ptr<NetSource> bottom);

  private:
	std::weak_ptr<NetSource> bottom;

	std::unordered_map<IUINT8, std::shared_ptr<NetSink>> topMap;
	std::unordered_map<uintptr_t, IUINT8> topInverseMap;
};

typedef void (*MicroPacketProcessFunc)(NetBuffer&&, const NetContext&);

class MicroPacketSink : public NetSink
{
  public:
	MicroPacketSink();
	~MicroPacketSink();

	static std::shared_ptr<MicroPacketSink> instance();

	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	void bindOpcode(uint32_t opcode, MicroPacketProcessFunc func);
	void sendMicroPacket(uint32_t opcode, NetBuffer&& payload, const NetContext& ctx);

private:
	std::unordered_map<uint32_t, MicroPacketProcessFunc> funcMap;
};

enum MicroPacketOpcode
{
	RESERVED,
	CLIENT_DISCONNECTING
};

void onClientDisconnecting(NetBuffer&& buf, const NetContext& ctx);
