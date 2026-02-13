#pragma once

class WorkThread;

namespace workthread::versionload {

class WorkThreadLocalVersionLoader {
public:
	explicit WorkThreadLocalVersionLoader(WorkThread& worker);
	void Execute();

private:
	WorkThread& m_worker;
};

} // namespace workthread::versionload
