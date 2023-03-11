#include "pch.h"
AUTOHOOK_INIT()
#define _QWORD unsigned __int64
#define _DWORD DWORD

std::string netgui_str;
_QWORD netgraph_qword_a1;
_QWORD* qword_193159AC8;
_DWORD* dword_193158BA0;
_DWORD* dword_193158BC8;
_DWORD* dword_19315AD80;
_DWORD* dword_19307E200;
char* word_19307E240;

__int64 Q_snprintf(char* a1, signed __int64 a2, const char* a3, ...)
{
	__int64 result; // rax
	va_list ArgList; // [rsp+58h] [rbp+20h] BYREF

	va_start(ArgList, a3);
	if (a2 <= 0)
		return 0i64;
	result = vsnprintf(a1, a2, a3, ArgList);
	if ((int)result < 0i64 || (int)result >= a2)
	{
		result = a2 - 1;
		a1[a2 - 1] = 0;
	}
	return result;
}
AUTOHOOK(NetGraphString, engine.dll + 0x158710, __int64, __fastcall, (__int64 a1))
{
	double v1; // xmm0_8
	double v2; // xmm1_8
	int v3; // edx
	float v4; // xmm7_4
	double v5; // xmm0_8
	float v6; // xmm6_4
	double v7; // xmm1_8
	float v8; // xmm5_4
	double v9; // xmm0_8
	float v10; // xmm4_4
	double v11; // xmm0_8
	float v12; // xmm3_4
	double v13; // xmm1_8
	float v14; // xmm9_4
	double v15; // xmm0_8
	float v16; // xmm10_4
	float v17; // xmm2_4
	double v18; // xmm8_8
	__int64 result; // rax
	int v20; // ecx
	int v21; // r10d
	int v22; // [rsp+1B0h] [rbp+1B0h]
	int v23; // [rsp+1B8h] [rbp+1B8h]
	__int64 v24; // [rsp+200h] [rbp+200h] BYREF

	v1 = *(double*)(a1 + 96);
	v2 = *(double*)(a1 + 112);
	v3 = 0;
	*(_QWORD*)(a1 + 96) = 0i64;
	*(_QWORD*)(a1 + 112) = 0i64;
	v4 = v1 * 1000.0;
	v5 = *(double*)(a1 + 104);
	v6 = v2 * 1000.0;
	*(_QWORD*)(a1 + 104) = 0i64;
	v7 = *(double*)(a1 + 128);
	v8 = v5 * 1000.0;
	v9 = *(double*)(a1 + 120);
	*(_QWORD*)(a1 + 120) = 0i64;
	*(_QWORD*)(a1 + 128) = 0i64;
	v10 = v9 * 1000.0;
	v11 = *(double*)(a1 + 136);
	v12 = v7 * 1000.0;
	*(_QWORD*)(a1 + 136) = 0i64;
	v13 = *(double*)(a1 + 144);
	v14 = v11 * 1000.0;
	v15 = *(double*)(a1 + 152);
	*(_QWORD*)(a1 + 144) = 0i64;
	*(_QWORD*)(a1 + 152) = 0i64;
	*(float*)(a1 + 160) = v6;
	if (*(float*)&*dword_193158BA0 >= 0.0001)
		v18 = 1.0 / *(float*)&*dword_193158BA0;
	else
		v18 = 999.0;
	result = *qword_193159AC8;
	long long alt_result = 0;
	v20 = 0;
	v21 = *dword_193158BC8 - *dword_19315AD80;
	for (*dword_19315AD80 = *dword_193158BC8; v20 < *dword_19307E200; ++v20)
	{
		if (word_19307E240[(__int16)v20])
			++v3;
	}
	v23 = v21;
	v22 = v3;
	v17 = v15 * 1000.0;
	v16 = v13 * 1000.0;
	char* target = new char[1024];
	alt_result = Q_snprintf(
		target,
		256i64,
		"%3i fps -- inp(%4.2f) sv(%4.2f) cl(%4.2f) render(%4.2f) snd(%4.2f) cl_dll(%4.2f) exec(%4.2f) paks(%4.2f) en"
		"ts(%d) ticks(%d)",
		(unsigned int)(int)v18,
		v4,
		v6,
		v8,
		v10,
		v12,
		v14,
		v16,
		v17,
		v22,
		v23);
	netgui_str = std::string(target);
	delete[] target;
	if (*(_DWORD*)(qword_193159AC8 + 92))
	{
		return alt_result;
	}
	else
	{
		return result;
	}
}

ON_DLL_LOAD_RELIESON("engine.dll", NETGRAPH, (ConCommand, ConVar), (CModule module))
{
	dword_193158BA0 = module.Offset(0x13158ba0).As<_DWORD*>();
	qword_193159AC8 = module.Offset(0x13159ac8).As<_QWORD*>();
	dword_193158BC8 = module.Offset(0x13158bc8).As<_DWORD*>();
	dword_19315AD80 = module.Offset(0x1315ad80).As<_DWORD*>();
	dword_19307E200 = module.Offset(0x1307e200).As<_DWORD*>();
	word_19307E240 = module.Offset(0x1307e240).As<char*>();
	AUTOHOOK_DISPATCH();
}
