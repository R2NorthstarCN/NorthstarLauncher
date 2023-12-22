#include "pch.h"
#include "netgraph.h"
#include <shared_mutex>

AUTOHOOK_INIT()

float* g_frameTime;

ON_DLL_LOAD("engine.dll", SERVERFPS, (CModule module))
{
	g_frameTime = module.Offset(0x13158ba0).As<float*>();
	AUTOHOOK_DISPATCH();
}

void sendThreadPayload(std::stop_token stoken)
{
	auto manager = NetManager::instance();
	while (!stoken.stop_requested())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::shared_lock lk(manager->routingTableMutex);
		for (const auto& entry : manager->routingTable)
		{
			NetBuffer buf(sizeof(float), 0, 128);
			memcpy(buf.data(), g_frameTime, sizeof(float));
			entry.second.first.second->sendto(std::move(buf), entry.first, FrameTimeSink::instance().get());
		}
	}
}

FrameTimeSink::FrameTimeSink()
{
	sendThread = std::jthread(sendThreadPayload);
}

FrameTimeSink::~FrameTimeSink() {}

int FrameTimeSink::input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom)
{
	float tmp = 0.0f;
	memcpy(&tmp, buf.data(), sizeof(float));
	remoteFrameTimes.insert(std::make_pair(ctx, tmp));
	NS::log::NEW_NET.get()->error("[FT] FrameTime {}: {} s | {} fps", ctx, tmp, 1.0 / tmp);
	return 0;
}

bool FrameTimeSink::initialized(int from)
{
	return true;
}

std::shared_ptr<FrameTimeSink> FrameTimeSink::instance()
{
	static std::shared_ptr<FrameTimeSink> singleton = std::shared_ptr<FrameTimeSink>(new FrameTimeSink());
	return singleton;
}
