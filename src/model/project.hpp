#pragma once

#include "core/time.hpp"

#include <immer/flex_vector.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace sessionflow
{

// Immutable project state. Every edit returns a new Project that shares unchanged parts
// with the old one, so undo keeps old versions and the audio thread reads them lock-free.

struct Note
{
    std::uint8_t pitch = 60;
    std::uint8_t velocity = 100;
    TickCount start = 0; // relative to the clip start
    TickCount length = ticksPerBeat;

    bool operator==(const Note&) const = default;
};

struct MidiClip
{
    TickCount position = 0;
    TickCount length = 0;
    immer::flex_vector<Note> notes;

    bool operator==(const MidiClip&) const = default;
};

struct AudioClip
{
    TickCount position = 0;
    TickCount length = 0;
    std::string filePath;
    SampleCount sourceOffset = 0; // first sample of the file that plays at position
    float gain = 1.0f;            // linear

    bool operator==(const AudioClip&) const = default;
};

using Clip = std::variant<AudioClip, MidiClip>;

struct AutomationPoint
{
    TickCount position = 0;
    double value = 0.0; // normalized 0 to 1, the parameter maps it to its own range

    bool operator==(const AutomationPoint&) const = default;
};

struct AutomationLane
{
    std::string parameterId;
    immer::flex_vector<AutomationPoint> points;

    bool operator==(const AutomationLane&) const = default;
};

struct TempoEvent
{
    TickCount position = 0;
    double bpm = 120.0;

    bool operator==(const TempoEvent&) const = default;
};

struct TimeSignatureEvent
{
    TickCount position = 0;
    int numerator = 4;
    int denominator = 4;

    bool operator==(const TimeSignatureEvent&) const = default;
};

// Events sorted by position. The first event of each list sits at tick 0.
struct TempoMap
{
    immer::flex_vector<TempoEvent> tempos{TempoEvent{}};
    immer::flex_vector<TimeSignatureEvent> timeSignatures{TimeSignatureEvent{}};

    bool operator==(const TempoMap&) const = default;
};

struct Track
{
    std::string name;
    immer::flex_vector<Clip> clips;
    immer::flex_vector<AutomationLane> automation;

    bool operator==(const Track&) const = default;
};

struct Project
{
    immer::flex_vector<Track> tracks;
    TempoMap tempoMap;

    bool operator==(const Project&) const = default;
};

Project addTrack(const Project& project, Track track);

} // namespace sessionflow
