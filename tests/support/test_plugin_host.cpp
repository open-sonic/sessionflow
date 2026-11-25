#include "support/test_plugin_host.hpp"
#include "plugins/plugin_host.hpp"

#include <chrono>
#include <csignal>
#include <thread>

using namespace sessionflow;

namespace
{

class TestProcessor final : public Processor
{
  public:
    void setGain(float gain) noexcept
    {
        m_gain = gain;
    }

    void prepare(const ProcessSetup&) override
    {
    }

    void process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING override
    {
        for (std::size_t channel = 0; channel < block.audio.channelCount(); ++channel)
        {
            for (float& sample : block.audio.channel(channel))
            {
                sample *= m_gain;
            }
        }
    }

  private:
    float m_gain = 1.0f;
};

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return 64;
    }

    TestProcessor processor;
    return runPluginHost(argv[1], processor,
                         [&](std::uint32_t id, float value)
                         {
                             switch (id)
                             {
                             case test::testHostGain:
                                 processor.setGain(value);
                                 break;
                             case test::testHostCrash:
                                 std::raise(SIGSEGV);
                                 break;
                             case test::testHostHang:
                                 while (true)
                                 {
                                     std::this_thread::sleep_for(std::chrono::hours(1));
                                 }
                             default:
                                 break;
                             }
                         });
}
