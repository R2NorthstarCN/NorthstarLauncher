#pragma once
#include <queue>

class MasterserverMessenger
{
  public:
	std::queue<std::string> m_vQueuedMasterserverMessages;
};

extern MasterserverMessenger* g_pMasterserverMessenger;
