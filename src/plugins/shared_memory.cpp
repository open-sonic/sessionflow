#include "plugins/shared_memory.hpp"

#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace sessionflow
{

namespace
{

#if defined(_WIN32)

[[noreturn]] void throwLastError(const char* what)
{
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), what);
}

void* map(const std::string& name, std::size_t size, bool create, void*& handle)
{
    const auto size64 = static_cast<unsigned long long>(size);
    handle = create ? CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         static_cast<DWORD>(size64 >> 32), static_cast<DWORD>(size64), name.c_str())
                    : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (handle == nullptr)
    {
        throwLastError("shared memory");
    }
    if (create && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(handle);
        throw std::system_error(ERROR_ALREADY_EXISTS, std::system_category(), "shared memory");
    }
    void* data = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (data == nullptr)
    {
        const DWORD error = GetLastError();
        CloseHandle(handle);
        throw std::system_error(static_cast<int>(error), std::system_category(), "shared memory");
    }
    return data;
}

#else

[[noreturn]] void throwErrno(const char* what)
{
    throw std::system_error(errno, std::generic_category(), what);
}

void* map(const std::string& name, std::size_t size, bool create, void*&)
{
    const int fd = create ? shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600) : shm_open(name.c_str(), O_RDWR, 0);
    if (fd < 0)
    {
        throwErrno("shared memory");
    }
    if (create && ftruncate(fd, static_cast<off_t>(size)) != 0)
    {
        const int error = errno;
        close(fd);
        shm_unlink(name.c_str());
        throw std::system_error(error, std::generic_category(), "shared memory");
    }
    void* data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int error = errno;
    close(fd);
    if (data == MAP_FAILED)
    {
        if (create)
        {
            shm_unlink(name.c_str());
        }
        throw std::system_error(error, std::generic_category(), "shared memory");
    }
    return data;
}

#endif

} // namespace

SharedMemory SharedMemory::create(const std::string& name, std::size_t size)
{
    SharedMemory memory;
    memory.m_data = map(name, size, true, memory.m_handle);
    memory.m_name = name;
    memory.m_size = size;
    memory.m_owner = true;
    return memory;
}

SharedMemory SharedMemory::open(const std::string& name, std::size_t size)
{
    SharedMemory memory;
    memory.m_data = map(name, size, false, memory.m_handle);
    memory.m_name = name;
    memory.m_size = size;
    return memory;
}

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : m_name(std::move(other.m_name)), m_size(std::exchange(other.m_size, 0)),
      m_data(std::exchange(other.m_data, nullptr)), m_handle(std::exchange(other.m_handle, nullptr)),
      m_owner(std::exchange(other.m_owner, false))
{
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
    if (this != &other)
    {
        release();
        m_name = std::move(other.m_name);
        m_size = std::exchange(other.m_size, 0);
        m_data = std::exchange(other.m_data, nullptr);
        m_handle = std::exchange(other.m_handle, nullptr);
        m_owner = std::exchange(other.m_owner, false);
    }
    return *this;
}

SharedMemory::~SharedMemory()
{
    release();
}

void SharedMemory::release() noexcept
{
    if (m_data == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    UnmapViewOfFile(m_data);
    CloseHandle(m_handle);
#else
    munmap(m_data, m_size);
    if (m_owner)
    {
        shm_unlink(m_name.c_str());
    }
#endif
    m_data = nullptr;
    m_handle = nullptr;
}

} // namespace sessionflow
