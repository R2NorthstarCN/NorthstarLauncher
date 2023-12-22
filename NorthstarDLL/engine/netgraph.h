#pragma once

#include "shared/kcpintegration.h"
#include <shared_mutex>
#include <concurrent_unordered_map.h>
#include "client/kcpstats.h"

extern float* g_frameTime;

struct NetStats
{
	float frameTime;
	IUINT64 outsegs;
	IUINT64 lostsegs;
	IUINT64 retranssegs;
	IUINT64 insegs;
	IUINT64 reconsegs;

	void encode(NetBuffer& buf) const;
	void decode(const NetBuffer& buf);
	void sync(ikcpcb* cb);
};

struct NetSlidingWindows
{
	sliding_window sw_frameTime = sliding_window(50, false, false);

	sliding_window sw_outsegs = sliding_window(50, false, true);
	sliding_window sw_lostsegs = sliding_window(50, false, true);
	sliding_window sw_retranssegs = sliding_window(50, false, true);

	sliding_window sw_insegs = sliding_window(50, false, true);
	sliding_window sw_reconsegs = sliding_window(50, false, true);

	void rotate(const NetStats& s);
};

class NetGraphSink : public NetSink
{
  public:
	~NetGraphSink();

	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	static std::shared_ptr<NetGraphSink> instance();

	std::shared_mutex windowsMutex;
	concurrency::concurrent_unordered_map<NetContext, std::tuple<NetStats, sliding_window, NetSlidingWindows, NetSlidingWindows>> windows;

  private:
	NetGraphSink();

	std::jthread sendThread;
};
