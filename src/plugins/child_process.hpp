#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace sessionflow
{

// A process started by this one. Not killed on destruction: the owner decides when it ends.
class ChildProcess
{
  public:
    // Throws std::system_error when the process can't be started.
    ChildProcess(const std::filesystem::path& executable, const std::vector<std::string>& arguments);
    ~ChildProcess();

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Blocks until the process has exited. Call it once, from one thread.
    void waitForExit() noexcept;

    // Ends the process immediately. Safe to call while another thread is in waitForExit().
    void kill() noexcept;

  private:
#if defined(_WIN32)
    void* m_process = nullptr;
#else
    int m_pid = 0;
    // Keeps kill() from signalling a reaped pid that the system may have reused.
    std::atomic<bool> m_exited{false};
#endif
};

// Id of the calling process, so a child can tell when its parent is gone.
long long currentProcessId() noexcept;

// False once the process with this id has exited.
bool isProcessAlive(long long processId) noexcept;

} // namespace sessionflow
