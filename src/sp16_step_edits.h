// Manual SP-16 edits belong to the seed that was on screen when they were made.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace tbmpc::sp16
{
enum class StepLane { number, pitch, gate, accent, slide };

struct StepEdits
{
    uint16_t seed = 0;
    std::array<uint16_t, 32> cells {};
    // Bits 0..7 hold a signed semitone offset. Each switch uses an override bit and a value bit.
    static constexpr unsigned flagShift(StepLane lane) { return 8 + 2*(int(lane)-int(StepLane::gate)); }
    static int pitch(uint16_t word) { return int(word & 127) - int(word & 128); }
    static bool flag(uint16_t word, StepLane lane, bool generated)
    {
        const unsigned pair = word >> flagShift(lane);
        return (pair & 2) ? (pair & 1) != 0 : generated;
    }
    void setPitch(int step, int semitones)
    {
        cells[step] = (cells[step] & 0xff00) | (std::clamp(semitones,-127,127) & 255);
    }
    void setFlag(int step, StepLane lane, bool on)
    {
        const int shift = flagShift(lane);
        cells[step] = (cells[step] & ~(3u << shift)) | ((2u + unsigned(on)) << shift);
    }
};

// The panel and project loader publish complete gestures. The audio callback never waits
// for a writer. If they overlap, it picks up the next complete snapshot one block later.
class StepEditExchange
{
public:
    void publish(const StepEdits& edits)
    {
        ++version;
        seed.store(edits.seed);
        for (std::size_t i=0; i<edits.cells.size(); ++i) cells[i].store(edits.cells[i]);
        ++version;
    }
    bool readIfChanged(uint32_t& last, StepEdits& result) const
    {
        const auto before = version.load();
        if (before == last || (before & 1u)) return false;
        result.seed = uint16_t(seed.load());
        for (std::size_t i=0; i<cells.size(); ++i) result.cells[i] = uint16_t(cells[i].load());
        if (before != version.load()) return false;
        last = before;
        return true;
    }
private:
    std::atomic<uint32_t> version {0}, seed {0};
    std::array<std::atomic<uint32_t>,32> cells {};
};
}
