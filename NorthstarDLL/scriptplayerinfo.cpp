#include "pch.h"
#include "squirrel.h"
#include "masterserver.h"
#include "serverauthentication.h"
#include "hooks.h"
#include "anticheat.h"
#include "r2client.h"
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
