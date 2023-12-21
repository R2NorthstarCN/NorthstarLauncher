#include "squirrel/squirrel.h"
#include <svm.h>

const float COORD_MIN = -16384.0;
const float COORD_LEN = 32767.0;

struct SvmPoint
{
	Vector3 origin;
	int faction;
};

void releasehook(void* val, int size)
{
	svm_free_and_destroy_model((svm_model**)val);
}

void svm_print_string(const char* str)
{
}

ADD_SQFUNC(
	"userdata",
	NSSvmTrain,
	"array points",
	"train svm model based on provided points",
	ScriptContext::CLIENT | ScriptContext::SERVER | ScriptContext::UI)
{
	SQArray* originsArray = sqvm->_stackOfCurrentFunction[1]._VAL.asArray;
	std::vector<SvmPoint> points;

	for (int vIdx = 0; vIdx < originsArray->_usedSlots; ++vIdx)
	{
		if (originsArray->_values[vIdx]._Type == OT_TABLE)
		{
			SQTable* originTable = originsArray->_values[vIdx]._VAL.asTable;
			SvmPoint newpoint;
			for (int idx = 0; idx < originTable->_numOfNodes; ++idx)
			{
				tableNode* node = &originTable->_nodes[idx];

				if (node->val._Type == OT_VECTOR)
				{
					SQVector* v = (SQVector*)node;
					newpoint.origin = Vector3(v->x,v->y, v->z);
				}
				if (node->val._Type == OT_INTEGER)
				{
					newpoint.faction = node->val._VAL.as64Integer;
				}
			}
			points.push_back(newpoint);
		}
	}

	svm_parameter param;
	param.svm_type = C_SVC;
	param.kernel_type = RBF;
	param.degree = 3;
	param.gamma = 1.0/3;
	param.coef0 = 0;
	param.nu = 0.5;
	param.cache_size = 100;
	param.C = 1000;
	param.eps = 1e-3;
	param.p = 0.1;
	param.shrinking = 1;
	param.probability = 0;
	param.nr_weight = 0;
	param.weight_label = NULL;
	param.weight = NULL;

	svm_problem prob;
	prob.l = points.size();
	prob.y = new double[prob.l];
	svm_node* x_space = new svm_node[4 * prob.l];
	prob.x = new svm_node*[prob.l];

	for (int i = 0; i < points.size(); ++i)
	{
		x_space[4 * i].index = 1;
		x_space[4 * i].value = ((points[i].origin.x - COORD_MIN) / COORD_LEN);
		x_space[4 * i + 1].index = 2;
		x_space[4 * i + 1].value = ((points[i].origin.y - COORD_MIN) / COORD_LEN);
		x_space[4 * i + 2].index = 3;
		x_space[4 * i + 2].value = ((points[i].origin.z - COORD_MIN) / COORD_LEN);
		x_space[4 * i + 3].index = -1;
		prob.x[i] = &x_space[4 * i];
		prob.y[i] = points[i].faction;
	}
	svm_set_print_string_function(svm_print_string);
	svm_model* model = svm_train(&prob, &param);
	svm_model** userdata_model = g_pSquirrel<context>->createuserdata<svm_model*>(sqvm, 8);
	*userdata_model = model;

	SQUserData* userdata = (SQUserData*)(((uintptr_t)userdata_model) - 80);
	userdata->releaseHook = releasehook;
	SQObject* object = new SQObject;
	object->_Type = OT_USERDATA;
	object->structNumber = 0;
	object->_VAL.asUserdata = userdata;
	g_pSquirrel<context>->pushobject(sqvm, object);
	return SQRESULT_NOTNULL;
}

ADD_SQFUNC(
	"int",
	NSSvmPredict,
	"userdata model, vector origin",
	"predict things",
	ScriptContext::CLIENT | ScriptContext::SERVER | ScriptContext::UI)
{
	SQObject* obj = new SQObject;
	svm_model** model = nullptr;
	g_pSquirrel<context>->__sq_getobject(sqvm, 1, obj);
	model = (svm_model**)obj->_VAL.asUserdata->data;
	Vector3 vec =g_pSquirrel<context>->getvector(sqvm, 2);

	svm_node x[4];
	x[0].index = 1;
	x[0].value = (vec.x - COORD_MIN) / COORD_LEN;
	x[1].index = 2;
	x[1].value = (vec.y - COORD_MIN) / COORD_LEN;
	x[2].index = 3;
	x[2].value = (vec.z - COORD_MIN) / COORD_LEN;
	x[3].index = -1;
	// code
	int result = svm_predict(*model, x);
	g_pSquirrel<context>->pushinteger(sqvm, result);
	return SQRESULT_NOTNULL;
}
