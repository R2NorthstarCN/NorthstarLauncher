typedef size_t (*MemAllocFailHandler_t)(size_t);

struct GenericMemoryStat_t
{
	const char* name;
	int value;
};

struct CMiMalloc
{
	virtual void* AllocDebug(size_t nSize) {}
	virtual void* Alloc(size_t nSize) {}
	virtual void* ReallocDebug(void* pMem, size_t nSize) {}
	virtual void* Realloc(void* pMem, size_t nSize) {}
	virtual void FreeDebug(void* pMem) {}
	virtual void Free(void* pMem) {}
	virtual void* Expand_NoLongerSupportedDebug(void* pMem, size_t nSize) {}
	virtual void* Expand_NoLongerSupported(void* pMem, size_t nSize) {}
	virtual size_t GetSize(void* pMem) {}
	virtual void PushAllocDbgInfo(const char* pFileName, int nLine) = 0;
	virtual void PopAllocDbgInfo() = 0;
	virtual __int32 CrtSetBreakAlloc(__int32 lNewBreakAlloc) = 0;
	virtual int CrtSetReportMode(int nReportType, int nReportMode) = 0;
	virtual int CrtIsValidHeapPointer(const void* pMem) = 0;
	virtual int CrtIsValidPointer(const void* pMem, unsigned int size, int access) = 0;
	virtual int CrtCheckMemory(void) = 0;
	virtual int CrtSetDbgFlag(int nNewFlag) = 0;
	virtual void CrtMemCheckpoint(_CrtMemState* pState) = 0;
	virtual void DumpStats() = 0;
	virtual void DumpStatsFileBase(char const* pchFileBase) = 0;
	virtual size_t ComputeMemoryUsedBy(char const* pchSubStr) = 0;
	virtual __int64 nullsub_1() = 0;
	virtual void* CrtSetReportFile(int nRptType, void* hFile) = 0;
	virtual void* CrtSetReportHook(void* pfnNewHook) = 0;
	virtual int CrtDbgReport(int nRptType, const char* szFile, int nLine, const char* szModule, const char* pMsg) = 0;
	virtual int heapchk() = 0;
	virtual bool IsDebugHeap() = 0;
	virtual void GetActualDbgInfo(const char*& pFileName, int& nLine) = 0;
	virtual void RegisterAllocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;
	virtual void RegisterDeallocation(const char* pFileName, int nLine, size_t nLogicalSize, size_t nActualSize, unsigned nTime) = 0;
	virtual int GetVersion() = 0;
	virtual void CompactHeap() = 0;
	virtual MemAllocFailHandler_t SetAllocFailHandler(MemAllocFailHandler_t pfnMemAllocFailHandler) = 0;
	virtual void DumpBlockStats(void*) = 0;
	virtual void SetStatsExtraInfo(const char* pMapName, const char* pComment) = 0;
	virtual size_t MemoryAllocFailed() = 0;
	virtual void CompactIncremental() = 0;
	virtual void OutOfMemory(size_t nBytesAttempted = 0) = 0;
	virtual void* RegionAllocDebug(int region, size_t nSize) = 0;
	virtual void* RegionAlloc(int region, size_t nSize) = 0;
	virtual void GlobalMemoryStatus(size_t* pUsedMemory, size_t* pFreeMemory) = 0;
	virtual __int64 AllocateVirtualMemorySection(size_t numMaxBytes) = 0;
	virtual int GetGenericMemoryStats(GenericMemoryStat_t** ppMemoryStats) = 0;
	virtual ~CMiMalloc() {};
};
