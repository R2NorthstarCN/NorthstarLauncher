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

sliding_window::sliding_window(size_t samples)
{
	raw = std::vector<double>(samples);
	for (int i = raw.size() - 1; i >= 0; i--)
	{
		axis_x.push_back(i);
	}
}

sliding_window::sliding_window(size_t samples, bool delta)
{
	this->delta = delta;
	raw = std::vector<double>(samples);
	for (int i = raw.size() - 1; i >= 0; i--)
	{
		axis_x.push_back(i);
	}
}

void sliding_window::rotate(double new_val)
{
	sumed -= *(raw.end() - 1);
	std::rotate(raw.rbegin(), raw.rbegin() + 1, raw.rend());
	if (delta)
	{
		raw[0] = new_val - last_val;
		last_val = new_val;
	}
	else
	{
		raw[0] = new_val;
	}
	sumed += raw[0];
}

std::pair<std::vector<double>&, std::vector<double>&> sliding_window::get_axes()
{
	return std::make_pair(std::ref(axis_x), std::ref(raw));
}

double sliding_window::last() const
{
	return raw[0];
}

double sliding_window::avg() const
{
	return sumed / raw.size();
}

double sliding_window::sum() const
{
	return sumed;
}

double sliding_window::max() const
{
	return *std::max_element(raw.begin(), raw.end());
}

double sliding_window::min() const
{
	return *std::min_element(raw.begin(), raw.end());
}
