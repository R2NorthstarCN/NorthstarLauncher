#pragma once

typedef size_t (*MemAllocFailHandler_t)(size_t);

struct GenericMemoryStat_t
{
	const char* name;
	int value;
};

class CMiMalloc
{
  public:
	virtual void* AllocDebug(size_t nSize);
	virtual void* Alloc(size_t nSize);
	virtual void* ReallocDebug(void* pMem, size_t nSize);
	virtual void* Realloc(void* pMem, size_t nSize);
	virtual void FreeDebug(void* pMem);
	virtual void Free(void* pMem);
	virtual void* Expand_NoLongerSupportedDebug(void* pMem, size_t nSize);
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize);
	virtual size_t GetSize(void* pMem);
	virtual void PushAllocDbgInfo(const char* pFileName, int nLine);
	virtual void PopAllocDbgInfo();
	virtual __int32 CrtSetBreakAlloc(__int32 lNewBreakAlloc);
	virtual int CrtSetReportMode(int nReportType, int nReportMode);
	virtual int CrtIsValidHeapPointer(const void* pMem);
	virtual int CrtIsValidPointer(const void* pMem, unsigned int size, int access);
	virtual int CrtCheckMemory();
	virtual int CrtSetDbgFlag(int nNewFlag);
	virtual void CrtMemCheckpoint(_CrtMemState* pState);
	virtual void DumpStats();
	virtual void DumpStatsFileBase(char const* pchFileBase);
	virtual size_t ComputeMemoryUsedBy(char const* pchSubStr);
	virtual __int64 nullsub_1();
	virtual void* CrtSetReportFile(int nRptType, void* hFile);
	virtual void* CrtSetReportHook(void* pfnNewHook);
	virtual int CrtDbgReport(int nRptType, const char* szFile, int nLine, const char* szModule, const char* pMsg);
	virtual int heapchk();
	virtual bool IsDebugHeap();
	virtual void GetActualDbgInfo(const char*& pFileName, int& nLine);
	virtual void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime);
	virtual void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime);
	virtual int GetVersion();
	virtual void CompactHeap();
	virtual MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler);
	virtual void DumpBlockStats(void* pMem);
	virtual void SetStatsExtraInfo(const char* pMapName, const char* pComment);
	virtual size_t MemoryAllocFailed();
	virtual void CompactIncremental();
	virtual void OutOfMemory(size_t nBytesAttempted = 0);
	virtual void* RegionAllocDebug(int region, size_t nSize);
	virtual void* RegionAlloc(int region, size_t nSize);
	virtual void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory);
	virtual __int64 AllocateVirtualMemorySection(size_t numMaxBytes);
	virtual int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats);
	virtual ~CMiMalloc();

	static CMiMalloc* instance();

  private:
	MemAllocFailHandler_t m_pfnFailHandler;
	size_t m_sMemoryAllocFailed = 0;
};
