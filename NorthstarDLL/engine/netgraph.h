#pragma once

#include "shared/kcpintegration.h"
#include <shared_mutex>
#include <concurrent_unordered_map.h>

extern float* g_frameTime;

const int NETGRAPH_UPDATE_INTERVAL = 50;
const int NETGRAPH_DATA_POINTS = 100;

struct sliding_window
{
	std::vector<double> raw;
	std::vector<double> axis_x;

	bool delta = false;
	double last_val = 0;
	double sumed = 0;

	inline sliding_window() : sliding_window(NETGRAPH_DATA_POINTS, false) {};
	sliding_window(size_t samples);
	sliding_window(size_t samples, bool delta);

	void rotate(double new_val);

	std::pair<std::vector<double>&, std::vector<double>&> get_axes();

	double last() const;
	double avg() const;
	double sum() const;
	double max() const;
	double min() const;
};

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
	sliding_window sw_sv = sliding_window(NETGRAPH_DATA_POINTS, false);

	sliding_window sw_outsegs = sliding_window(NETGRAPH_DATA_POINTS, true);
	sliding_window sw_lostsegs = sliding_window(NETGRAPH_DATA_POINTS, true);
	sliding_window sw_retranssegs = sliding_window(NETGRAPH_DATA_POINTS, true);

	sliding_window sw_insegs = sliding_window(NETGRAPH_DATA_POINTS, true);
	sliding_window sw_reconsegs = sliding_window(NETGRAPH_DATA_POINTS, true);

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
