#pragma once

#include "core/realtime.hpp"
#include "core/time.hpp"
#include "model/project.hpp"

#include <vector>

namespace sessionflow
{

// Bars and beats count from 1, like a DAW shows them. tick is the offset inside the beat.
struct MusicalPosition
{
    int bar = 1;
    int beat = 1;
    TickCount tick = 0;

    bool operator==(const MusicalPosition&) const = default;
};

struct Beat
{
    TickCount tick = 0;
    bool isDownbeat = false;

    bool operator==(const Beat&) const = default;
};

// Converts between ticks, samples, seconds and bars and beats for one tempo map at one
// sample rate. Every position is computed from the start of its tempo segment instead of
// being accumulated block by block, so hours of playback don't drift. Tempo is in quarter
// notes per minute, and a quarter note is ticksPerBeat ticks.
//
// Construct it off the audio thread. All queries are real-time safe.
class TimeMap
{
  public:
    // tempoMap must hold at least one tempo and one time signature at tick 0, positions
    // strictly ascending, positive tempos, and denominators that are powers of two up to 64.
    TimeMap(const TempoMap& tempoMap, double sampleRate);

    double sampleRate() const noexcept
    {
        return m_sampleRate;
    }

    // Exact, fractional conversions.
    double samplesAt(double ticks) const noexcept SESSIONFLOW_NONBLOCKING;
    double ticksAt(double samples) const noexcept SESSIONFLOW_NONBLOCKING;

    // The first sample at or after tick. Everything scheduled at a tick, such as a loop end,
    // a click or a clip start, plays on this sample, so they all agree.
    SampleCount sampleAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING;

    double secondsAt(SampleCount sample) const noexcept;
    SampleCount sampleAtSeconds(double seconds) const noexcept;

    double tempoAt(double ticks) const noexcept SESSIONFLOW_NONBLOCKING;
    TimeSignatureEvent timeSignatureAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING;

    MusicalPosition musicalPositionAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING;
    TickCount tickAt(const MusicalPosition& position) const noexcept SESSIONFLOW_NONBLOCKING;

    // The first beat at or after tick. A beat is one denominator note: a quarter in 4/4, an
    // eighth in 6/8. A time signature change always starts a new bar.
    Beat beatAtOrAfter(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING;

  private:
    struct TempoSegment
    {
        TickCount startTick = 0;
        double startSample = 0.0;
        double samplesPerTick = 0.0;
        double bpm = 0.0;
    };

    struct MeterSegment
    {
        TickCount startTick = 0;
        int startBar = 1;
        int numerator = 4;
        int denominator = 4;
        TickCount beatTicks = ticksPerBeat;
        TickCount barTicks = 4 * ticksPerBeat;
    };

    const TempoSegment& tempoSegmentAtTick(double ticks) const noexcept SESSIONFLOW_NONBLOCKING;
    const MeterSegment& meterSegmentAtTick(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING;

    double m_sampleRate;
    std::vector<TempoSegment> m_tempos;
    std::vector<MeterSegment> m_meters;
};

} // namespace sessionflow
