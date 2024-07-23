#pragma once
#include <fstream>
#include <string>

typedef unsigned long       DWORD;

class TempReadWrite
{
  private:
	DWORD m_origProtection;
	void* m_ptr;

  public:
	TempReadWrite(void* ptr);
	~TempReadWrite();
};
class ClientAnticheatSystem
{
  public:
	void NoFindWindowHack(uintptr_t baseAddress);
};

extern ClientAnticheatSystem g_ClientAnticheatSystem;
