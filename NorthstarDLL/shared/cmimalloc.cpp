#include "cmimalloc.h"
#include <mimalloc.h>

void* CMiMalloc::AllocDebug(size_t nSize)
{
	return mi_malloc(nSize);
}

void* CMiMalloc::Alloc(size_t nSize)
{
	return mi_malloc(nSize);
}

void* CMiMalloc::ReallocDebug(void* pMem, size_t nSize)
{
	return mi_realloc(pMem, nSize);
}

void* CMiMalloc::Realloc(void* pMem, size_t nSize)
{
	return mi_realloc(pMem, nSize);
}

void CMiMalloc::FreeDebug(void* pMem)
{
	return mi_free(pMem);
}

void CMiMalloc::Free(void* pMem)
{
	return mi_free(pMem);
}

void* CMiMalloc::Expand_NoLongerSupportedDebug(void* pMem, size_t nSize)
{
	return nullptr;
}

void* CMiMalloc::Expand_NoLongerSupported(void* pMem, size_t nSize)
{
	return nullptr;
}

size_t CMiMalloc::GetSize(void* pMem)
{
	return mi_usable_size(pMem);
}

void CMiMalloc::PushAllocDbgInfo(const char* pFileName, int nLine) {}

void CMiMalloc::PopAllocDbgInfo() {}

__int32 CMiMalloc::CrtSetBreakAlloc(__int32 lNewBreakAlloc)
{
	return 0;
}

int CMiMalloc::CrtSetReportMode(int nReportType, int nReportMode)
{
	return 0;
}

int CMiMalloc::CrtIsValidHeapPointer(const void* pMem)
{
	return 1;
}

int CMiMalloc::CrtIsValidPointer(const void* pMem, unsigned int size, int access)
{
	return 1;
}

int CMiMalloc::CrtCheckMemory()
{
	return 1;
}

int CMiMalloc::CrtSetDbgFlag(int nNewFlag)
{
	return 0;
}

void CMiMalloc::CrtMemCheckpoint(_CrtMemState* pState) {}

void CMiMalloc::DumpStats()
{
	return mi_stats_print(NULL);
}

void PrintToFile(const char* msg, void* arg) {
	FILE* fp = (FILE*)arg;
	fprintf_s(fp, msg);
}

void CMiMalloc::DumpStatsFileBase(char const* pchFileBase)
{
	std::string filename(pchFileBase);
	filename.append(".txt");
	FILE* fp;
	errno_t err = fopen_s(&fp, filename.c_str(), "w+");
	if (err != 0)
	{
		return;
	}
	mi_stats_print_out(PrintToFile, fp);
}

size_t CMiMalloc::ComputeMemoryUsedBy(char const* pchSubStr)
{
	return 0;
}

__int64 CMiMalloc::nullsub_1()
{
	return 0xDC00000;
}

void* CMiMalloc::CrtSetReportFile(int nRptType, void* hFile)
{
	return nullptr;
}

void* CMiMalloc::CrtSetReportHook(void* pfnNewHook)
{
	return nullptr;
}

int CMiMalloc::CrtDbgReport(int nRptType, const char* szFile, int nLine, const char* szModule, const char* pMsg)
{
	return 0;
}

int CMiMalloc::heapchk()
{
	return _HEAPOK;
}

bool CMiMalloc::IsDebugHeap()
{
	return false;
}

void CMiMalloc::GetActualDbgInfo(const char*& pFileName, int& nLine) {}

void CMiMalloc::RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) {}

void CMiMalloc::RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) {}

int CMiMalloc::GetVersion()
{
	return 1;
}

void CMiMalloc::CompactHeap()
{
	return mi_collect(false);
}

MemAllocFailHandler_t CMiMalloc::SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler)
{
	auto old = m_pfnFailHandler;
	m_pfnFailHandler = pfnMemAllocFailHandler;
	return old;
}

void CMiMalloc::DumpBlockStats(void* pMem) {}

void CMiMalloc::SetStatsExtraInfo(const char* pMapName, const char* pComment) {}

size_t CMiMalloc::MemoryAllocFailed()
{
	return m_sMemoryAllocFailed;
}

void CMiMalloc::CompactIncremental() {}

void CMiMalloc::OutOfMemory(size_t nBytesAttempted) {}

void* CMiMalloc::RegionAllocDebug(int region, size_t nSize)
{
	return mi_malloc(nSize);
}

void* CMiMalloc::RegionAlloc(int region, size_t nSize)
{
	return mi_malloc(nSize);
}

void CMiMalloc::GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory)
{
	if (pUsedMemory && pFreeMemory)
	{
		*pUsedMemory = 0;
		*pFreeMemory = 0;
	}
}

__int64 CMiMalloc::AllocateVirtualMemorySection(size_t numMaxBytes)
{
	return 0;
}

int CMiMalloc::GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats)
{
	if (ppMemoryStats)
	{
		*ppMemoryStats = nullptr;
	}

	return 0;
}

CMiMalloc::~CMiMalloc() {}

CMiMalloc* CMiMalloc::instance()
{
	return nullptr;
}






