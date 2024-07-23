
#include "squirrel/squirrel.h"
#include "masterserver/masterserver.h"
#include "server/auth/serverauthentication.h"
#include "client/r2client.h"
AUTOHOOK_INIT()

// string function NSGetLocalPlayerUID()
SQRESULT NSGetLocalPlayerUID(HSquirrelVM*sqvm)
{
	g_pSquirrel<ScriptContext::CLIENT>->pushstring(sqvm, g_pLocalPlayerUserID, -1);
	return SQRESULT_NOTNULL;
}

ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptPlayerInfo, ClientSquirrel, (CModule module))
{
	g_pSquirrel<ScriptContext::CLIENT>->AddFuncRegistration("string", "NSGetLocalPlayerUID", "", "", NSGetLocalPlayerUID);
}
