#include "pch.h"
#include "squirrel/squirrel.h"
#include "masterserver/masterserver.h"
#include "server/auth/serverauthentication.h"
#include "core/hooks.h"
#include "core/anticheat.h"
#include "client/r2client.h"
AUTOHOOK_INIT()

// string function NSGetLocalPlayerUID()
SQRESULT NSGetLocalPlayerUID(HSquirrelVM*sqvm)
{
	g_pSquirrel<ScriptContext::CLIENT>->pushstring(sqvm, R2::g_pLocalPlayerUserID, -1);
	return SQRESULT_NOTNULL;
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptPlayerInfo, ClientSquirrel, (CModule module))
{
	g_pSquirrel<ScriptContext::CLIENT>->AddFuncRegistration("string", "NSGetLocalPlayerUID", "", "", NSGetLocalPlayerUID);
}
