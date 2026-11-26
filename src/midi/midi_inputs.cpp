#include "midi/midi_inputs.hpp"

#include <libremidi/libremidi.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <mutex>
#include <optional>

namespace sessionflow
{

namespace
{

std::string displayName(const libremidi::input_port& port)
{
    return port.display_name.empty() ? port.port_name : port.display_name;
}

// The notes and sustain pedals an input holds down, so unplugging it can release them.
class HeldNotes
{
  public:
    void track(const std::array<std::uint8_t, 3>& data) noexcept
    {
        const std::uint8_t type = data[0] & 0xF0;
        const std::size_t channel = data[0] & 0x0F;
        const std::size_t note = channel * s_keyCount + (data[1] & 0x7F);
        if (type == 0x90 && data[2] != 0)
        {
            m_notes.set(note);
        }
        else if (type == 0x80 || type == 0x90)
        {
            m_notes.reset(note);
        }
        else if (type == 0xB0 && data[1] == s_sustainPedal)
        {
            m_sustain.set(channel, data[2] >= 64);
        }
    }

    // Pushes a note-off for every held note and a release for every held pedal.
    void release(MidiInputCollector& collector, std::size_t source)
    {
        const std::int64_t now = midiInputClockNs();
        for (std::size_t note = 0; note < m_notes.size(); ++note)
        {
            if (m_notes.test(note))
            {
                const auto status = static_cast<std::uint8_t>(0x80 | (note / s_keyCount));
                collector.push(source,
                               {.timeNs = now, .data = {status, static_cast<std::uint8_t>(note % s_keyCount), 0}});
            }
        }
        for (std::size_t channel = 0; channel < m_sustain.size(); ++channel)
        {
            if (m_sustain.test(channel))
            {
                const auto status = static_cast<std::uint8_t>(0xB0 | channel);
                collector.push(source, {.timeNs = now, .data = {status, s_sustainPedal, 0}});
            }
        }
        m_notes.reset();
        m_sustain.reset();
    }

  private:
    static constexpr std::size_t s_keyCount = 128;
    static constexpr std::uint8_t s_sustainPedal = 64;

    std::bitset<16 * s_keyCount> m_notes;
    std::bitset<16> m_sustain;
};

} // namespace

std::vector<std::string> listMidiInputs()
{
    const libremidi::observer observer;

    std::vector<std::string> names;
    for (const libremidi::input_port& port : observer.get_input_ports())
    {
        names.push_back(displayName(port));
    }
    return names;
}

struct MidiInputs::Impl
{
    static constexpr std::size_t s_slotCount = MidiInputCollector::maxSources;

    // Destroyed in reverse order: the observer stops hot-plug callbacks first, then closing
    // the inputs joins their callback threads, and only then does the collector go away.
    MidiInputCollector collector;
    mutable std::mutex mutex;
    std::array<std::optional<libremidi::input_port>, s_slotCount> ports;
    // Written by each input's callback thread, read by close() once that thread has joined.
    std::array<HeldNotes, s_slotCount> heldNotes;
    std::array<std::unique_ptr<libremidi::midi_in>, s_slotCount> inputs;
    std::optional<libremidi::observer> observer;

    Impl()
    {
        libremidi::observer_configuration config;
        config.on_error = [](libremidi::midi_error, std::string_view message) { spdlog::warn("midi: {}", message); };
        config.input_added = [this](const libremidi::input_port& port) { open(port); };
        config.input_removed = [this](const libremidi::input_port& port) { close(port); };
        observer.emplace(config);
    }

    void open(const libremidi::input_port& port)
    {
        const std::scoped_lock lock(mutex);

        const auto freeSlot = std::find(ports.begin(), ports.end(), std::nullopt);
        if (freeSlot == ports.end())
        {
            spdlog::warn("midi: more than {} inputs, not opening {}", s_slotCount, displayName(port));
            return;
        }
        const auto slot = static_cast<std::size_t>(freeSlot - ports.begin());

        libremidi::input_configuration config;
        config.on_message = [this, slot](libremidi::message&& message)
        {
            // Sysex is ignored, so everything else fits a MidiEvent.
            if (message.bytes.empty() || message.bytes.size() > 3)
            {
                return;
            }
            TimedMidiMessage timed{.timeNs = message.timestamp};
            std::copy(message.bytes.begin(), message.bytes.end(), timed.data.begin());
            heldNotes[slot].track(timed.data);
            collector.push(slot, timed);
        };
        config.on_error = [](libremidi::midi_error, std::string_view message) { spdlog::warn("midi: {}", message); };
        config.timestamps = libremidi::timestamp_mode::SystemMonotonic;

        auto input = std::make_unique<libremidi::midi_in>(config);
        input->open_port(port);
        if (!input->is_port_open())
        {
            spdlog::warn("midi: could not open {}", displayName(port));
            return;
        }

        spdlog::info("midi: opened {}", displayName(port));
        ports[slot] = port;
        inputs[slot] = std::move(input);
    }

    void close(const libremidi::input_port& port)
    {
        const std::scoped_lock lock(mutex);

        const auto openSlot = std::find(ports.begin(), ports.end(), port);
        if (openSlot == ports.end())
        {
            return;
        }
        const auto slot = static_cast<std::size_t>(openSlot - ports.begin());

        // Joins the callback thread, so a later input can take over this slot as the single
        // producer of its queue.
        inputs[slot].reset();
        heldNotes[slot].release(collector, slot);
        ports[slot].reset();
        spdlog::info("midi: closed {}", displayName(port));
    }
};

MidiInputs::MidiInputs() : m_impl(std::make_unique<Impl>())
{
}

MidiInputs::~MidiInputs() = default;

MidiInputCollector& MidiInputs::collector() noexcept
{
    return m_impl->collector;
}

std::vector<std::string> MidiInputs::openInputNames() const
{
    const std::scoped_lock lock(m_impl->mutex);

    std::vector<std::string> names;
    for (const std::optional<libremidi::input_port>& port : m_impl->ports)
    {
        if (port)
        {
            names.push_back(displayName(*port));
        }
    }
    return names;
}

} // namespace sessionflow
