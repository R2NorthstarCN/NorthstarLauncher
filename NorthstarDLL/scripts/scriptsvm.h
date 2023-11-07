#pragma once

#include <svm.h>


// userdata NSSvmTrain(players origin)
SQRESULT NSSvmTrain(HSquirrelVM* sqvm)
{
	//g_pSquirrel<ScriptContext::SERVER>->__sq_get
	return SQRESULT_NOTNULL;
}

// integer NSSvmPredict(point origin)
SQRESULT NSSvmPredict(HSquirrelVM* sqvm)
{
	Vector3 vec = g_pSquirrel<ScriptContext::CLIENT>->getvector(sqvm,-1);

	// code

	g_pSquirrel<ScriptContext::SERVER>->pushinteger(sqvm,1);
	return SQRESULT_NOTNULL;
}


ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptPlayerInfo, ClientSquirrel, (CModule module))
{
	g_pSquirrel<ScriptContext::SERVER>->AddFuncRegistration("userdata", "NSSvmTrain", "", "", NSSvmTrain);
	g_pSquirrel<ScriptContext::SERVER>->AddFuncRegistration("int", "NSSvmPredict", "vector origin", "", NSSvmPredict);
}
