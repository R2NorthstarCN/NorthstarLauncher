#pragma once

#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>
#include "shared/ikcp.h"
#include <unordered_map>
#include <shared_mutex>

#define RS_MALLOC _malloc_base
#define RS_FREE _free_base
#define RS_CALLOC _calloc_base
#include "shared/rs.h"

struct NetContext
{
	SOCKET socket;
	sockaddr_in6 addr;
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
		return fmt::format_to(ctx.out(), "[{}]:{}@{}", addrStr, ntohs(p.addr.sin6_port), p.socket);
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
			return equal_to<SOCKET>()(l.socket, r.socket) && equal_to<sockaddr_in6>()(l.addr, r.addr);
		}
	};

	template <> struct hash<NetContext>
	{
		size_t operator()(const NetContext& k) const
		{
			size_t res = 17;
			res = res * 31 + hash<SOCKET>()(k.socket);
			res = res * 31 + hash<sockaddr_in6>()(k.addr);
			return res;
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
extern ConVar* Cvar_kcp_select_timeout;
extern ConVar* Cvar_kcp_conn_timeout;
extern ConVar* Cvar_kcp_fec_timeout;
extern ConVar* Cvar_kcp_fec_rx_multi;
extern ConVar* Cvar_kcp_fec_send_data_shards;
extern ConVar* Cvar_kcp_fec_send_parity_shards;

// Net architecture

const int NET_BUFFER_DEFAULT_HEADER_EXTRA_LEN = 128;

struct NetBuffer
{
	std::vector<char> inner;
	size_t currentOffset;

	inline NetBuffer() : NetBuffer(std::vector<char>()) {}
	inline NetBuffer(std::vector<char>&& buf) : NetBuffer(buf.data(), buf.size()) {}
	inline NetBuffer(const std::vector<char>& buf) : NetBuffer(buf.data(), buf.size()) {}
	inline NetBuffer(const char* buf, const int len) : NetBuffer(buf, len, NET_BUFFER_DEFAULT_HEADER_EXTRA_LEN) {}
	NetBuffer(const char* buf, const int len, const int headerExtraLen);

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
//                      │ Sink   │
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
//                      │ Source │
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

class NetSource
{
  public:
	virtual int sendto(const NetBuffer& buf, const NetContext& ctx) = 0;
	virtual bool initialized() = 0;
};

class NetSink
{
  public:
	virtual int input(const NetBuffer& buf, const NetContext& ctx) = 0;
	virtual bool initialized() = 0;
};

class NetManager
{
  public:
	~NetManager();
	static NetManager* instance();

	std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> initAndBind(const NetContext& ctx);
	std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> initAndBind(
		const NetContext& ctx,
		std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>> (*connectionInitFunc)(const SOCKET& s, const sockaddr_in6& addr));
	void bind(const NetContext& ctx, std::shared_ptr<NetSink> inboundDst, std::shared_ptr<NetSource> outboundDst);
	std::optional<std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>>> route(const NetContext& ctx);

	friend void recycleThreadPayload(std::stop_token stoken);

  private:
	// Only locked exclusively when disconnecting.
	std::shared_mutex routingTableMutex;
	Concurrency::concurrent_unordered_map<NetContext, std::pair<std::pair<std::shared_ptr<NetSink>, std::shared_ptr<NetSource>>, IINT64>>
		routingTable;

	std::jthread recycleThread;

	NetManager();
};

const size_t NET_UDP_MAX_MESSAGE_SIZE = 65535;
const int NET_HOOK_NOT_ALTERED = -114;

class GameSink : public NetSink
{
  public:
	virtual int input(const NetBuffer& buf, const NetContext& ctx);
	virtual bool initialized();

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

	virtual int sendto(const NetBuffer& buf, const NetContext& ctx);
	virtual bool initialized();

	void bindSocket(const SOCKET& s);

	static std::shared_ptr<UdpSource> instance();

	friend GameSink;
	friend void selectThreadPayload(std::stop_token stoken);

  private:
	SOCKET socket = NULL;
	std::jthread selectThread;

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

	virtual int sendto(const NetBuffer& buf, const NetContext& ctx);
	virtual int input(const NetBuffer& buf, const NetContext& ctx);
	virtual bool initialized();

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

class KcpLayer : public NetSource, NetSink
{
  public:
	KcpLayer();
	~KcpLayer();

	virtual int sendto(const NetBuffer& buf, const NetContext& ctx);
	virtual int input(const NetBuffer& buf, const NetContext& ctx);
	virtual bool initialized();

	void startUpdateThread();

  private:
	std::shared_ptr<NetSink> top;
	std::weak_ptr<NetSource> bottom;

	std::jthread updateThread;

	std::condition_variable updateCv;
	std::mutex updateCvMutex;

	ikcpcb* cb;
};
