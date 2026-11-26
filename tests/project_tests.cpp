#include "model/project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <variant>

using namespace sessionflow;

namespace
{

// Drums with an audio clip, keys with a MIDI clip and a volume automation lane.
Project makeSong()
{
    const AudioClip loop{.position = 0, .length = 4 * ticksPerBeat, .filePath = "loops/drums.wav"};
    const MidiClip chords{.position = 0, .length = 4 * ticksPerBeat, .notes = {Note{.pitch = 60}, Note{.pitch = 64}}};
    const AutomationLane volume{.parameterId = "volume",
                                .points = {AutomationPoint{.position = 0, .value = 0.5},
                                           AutomationPoint{.position = 4 * ticksPerBeat, .value = 1.0}}};

    Project project = addTrack({}, {.name = "Drums", .clips = {loop}});
    return addTrack(project, {.name = "Keys", .clips = {chords}, .automation = {volume}});
}

Project withFirstKeysNote(const Project& project, Note note)
{
    Track keys = project.tracks[1];
    MidiClip clip = std::get<MidiClip>(keys.clips[0]);
    clip.notes = clip.notes.set(0, note);
    keys.clips = keys.clips.set(0, clip);

    Project result = project;
    result.tracks = project.tracks.set(1, keys);
    return result;
}

} // namespace

TEST_CASE("adding a track returns a new project and keeps the old one")
{
    const Project empty;

    const Project withDrums = addTrack(empty, {.name = "Drums"});

    CHECK(empty.tracks.empty());
    REQUIRE(withDrums.tracks.size() == 1);
    CHECK(withDrums.tracks[0].name == "Drums");
}

TEST_CASE("projects compare by value")
{
    const Project first = addTrack({}, {.name = "Bass"});
    const Project second = addTrack({}, {.name = "Bass"});

    CHECK(first == second);
    CHECK(first != addTrack(first, {.name = "Keys"}));
}

TEST_CASE("a new project starts at 120 bpm in 4/4")
{
    const TempoMap tempoMap = Project{}.tempoMap;

    REQUIRE(tempoMap.tempos.size() == 1);
    CHECK(tempoMap.tempos[0] == TempoEvent{.position = 0, .bpm = 120.0});
    REQUIRE(tempoMap.timeSignatures.size() == 1);
    CHECK(tempoMap.timeSignatures[0] == TimeSignatureEvent{.position = 0, .numerator = 4, .denominator = 4});
}

TEST_CASE("projects with the same clips, notes and automation compare equal")
{
    CHECK(makeSong() == makeSong());
}

TEST_CASE("changing a note leaves the previous project unchanged")
{
    const Project before = makeSong();

    const Project after = withFirstKeysNote(before, Note{.pitch = 62});

    CHECK(std::get<MidiClip>(before.tracks[1].clips[0]).notes[0].pitch == 60);
    CHECK(std::get<MidiClip>(after.tracks[1].clips[0]).notes[0].pitch == 62);
    CHECK(before != after);
}

TEST_CASE("a change to one track shares the other tracks' clips with the previous version")
{
    const Project before = makeSong();

    const Project after = withFirstKeysNote(before, Note{.pitch = 62});

    CHECK(after.tracks[0].clips.identity() == before.tracks[0].clips.identity());
    CHECK(after.tracks[1].clips.identity() != before.tracks[1].clips.identity());
}

TEST_CASE("an audio clip and a MIDI clip at the same position are different clips")
{
    const Clip audio = AudioClip{.position = ticksPerBeat, .length = ticksPerBeat};
    const Clip midi = MidiClip{.position = ticksPerBeat, .length = ticksPerBeat};

    CHECK(audio != midi);
}

TEST_CASE("controller changes are part of a MIDI clip's value")
{
    const MidiClip plain{.length = ticksPerBeat};
    const MidiClip modulated{.length = ticksPerBeat,
                             .controlChanges = {ControlChange{.position = 0, .controller = 1, .value = 64}}};

    CHECK(plain != modulated);
    CHECK(modulated.controlChanges[0].value == 64);
}
