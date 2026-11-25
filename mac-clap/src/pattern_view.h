#pragma once
#include <clap/clap.h>
#include <cstdint>
#include <cstring>

namespace tbmpc
{
struct PatternView
{
    int32_t steps, current, seed, running;
    uint32_t gates, slides, accents;  // a bit a step
    int16_t pitch[32];                // each step's pitch CV, 1/128 semitone
    bool operator== (const PatternView& o) const { return std::memcmp (this, &o, sizeof (*this)) == 0; }
};
static_assert (sizeof (PatternView) == 92, "no padding: compared and copied as bytes");

inline constexpr char patternViewId[] = "com.hdslave.slink.pattern-view/1";
struct PatternReader
{
    PatternView (CLAP_ABI *read) (const clap_plugin_t*);
};
}
