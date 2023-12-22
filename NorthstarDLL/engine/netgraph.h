#pragma once

#include "shared/kcpintegration.h"
#include <shared_mutex>
#include <concurrent_unordered_map.h>

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

class NetGraphSink : public NetSink
{
  public:
	~NetGraphSink();

	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	static std::shared_ptr<NetGraphSink> instance();

	std::shared_mutex remoteStatsMutex;
	concurrency::concurrent_unordered_map<NetContext, NetStats> remoteStats;

	NetStats localStat;

  private:
	NetGraphSink();

	std::jthread sendThread;
};
