#include "pch.h"
#include "netgraph.h"

AUTOHOOK_INIT()

float* g_frameTime;

ON_DLL_LOAD("engine.dll", SERVERFPS, (CModule module))
{
	g_frameTime = module.Offset(0x13158ba0).As<float*>();
	AUTOHOOK_DISPATCH();
}
