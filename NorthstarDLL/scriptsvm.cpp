#include "squirrel/squirrel.h"
#include <svm.h>

struct SvmPoint
{
	Vector3 origin;
	int faction;
};
struct SvmLimits
{
	float x_max = 0.0f;
	float x_min = 0.0f;
	float y_max = 0.0f;
	float y_min = 0.0f;
	float z_max = 0.0f;
	float z_min = 0.0f;
};

void releasehook(void* val, int size)
{
	spdlog::error("Release hook: {:p} {}", val, size);
	svm_free_and_destroy_model((svm_model**)val);
	SvmLimits** userdata_limits = (SvmLimits**)((uintptr_t)val + 8);
	delete *userdata_limits;
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
	param.gamma = 0.5;
	param.coef0 = 0;
	param.nu = 0.5;
	param.cache_size = 100;
	param.C = 1;
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

	SvmLimits* limits = new SvmLimits;

	limits->x_max = std::numeric_limits<float>::min();
	limits->x_min = std::numeric_limits<float>::max();
	limits->y_max = std::numeric_limits<float>::min();
	limits->y_min = std::numeric_limits<float>::max();
	limits->z_max = std::numeric_limits<float>::min();
	limits->z_min = std::numeric_limits<float>::max();

	for (const auto& p : points)
	{
		limits->x_max = std::max(limits->x_max, p.origin.x);
		limits->x_min = std::min(limits->x_min, p.origin.x);
		limits->y_max = std::max(limits->y_max, p.origin.y);
		limits->y_min = std::min(limits->y_min, p.origin.y);
		limits->z_max = std::max(limits->z_max, p.origin.z);
		limits->z_min = std::min(limits->z_min, p.origin.z);
	}

	//spdlog::info("determined axis limits : x {} - {}, y {} - {}, z {} - {}", x_min, x_max,y_min,y_max,z_min,z_max);
	float x_len = limits->x_max - limits->x_min;
	float y_len = limits->y_max - limits->y_min;
	float z_len = limits->z_max - limits->z_min;

	for (int i = 0; i < points.size(); ++i)
	{
		x_space[4 * i].index = 1;
		x_space[4 * i].value = ((points[i].origin.x - limits->x_min) / x_len);
		x_space[4 * i + 1].index = 2;
		x_space[4 * i + 1].value = ((points[i].origin.y - limits->y_min) / y_len);
		x_space[4 * i + 2].index = 3;
		x_space[4 * i + 2].value = ((points[i].origin.z - limits->z_min) / z_len);
		x_space[4 * i + 3].index = -1;
		prob.x[i] = &x_space[4 * i];
		prob.y[i] = points[i].faction;
	}

	svm_model* model = svm_train(&prob, &param);
	svm_model** userdata_model = g_pSquirrel<context>->createuserdata<svm_model*>(sqvm, 16);
	*userdata_model = model;
	SvmLimits** userdata_limits = (SvmLimits**)(((uintptr_t)userdata_model) + 8);
	*userdata_limits = limits;

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
	SvmLimits** limits = (SvmLimits**)(((uintptr_t)model) + 8);
	Vector3 vec =g_pSquirrel<context>->getvector(sqvm, 2);
	float x_len = (*limits)->x_max - (*limits)->x_min;
	float y_len = (*limits)->y_max - (*limits)->y_min;
	float z_len = (*limits)->z_max - (*limits)->z_min;
	svm_node x[4];
	x[0].index = 1;
	x[0].value = (vec.x - (*limits)->x_min) / x_len;
	x[1].index = 2;
	x[1].value = (vec.y - (*limits)->y_min) / y_len;
	x[2].index = 3;
	x[2].value = (vec.z - (*limits)->z_min) / z_len;
	x[3].index = -1;
	// code
	int result = svm_predict(*model, x);
	g_pSquirrel<context>->pushinteger(sqvm, result);
	return SQRESULT_NOTNULL;
}
