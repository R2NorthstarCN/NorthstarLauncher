#include <mimalloc.h>

typedef size_t (*MemAllocFailHandler_t)(size_t);

struct GenericMemoryStat_t
{
	const char* name;
	int value;
};

struct CMiMalloc
{
	virtual void* AllocDebug(size_t nSize)
	{
		return mi_malloc(nSize);
	}
	virtual void* Alloc(size_t nSize)
	{
		return mi_malloc(nSize);
	}

	virtual void* ReallocDebug(void* pMem, size_t nSize)
	{
		return mi_realloc(pMem, nSize);
	}
	virtual void* Realloc(void* pMem, size_t nSize)
	{
		return mi_realloc(pMem, nSize);
	}

	virtual void FreeDebug(void* pMem)
	{
		return mi_free(pMem);
	}
	virtual void Free(void* pMem)
	{
		return mi_free(pMem);
	}

	virtual void* Expand_NoLongerSupportedDebug(void* pMem, size_t nSize)
	{
		return 0;
	}
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize)
	{
		return 0;
	}

	virtual size_t GetSize(void* pMem)
	{
		return mi_usable_size(pMem);
	}

	virtual void PushAllocDbgInfo(const char* pFileName, int nLine) {}
	virtual void PopAllocDbgInfo() {}

	virtual __int32 CrtSetBreakAlloc(__int32 lNewBreakAlloc)
	{
		return 0;
	}

	virtual int CrtSetReportMode(int nReportType, int nReportMode)
	{
		return 0;
	}

	virtual int CrtIsValidHeapPointer(const void* pMem)
	{
		return 1;
	}

	virtual int CrtIsValidPointer(const void* pMem, unsigned int size, int access)
	{
		return 1;
	}

	virtual int CrtCheckMemory()
	{
		return 1;
	}

	virtual int CrtSetDbgFlag(int nNewFlag)
	{
		return 0;
	}

	virtual void CrtMemCheckpoint(_CrtMemState* pState) {}

	// TODO 1
	virtual void DumpStats() {}
	virtual void DumpStatsFileBase(char const* pchFileBase) {}

	virtual size_t ComputeMemoryUsedBy(char const* pchSubStr)
	{
		return 0;
	}

	virtual __int64 nullsub_1()
	{
		return 0xDC00000;
	}

	virtual void* CrtSetReportFile(int nRptType, void* hFile)
	{
		return 0;
	}

	virtual void* CrtSetReportHook(void* pfnNewHook)
	{
		return 0;
	}

	virtual int CrtDbgReport(int nRptType, const char* szFile, int nLine, const char* szModule, const char* pMsg)
	{
		return 0;
	}

	// TODO 2
	virtual int heapchk() {}

	virtual bool IsDebugHeap()
	{
		return false;
	}

	virtual void GetActualDbgInfo(const char*& pFileName, int& nLine) {}
	virtual void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) {}
	virtual void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) {}

	virtual int GetVersion()
	{
		return 1;
	}

	// TODO 3
	virtual void CompactHeap() {}

	virtual MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler)
	{
		MemAllocFailHandler_t old = m_pfnFailHandler;
		m_pfnFailHandler = pfnMemAllocFailHandler;
		return old;
	}

	// TODO 4
	virtual void DumpBlockStats(void* pMem) {}

	virtual void SetStatsExtraInfo(const char* pMapName, const char* pComment) {}

	virtual size_t MemoryAllocFailed()
	{
		return m_sMemoryAllocFailed;
	}

	// TODO 5
	virtual void CompactIncremental() {}

	virtual void OutOfMemory(size_t nBytesAttempted = 0) {}

	// TODO 6
	virtual void* RegionAllocDebug(int region, size_t nSize) {}
	virtual void* RegionAlloc(int region, size_t nSize) {}

	virtual void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory) {}

	virtual __int64 AllocateVirtualMemorySection(size_t numMaxBytes)
	{
		return 0;
	}

	virtual int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats) {}

	virtual ~CMiMalloc() {};

	MemAllocFailHandler_t m_pfnFailHandler;
	size_t m_sMemoryAllocFailed;
};
