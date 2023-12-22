#pragma once

#include "shared/kcpintegration.h"

extern float* g_frameTime;

class FrameTimeSink : public NetSink
{
  public:
	~FrameTimeSink();

	virtual int input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom);
	virtual bool initialized(int from);

	static std::shared_ptr<FrameTimeSink> instance();

	std::unordered_map<NetContext, float> remoteFrameTimes;

  private:
	FrameTimeSink();

	std::jthread sendThread;
};
