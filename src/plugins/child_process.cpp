#include "plugins/child_process.hpp"

#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace sessionflow
{

#if defined(_WIN32)

namespace
{

std::wstring quoted(const std::wstring& argument)
{
    std::wstring result = L"\"";
    for (const wchar_t character : argument)
    {
        if (character == L'"')
        {
            result += L'\\';
        }
        result += character;
    }
    result += L'"';
    return result;
}

} // namespace

ChildProcess::ChildProcess(const std::filesystem::path& executable, const std::vector<std::string>& arguments)
{
    std::wstring commandLine = quoted(executable.wstring());
    for (const std::string& argument : arguments)
    {
        commandLine += L' ';
        commandLine += quoted(std::filesystem::path(argument).wstring());
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &startup, &info))
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "start process");
    }
    CloseHandle(info.hThread);
    m_process = info.hProcess;
}

ChildProcess::~ChildProcess()
{
    CloseHandle(m_process);
}

void ChildProcess::waitForExit() noexcept
{
    WaitForSingleObject(m_process, INFINITE);
}

void ChildProcess::kill() noexcept
{
    TerminateProcess(m_process, 1);
}

long long currentProcessId() noexcept
{
    return static_cast<long long>(GetCurrentProcessId());
}

bool isProcessAlive(long long processId) noexcept
{
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr)
    {
        return false;
    }
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

#else

ChildProcess::ChildProcess(const std::filesystem::path& executable, const std::vector<std::string>& arguments)
{
    std::string path = executable.string();
    std::vector<std::string> storage = arguments;
    std::vector<char*> argv;
    argv.push_back(path.data());
    for (std::string& argument : storage)
    {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int error = posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ);
    if (error != 0)
    {
        throw std::system_error(error, std::generic_category(), "start process");
    }
    m_pid = pid;
}

ChildProcess::~ChildProcess() = default;

void ChildProcess::waitForExit() noexcept
{
    int status = 0;
    while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    m_exited.store(true, std::memory_order_release);
}

void ChildProcess::kill() noexcept
{
    if (!m_exited.load(std::memory_order_acquire))
    {
        ::kill(m_pid, SIGKILL);
    }
}

long long currentProcessId() noexcept
{
    return static_cast<long long>(getpid());
}

bool isProcessAlive(long long processId) noexcept
{
    return ::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM;
}

#endif

} // namespace sessionflow
