#pragma once

#include <cstddef>
#include <string>

namespace sessionflow
{

// A named memory region mapped into several processes. The creator owns the name and removes
// it on destruction; processes that opened it keep their mapping until they close it.
class SharedMemory
{
  public:
    // Throws std::system_error when the region can't be created or mapped.
    static SharedMemory create(const std::string& name, std::size_t size);
    static SharedMemory open(const std::string& name, std::size_t size);

    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;
    ~SharedMemory();

    void* data() const noexcept
    {
        return m_data;
    }

  private:
    SharedMemory() = default;
    void release() noexcept;

    std::string m_name;
    std::size_t m_size = 0;
    void* m_data = nullptr;
    void* m_handle = nullptr;
    bool m_owner = false;
};

} // namespace sessionflow
