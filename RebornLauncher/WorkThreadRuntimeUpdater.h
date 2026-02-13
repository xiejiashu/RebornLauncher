#pragma once

class WorkThread;

namespace workthread::runtimeupdate {

class WorkThreadRuntimeUpdater {
public:
	explicit WorkThreadRuntimeUpdater(WorkThread& worker);
	bool Execute();

private:
	WorkThread& m_worker;
};

} // namespace workthread::runtimeupdate
