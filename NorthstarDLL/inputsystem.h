#pragma once
struct CandidateList
{
  public:
	// std::shared_ptr<std::vector<std::wstring>> m_pCandidates = std::make_shared<std::vector<std::wstring>>();
	// std::mutex m_mutex;
	std::atomic<std::shared_ptr<std::vector<std::wstring>>> m_pCandidates = std::make_shared<std::vector<std::wstring>>();

	void Reset()
	{
		m_pCandidates = std::make_shared<std::vector<std::wstring>>();
	}

	/*std::shared_ptr<std::vector<std::wstring>> Get()
	{
		std::scoped_lock l(m_mutex);
		return m_pCandidates;
	}

	std::shared_ptr<std::vector<std::wstring>> Swap(std::shared_ptr<std::vector<std::wstring>>& swapee)
	{
		std::scoped_lock l(m_mutex);
		m_pCandidates = swapee;
		swapee.reset();
	}*/

	/*void Lock()
	{
		m_mutex.lock();
	}

	void Unlock()
	{
		m_mutex.lock();
	}*/
};
extern CandidateList m_CandidateList;
