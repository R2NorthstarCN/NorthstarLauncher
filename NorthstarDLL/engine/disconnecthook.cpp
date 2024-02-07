#include "pch.h"
#include "shared/kcpintegration.h"

AUTOHOOK_INIT()

AUTOHOOK(CClientState_Disconnect, engine.dll + 0x8DC50, __int64, __fastcall, (__int64 a1, char a2))
// clang-format on
{
	auto nm = NetManager::instance();
	std::shared_lock lk(nm->routingTableMutex);
	for (auto& conn : nm->routingTable)
	{
		MicroPacketSink::instance()->sendMicroPacket(MicroPacketOpcode::CLIENT_DISCONNECTING, NetBuffer(32, '\0', 32), conn.first);
		conn.second.disconnected = true;
		NS::log::NEW_NET->info("[MicroPacketSink] Sent disconnecting packet to {}", conn.first);
	}
	return CClientState_Disconnect(a1,a2);
}

ON_DLL_LOAD("engine.dll", DISCONNECTHOOK, (CModule module))
{
	AUTOHOOK_DISPATCH();
}
