#pragma once

#include "shared/kcpintegration.h"

extern float* g_frameTime;

class NetGraphSink : public NetSink
{
  public:
	~NetGraphSink();

	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	static std::shared_ptr<NetGraphSink> instance();

	std::unordered_map<NetContext, float> remoteFrameTimes;

  private:
	NetGraphSink();

	std::jthread sendThread;
};
