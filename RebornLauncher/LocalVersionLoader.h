#pragma once

class LauncherUpdateCoordinator;

namespace workthread::versionload {

class LocalVersionLoader {
public:
	explicit LocalVersionLoader(LauncherUpdateCoordinator& worker);
	void Execute();

private:
	LauncherUpdateCoordinator& m_worker;
};

} // namespace workthread::versionload
