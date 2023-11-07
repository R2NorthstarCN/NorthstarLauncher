#pragma once

#include <svm.h>


// userdata NSSvmTrain(players origin)
SQRESULT NSSvmTrain(HSquirrelVM* sqvm)
{
	//g_pSquirrel<ScriptContext::SERVER>->__sq_get
	SQArray* array = sqvm->_stackOfCurrentFunction[1]._VAL.asArray;
	std::vector<Vector3> origins;

	for (int vIdx = 0; vIdx < array->_usedSlots; ++vIdx)
	{
		if (array->_values[vIdx]._Type == OT_STRING)
		{
			origins.push_back(Vector3(
				array->_values[vIdx]._VAL.asVector->x, array->_values[vIdx]._VAL.asVector->y, array->_values[vIdx]._VAL.asVector->z));
		}
	}

	// origins operations


	return SQRESULT_NOTNULL;
}

// integer NSSvmPredict(point origin)
SQRESULT NSSvmPredict(HSquirrelVM* sqvm)
{
	Vector3 vec = g_pSquirrel<ScriptContext::CLIENT>->getvector(sqvm,1);

	// code
	int result = 0;
	g_pSquirrel<ScriptContext::SERVER>->pushinteger(sqvm, result);
	return SQRESULT_NOTNULL;
}


ON_DLL_LOAD_CLIENT_RELIESON("client.dll", ScriptPlayerInfo, ClientSquirrel, (CModule module))
{
	g_pSquirrel<ScriptContext::SERVER>->AddFuncRegistration("userdata", "NSSvmTrain", "array origins", "", NSSvmTrain);
	g_pSquirrel<ScriptContext::SERVER>->AddFuncRegistration("int", "NSSvmPredict", "vector origin", "", NSSvmPredict);
}
