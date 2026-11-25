#include "plugins/hosted_processor.hpp"
#include "support/signals.hpp"
#include "support/test_plugin_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace sessionflow;

namespace
{

constexpr ProcessSetup s_setup{.sampleRate = 48000.0, .maxBlockSize = 512};
constexpr std::chrono::seconds s_crashDetectionLimit{5};

// Runs one block of constant 1.0 through processor and returns the output peak.
float processOnes(HostedProcessor& processor, AudioBuffer& buffer)
{
    test::fillConstant(buffer.view(), 1.0f);
    processor.process({.audio = buffer.view()});
    return test::peak(buffer.view());
}

} // namespace

TEST_CASE("audio runs through the host process")
{
    HostedProcessor processor(SESSIONFLOW_TEST_PLUGIN_HOST);
    processor.prepare(s_setup);
    REQUIRE(processor.state() == HostState::Running);

    AudioBuffer buffer(2, s_setup.maxBlockSize);
    REQUIRE(processor.setParameter(test::testHostGain, 0.5f));

    CHECK(processOnes(processor, buffer) == 0.5f);
    CHECK(processor.state() == HostState::Running);
}

TEST_CASE("a crashing host silences its track and the others keep playing")
{
    HostedProcessor crashing(SESSIONFLOW_TEST_PLUGIN_HOST);
    HostedProcessor healthy(SESSIONFLOW_TEST_PLUGIN_HOST);
    crashing.prepare(s_setup);
    healthy.prepare(s_setup);

    AudioBuffer buffer(2, s_setup.maxBlockSize);
    REQUIRE(processOnes(crashing, buffer) == 1.0f);

    crashing.setParameter(test::testHostCrash, 1.0f);

    const auto deadline = std::chrono::steady_clock::now() + s_crashDetectionLimit;
    bool crashingSilent = true;
    bool healthyPlaying = true;
    while (crashing.state() != HostState::Crashed && std::chrono::steady_clock::now() < deadline)
    {
        crashingSilent = crashingSilent && processOnes(crashing, buffer) == 0.0f;
        healthyPlaying = healthyPlaying && processOnes(healthy, buffer) == 1.0f;
    }

    CHECK(crashing.state() == HostState::Crashed);
    CHECK(crashingSilent);
    CHECK(healthyPlaying);
    CHECK(processOnes(crashing, buffer) == 0.0f);
    CHECK(processOnes(healthy, buffer) == 1.0f);
}

TEST_CASE("a hanging host misses its deadline without stalling later blocks")
{
    HostedProcessor processor(SESSIONFLOW_TEST_PLUGIN_HOST);
    processor.prepare(s_setup);
    AudioBuffer buffer(2, s_setup.maxBlockSize);

    processor.setParameter(test::testHostHang, 1.0f);
    CHECK(processOnes(processor, buffer) == 0.0f);
    CHECK(processor.state() == HostState::Unresponsive);

    const auto start = std::chrono::steady_clock::now();
    CHECK(processOnes(processor, buffer) == 0.0f);
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(1));
}
