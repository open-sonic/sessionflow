#pragma once

#include "core/smoother.hpp"
#include "engine/processor.hpp"

#include <atomic>

namespace sessionflow
{

// Smallest complete built-in device: shows the Processor contract, a parameter set from
// another thread and smoothing on the audio thread.
class GainDevice final : public Processor
{
  public:
    // Safe to call from any thread.
    void setGainDecibels(float decibels) noexcept;

    void prepare(const ProcessSetup& setup) override;
    void process(const ProcessBlock& block) noexcept SESSIONFLOW_NONBLOCKING override;

  private:
    std::atomic<float> m_targetGain{1.0f};
    LinearSmoother m_gain;
};

} // namespace sessionflow
