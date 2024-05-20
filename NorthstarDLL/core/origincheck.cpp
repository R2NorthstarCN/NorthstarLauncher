#include "pch.h"
#include "tier0.h"
#include "hooks.h"


AUTOHOOK_INIT()
// clang-format off
AUTOHOOK(InitialiseOrigin, engine.dll + 0x183780, void, __fastcall,())
// clang-format on
{
	__int64 result = -1;
	result = Tier0::Tier0_GetOriginStartupResult();
	if (result != 0)
	{
		spdlog::error("[Origin] Origin initialization error! Result: {}", result);
		int msgboxID = MessageBox(
			NULL,
			(LPCWSTR)L"Origin/EA\x521D\x59CB\x5316\x5931\x8D25\xFF0C\x8BF7\x5C1D\x8BD5\x91CD\x65B0\x542F\x52A8\Origin\x6216\EA App\x540E\x518D\x8BD5\x3002\x82E5\x4ECD\x65E0\x6CD5\x6B63\x5E38\x542F\x52A8\x5317\x6781\x661F\CN\xFF0C\x8BF7\x5C1D\x8BD5\x91CD\x542F\x60A8\x7684\x8BA1\x7B97\x673A\x3002",
			(LPCWSTR)L"\x5317\x6781\x661F\CN\x521D\x59CB\x5316\x9519\x8BEF",
			MB_OK | MB_SETFOREGROUND | MB_TOPMOST | MB_ICONERROR);
	}
	spdlog::info("[Origin] Origin Initialization result: {}", result);
	return InitialiseOrigin();
}

ON_DLL_LOAD("engine.dll", InitialiseOriginHooks, (CModule module))
{
	AUTOHOOK_DISPATCH();
}
