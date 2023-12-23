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

void sendThreadPayload(std::stop_token stoken, NetGraphSink* ng)
{
	auto manager = NetManager::instance();
	auto lastSeenInterval = Cvar_kcp_conn_timeout->GetInt() / 2;
	while (!stoken.stop_requested())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::shared_lock lk(manager->routingTableMutex);
		for (const auto& entry : manager->routingTable)
		{
			if (itimediff(iclock(), entry.second.second) > lastSeenInterval)
			{
				continue;
			}
			NetBuffer buf(128, 0, 128);
			std::shared_lock lk(ng->windowsMutex);
			std::get<0>(ng->windows[entry.first]).encode(buf);
			entry.second.first.second->sendto(std::move(buf), entry.first, NetGraphSink::instance().get());
		}
	}
}

NetGraphSink::NetGraphSink()
{
	sendThread = std::jthread(sendThreadPayload, this);
}

NetGraphSink::~NetGraphSink()
{
	sendThread.request_stop();
	sendThread.join();
}

int NetGraphSink::input(NetBuffer&& buf, const NetContext& ctx, const NetSource* bottom)
{
	NetStats s {};
	s.decode(buf);
	std::shared_lock lk(windowsMutex);
	std::get<3>(windows[ctx]).rotate(s);
	return 0;
}

bool NetGraphSink::initialized(int from)
{
	return true;
}

std::shared_ptr<NetGraphSink> NetGraphSink::instance()
{
	static std::shared_ptr<NetGraphSink> singleton = std::shared_ptr<NetGraphSink>(new NetGraphSink());
	return singleton;
}

void NetStats::encode(NetBuffer& buf) const
{
	if (buf.size() < sizeof(float) + 5 * sizeof(IUINT64))
	{
		buf.resize(buf.size() + sizeof(float) + 5 * sizeof(IUINT64), 0);
	}

	memcpy(buf.data(), &frameTime, sizeof(float));
	memcpy(buf.data() + sizeof(float), &outsegs, sizeof(IUINT64));
	memcpy(buf.data() + sizeof(float) + 1 * sizeof(IUINT64), &lostsegs, sizeof(IUINT64));
	memcpy(buf.data() + sizeof(float) + 2 * sizeof(IUINT64), &retranssegs, sizeof(IUINT64));
	memcpy(buf.data() + sizeof(float) + 3 * sizeof(IUINT64), &insegs, sizeof(IUINT64));
	memcpy(buf.data() + sizeof(float) + 4 * sizeof(IUINT64), &reconsegs, sizeof(IUINT64));
}

void NetStats::decode(const NetBuffer& buf)
{
	if (buf.size() < sizeof(float) + 5 * sizeof(IUINT64))
	{
		NS::log::NEW_NET->warn("[NG] Spurious input of NetStats::decode");
		return;
	}

	memcpy(&frameTime, buf.data(), sizeof(float));
	memcpy(&outsegs, buf.data() + sizeof(float), sizeof(IUINT64));
	memcpy(&lostsegs, buf.data() + sizeof(float) + 1 * sizeof(IUINT64), sizeof(IUINT64));
	memcpy(&retranssegs, buf.data() + sizeof(float) + 2 * sizeof(IUINT64), sizeof(IUINT64));
	memcpy(&insegs, buf.data() + sizeof(float) + 3 * sizeof(IUINT64), sizeof(IUINT64));
	memcpy(&reconsegs, buf.data() + sizeof(float) + 4 * sizeof(IUINT64), sizeof(IUINT64));
}

void NetStats::sync(ikcpcb* cb)
{
	frameTime = *g_frameTime;
	outsegs = cb->outsegs;
	lostsegs = cb->lostsegs;
	retranssegs = cb->retranssegs;
	insegs = cb->insegs;
	reconsegs = cb->reconsegs;
}

static double getSV(float ft)
{
	if (ft < 0.001f)
	{
		return 999.0;
	}
	else
	{
		return 1.0 / ft;
	}
}

void NetSlidingWindows::rotate(const NetStats& s) {
	sw_sv.rotate(getSV(s.frameTime));
	sw_outsegs.rotate(s.outsegs);
	sw_lostsegs.rotate(s.lostsegs);
	sw_retranssegs.rotate(s.retranssegs);
	sw_insegs.rotate(s.insegs);
	sw_reconsegs.rotate(s.reconsegs);
}
