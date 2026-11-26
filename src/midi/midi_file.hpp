#pragma once

#include "model/project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sessionflow
{

struct MidiFileTrack
{
    std::string name{};
    MidiClip clip{};

    bool operator==(const MidiFileTrack&) const = default;
};

struct MidiFile
{
    std::vector<MidiFileTrack> tracks{};
    TempoMap tempoMap{};

    bool operator==(const MidiFile&) const = default;
};

// Parses a Standard MIDI File of format 0 or 1 with a ticks-per-quarter division. Every track
// with notes or controllers becomes one clip at position 0 that lasts until the track's end,
// with notes sorted by start and pitch. Positions are rescaled to ticksPerBeat. All channels
// of a track go into its clip, and messages the model has no place for, such as pitch bend
// and sysex, are dropped. Returns nothing for malformed data, format 2 and SMPTE timing.
std::optional<MidiFile> parseMidiFile(std::span<const std::uint8_t> bytes);

// Writes format 1 at ticksPerBeat: a tempo track, then one track per entry on channel 1.
// Events go at the clip's position plus their offset. Velocities of 0 are written as 1, since
// a note-on with velocity 0 means note-off.
std::vector<std::uint8_t> serializeMidiFile(const MidiFile& file);

// Blocking file I/O, never on the audio thread.
std::optional<MidiFile> readMidiFile(const std::filesystem::path& path);
bool writeMidiFile(const std::filesystem::path& path, const MidiFile& file);

} // namespace sessionflow
