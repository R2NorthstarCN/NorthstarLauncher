#pragma once

#include <svm.h>


// userdata NSSvmTrain(players origin)
SQRESULT NSSvmTrain(HSquirrelVM* sqvm)
{
	//g_pSquirrel<ScriptContext::SERVER>->__sq_get
	SQArray* array = sqvm->_stackOfCurrentFunction[1]._VAL.asArray;
	std::vector<std::pair<Vector3,int>> origins;



	SQTable* originsTable = sqvm->_stackOfCurrentFunction[1]._VAL.asTable;
	for (int idx = 0; idx < originsTable->_numOfNodes; ++idx)
	{
		tableNode* node = &originsTable->_nodes[idx];

		if (node->key._Type == OT_VECTOR && node->val._Type == OT_INTEGER)
		{
			origins.push_back(
				std::make_pair(Vector3(node->key._VAL.asVector->x, node->key._VAL.asVector->y,node->key._VAL.asVector->z),node->val._VAL.as64Integer));		
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
