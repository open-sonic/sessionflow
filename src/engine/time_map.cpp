#include "engine/time_map.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>

namespace sessionflow
{

namespace
{

// Rounding slack for "first sample at or after". Double positions carry rounding error far
// below this, and a real position this close to a sample boundary is musically the same.
constexpr double s_sampleEpsilon = 1e-4;

TickCount floorDiv(TickCount value, TickCount divisor) noexcept
{
    TickCount quotient = value / divisor;
    if (value % divisor != 0 && (value < 0) != (divisor < 0))
    {
        --quotient;
    }
    return quotient;
}

TickCount ceilDiv(TickCount value, TickCount divisor) noexcept
{
    return -floorDiv(-value, divisor);
}

SampleCount firstSampleAtOrAfter(double samples) noexcept
{
    return static_cast<SampleCount>(std::ceil(samples - s_sampleEpsilon));
}

} // namespace

TimeMap::TimeMap(const TempoMap& tempoMap, double sampleRate) : m_sampleRate(sampleRate)
{
    assert(sampleRate > 0.0);
    assert(!tempoMap.tempos.empty() && tempoMap.tempos[0].position == 0);
    assert(!tempoMap.timeSignatures.empty() && tempoMap.timeSignatures[0].position == 0);

    m_tempos.reserve(tempoMap.tempos.size());
    for (const TempoEvent& event : tempoMap.tempos)
    {
        assert(event.bpm > 0.0);
        double startSample = 0.0;
        if (!m_tempos.empty())
        {
            const TempoSegment& previous = m_tempos.back();
            assert(event.position > previous.startTick);
            startSample = previous.startSample +
                          static_cast<double>(event.position - previous.startTick) * previous.samplesPerTick;
        }
        m_tempos.push_back({
            .startTick = event.position,
            .startSample = startSample,
            .samplesPerTick = sampleRate * 60.0 / (event.bpm * static_cast<double>(ticksPerBeat)),
            .bpm = event.bpm,
        });
    }

    m_meters.reserve(tempoMap.timeSignatures.size());
    for (const TimeSignatureEvent& event : tempoMap.timeSignatures)
    {
        assert(event.numerator > 0);
        assert(event.denominator > 0 && event.denominator <= 64 &&
               std::has_single_bit(static_cast<unsigned int>(event.denominator)));
        int startBar = 1;
        if (!m_meters.empty())
        {
            const MeterSegment& previous = m_meters.back();
            assert(event.position > previous.startTick);
            // A partial bar before the change still counts as a bar.
            startBar =
                previous.startBar + static_cast<int>(ceilDiv(event.position - previous.startTick, previous.barTicks));
        }
        const TickCount beatTicks = 4 * ticksPerBeat / event.denominator;
        m_meters.push_back({
            .startTick = event.position,
            .startBar = startBar,
            .numerator = event.numerator,
            .denominator = event.denominator,
            .beatTicks = beatTicks,
            .barTicks = event.numerator * beatTicks,
        });
    }
}

const TimeMap::TempoSegment& TimeMap::tempoSegmentAtTick(double ticks) const noexcept SESSIONFLOW_NONBLOCKING
{
    const auto next =
        std::upper_bound(m_tempos.begin(), m_tempos.end(), ticks, [](double value, const TempoSegment& segment)
                         { return value < static_cast<double>(segment.startTick); });
    return next == m_tempos.begin() ? *next : *(next - 1);
}

const TimeMap::MeterSegment& TimeMap::meterSegmentAtTick(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING
{
    const auto next =
        std::upper_bound(m_meters.begin(), m_meters.end(), tick,
                         [](TickCount value, const MeterSegment& segment) { return value < segment.startTick; });
    return next == m_meters.begin() ? *next : *(next - 1);
}

double TimeMap::samplesAt(double ticks) const noexcept SESSIONFLOW_NONBLOCKING
{
    const TempoSegment& segment = tempoSegmentAtTick(ticks);
    return segment.startSample + (ticks - static_cast<double>(segment.startTick)) * segment.samplesPerTick;
}

double TimeMap::ticksAt(double samples) const noexcept SESSIONFLOW_NONBLOCKING
{
    const auto next =
        std::upper_bound(m_tempos.begin(), m_tempos.end(), samples,
                         [](double value, const TempoSegment& segment) { return value < segment.startSample; });
    const TempoSegment& segment = next == m_tempos.begin() ? *next : *(next - 1);
    return static_cast<double>(segment.startTick) + (samples - segment.startSample) / segment.samplesPerTick;
}

SampleCount TimeMap::sampleAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING
{
    return firstSampleAtOrAfter(samplesAt(static_cast<double>(tick)));
}

double TimeMap::secondsAt(SampleCount sample) const noexcept
{
    return static_cast<double>(sample) / m_sampleRate;
}

SampleCount TimeMap::sampleAtSeconds(double seconds) const noexcept
{
    return firstSampleAtOrAfter(seconds * m_sampleRate);
}

double TimeMap::tempoAt(double ticks) const noexcept SESSIONFLOW_NONBLOCKING
{
    return tempoSegmentAtTick(ticks).bpm;
}

TimeSignatureEvent TimeMap::timeSignatureAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING
{
    const MeterSegment& segment = meterSegmentAtTick(tick);
    return {.position = segment.startTick, .numerator = segment.numerator, .denominator = segment.denominator};
}

MusicalPosition TimeMap::musicalPositionAt(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING
{
    const MeterSegment& segment = meterSegmentAtTick(tick);
    const TickCount sinceStart = tick - segment.startTick;
    const TickCount bars = floorDiv(sinceStart, segment.barTicks);
    const TickCount inBar = sinceStart - bars * segment.barTicks;
    const TickCount beats = inBar / segment.beatTicks;
    return {
        .bar = segment.startBar + static_cast<int>(bars),
        .beat = static_cast<int>(beats) + 1,
        .tick = inBar - beats * segment.beatTicks,
    };
}

TickCount TimeMap::tickAt(const MusicalPosition& position) const noexcept SESSIONFLOW_NONBLOCKING
{
    const auto next = std::upper_bound(m_meters.begin(), m_meters.end(), position.bar,
                                       [](int bar, const MeterSegment& segment) { return bar < segment.startBar; });
    const MeterSegment& segment = next == m_meters.begin() ? *next : *(next - 1);
    return segment.startTick + (position.bar - segment.startBar) * segment.barTicks +
           (position.beat - 1) * segment.beatTicks + position.tick;
}

Beat TimeMap::beatAtOrAfter(TickCount tick) const noexcept SESSIONFLOW_NONBLOCKING
{
    const auto next =
        std::upper_bound(m_meters.begin(), m_meters.end(), tick,
                         [](TickCount value, const MeterSegment& segment) { return value < segment.startTick; });
    const auto current = next == m_meters.begin() ? next : next - 1;
    const MeterSegment& segment = *current;
    const auto following = current + 1;

    const TickCount beatIndex = ceilDiv(tick - segment.startTick, segment.beatTicks);
    const TickCount beatTick = segment.startTick + beatIndex * segment.beatTicks;
    if (following != m_meters.end() && following->startTick <= beatTick)
    {
        return {.tick = following->startTick, .isDownbeat = true};
    }
    const TickCount beatsPerBar = segment.numerator;
    return {.tick = beatTick, .isDownbeat = beatIndex - floorDiv(beatIndex, beatsPerBar) * beatsPerBar == 0};
}

} // namespace sessionflow
