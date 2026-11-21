// Included after PatternView in screen.h. Plot coordinates are native pixels.
#pragma once

namespace tbmpc::plots
{
using hdui::Raster;
namespace ink = hdui::ink;
constexpr int pitchGutter = 30;

inline int stepCount (const PatternView& view)
{
    return std::clamp (int (view.steps), 0, 32);
}

inline void noteName (int semitone, char (&s)[12])
{
    const int midi = semitone + noteAtZeroVolts;
    const int pc = (midi % 12 + 12) % 12;
    std::snprintf (s, sizeof (s), "%s%d", rootChoices[pc], (midi - pc) / 12 - 1);
}

inline void pitchLabel (const Raster& r, int mid, int y, int pitch, hdui::Ink c)
{
    char s[12];
    noteName (int (std::lround (pitch / 128.0)), s);
    const int length = int (std::strlen (s));
    const int advance = length > 3 ? 4 : 5;
    int x = mid - (length * advance - 1) / 2;
    for (const char* ch = s; *ch; ++ch, x += advance)
    {
        const char* at = std::strchr (hdui::font::noteChars, *ch);
        if (!at) continue;
        const auto& glyph = hdui::font::notes[at - hdui::font::noteChars];
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 4; ++k)
                if ((glyph[j] >> k) & 1) r.dot (x + k, y + j, c);
    }
}

// Reserve a row for each used note when the panel has room. Dense phrases use
// an octave ruler and exact per-step names. The pitch scale spans the whole pattern.
struct PitchAxis
{
    int notes[32] {}, count = 0, low, high;
    bool semitones = true;

    explicit PitchAxis (const PatternView& v)
    {
        const int n = stepCount (v);
        const auto limits = std::minmax_element (v.pitch, v.pitch + n);
        low = int (std::floor (*limits.first / 1536.0)) * 12;
        high = std::max (low + 12, int (std::ceil (*limits.second / 1536.0)) * 12);
        for (int i = 0; i < n; ++i)
        {
            notes[i] = int (std::lround (v.pitch[i] / 128.0));
            semitones &= v.pitch[i] % 128 == 0;
        }
        std::sort (notes, notes + n);
        count = int (std::unique (notes, notes + n) - notes);
    }

    int linear (int cv, int top, int bottom) const
    {
        return bottom - (cv - low * 128) * (bottom - top) / ((high - low) * 128);
    }

    // Keep neighbouring labels nine pixels apart; give the spare space to wider intervals.
    int row (int i, int top, int bottom) const
    {
        if (count == 1) return (top + bottom) / 2;
        const int freeSpace = bottom - top - 9 * (count - 1);
        return bottom - 9 * i - freeSpace * (notes[i] - notes[0]) / (notes[count - 1] - notes[0]);
    }
};

inline void notehead (const Raster& r, int mid, int y, bool gate, bool live, bool narrow)
{
    const auto c = live ? ink::live : (gate ? ink::mint : ink::soft);
    if (narrow)
    {
        if (gate) r.box (mid - 1, y - 1, 4, 3, c);
        else
        {
            r.dot (mid, y - 2, c);
            r.dot (mid, y + 2, c);
            r.frame (mid - 1, y - 1, 3, 3, c);
        }
        return;
    }
    if (gate)
    {
        r.box (mid - 3, y - 1, 8, 1, c);
        r.box (mid - 5, y, 11, 2, c);
        r.box (mid - 4, y + 2, 8, 1, live ? c : ink::pink);
    }
    else
    {
        r.box (mid - 1, y - 2, 3, 1, c);
        r.box (mid - 1, y + 2, 3, 1, c);
        for (int side : { -1, 1 })
        {
            r.dot (mid + 2 * side, y - 1, c);
            r.dot (mid + 3 * side, y, c);
            r.dot (mid + 2 * side, y + 1, c);
        }
    }
}

inline void score (const Raster& r, const PatternView& v, const PitchAxis& axis, int x, int y, int w, int h,
                   bool playing)
{
    const int count = stepCount (v), columns = count <= 16 ? 16 : 32;
    // Each named pitch needs its own eight-pixel cap and one row of leading.
    const bool namedRows = axis.semitones && (axis.count - 1) * 9 <= h - 35;
    const int left = x + pitchGutter, width = w - pitchGutter, top = y + 12;
    const int labelRows = columns / 16;
    const int labelSpace = namedRows ? 0 : 10 * labelRows + 2;
    const int bottom = y + h - 23 - labelSpace;
    const int acc = y + h - 18, slide = y + h - 8;
    r.box (x, y, w, h, ink::field);
    auto column = [&](int i) { return left + i * width / columns; };
    for (int i = 4; i < count; i += 8)
        r.box (column (i), y, column (std::min (i + 4, count)) - column (i), h, ink::alternate);
    r.box (x, y, left - x - 1, h, ink::card);
    r.box (left - 1, y, 1, h, ink::rule);
    r.text (x + 3, y + 1, "CV", ink::orange);
    const int active = playing && v.running ? v.current : -1;
    if (active >= 0 && active < count)
    {
        const int mid = (column (active) + column (active + 1)) / 2;
        const int half = columns == 16 ? 3 : 2;
        r.box (mid - half, y + 10, 2 * half + 1, h - 10, ink::cursor);
        r.box (mid - 2, y + 9, 5, 2, ink::live);
    }

    auto ruledNote = [&](int semitone, int line) {
        char name[12];
        noteName (semitone, name);
        r.right (left - 3, line - 3, name, ink::soft);
        for (int i = left; i < column (count); i += 3) r.dot (i, line, ink::grid);
    };
    if (namedRows)
        for (int i = 0; i < axis.count; ++i) ruledNote (axis.notes[i], axis.row (i, top, bottom));
    else
    {
        ruledNote (axis.high, top);
        int lastLine = top;
        for (int n = axis.high - 12; n > axis.low; n -= 12)
        {
            const int line = axis.linear (n * 128, top, bottom);
            if (line - lastLine >= 10 && bottom - line >= 10)
            {
                ruledNote (n, line);
                lastLine = line;
            }
        }
        ruledNote (axis.low, bottom);
    }

    int lines[32];
    for (int i = 0; i < count; ++i)
    {
        if (namedRows)
        {
            const int note = v.pitch[i] / 128;
            const auto* at = std::lower_bound (axis.notes, axis.notes + axis.count, note);
            lines[i] = axis.row (int (at - axis.notes), top, bottom);
        }
        else lines[i] = axis.linear (v.pitch[i], top, bottom);
        if (i == 0 || !((v.slides >> (i - 1)) & 1)) continue;
        // Connect the two stored pitches. Leave a gap at either end for each notehead.
        const int from = (column (i - 1) + column (i)) / 2, to = (column (i) + column (i + 1)) / 2;
        const int dy = lines[i] - lines[i - 1], dx = to - from;
        const int span = std::max (dx, std::abs (dy));
        for (int j = 3; j <= span - 3; ++j)
            r.dot (from + (j * dx + span / 2) / span, lines[i - 1] + j * dy / span, ink::pink);
    }
    r.box (left, acc - 3, column (count) - left, 1, ink::grid);
    r.text (x + 2, acc, "ACC", ink::pink);
    r.text (x + 2, slide, "SLD", ink::orange);
    for (int i = 0; i < count; ++i)
    {
        const int a = column (i), b = column (i + 1), mid = (a + b) / 2;
        const bool current = i == active;
        if (i % 4 != 0) r.dot (mid, y + 6, ink::rule);
        if (i % 4 == 0)
        {
            char index[] = { char ('0' + (i + 1) / 10), char ('0' + (i + 1) % 10), 0 };
            r.text (a + 2, y + 1, index, ink::orange);
            r.box (a + 1, y + 10, 12, 1, ink::grid);
            for (int j = top; j <= bottom; j += 5) r.dot (a, j, ink::rule);
        }
        if (!namedRows)
        {
            const int stagger = 10 * (i % labelRows);
            pitchLabel (r, std::clamp (mid, left + 8, x + w - 8), acc - 10 - stagger, v.pitch[i], current ? ink::live : ink::muted);
        }
        notehead (r, mid, lines[i], (v.gates >> i) & 1, current, columns == 32);
        const auto c = current ? ink::live : ink::orange;
        if ((v.accents >> i) & 1)
        {
            const auto accent = current ? ink::live : ink::pink;
            r.box (mid - 2, acc + 3, 5, 2, accent);
            r.box (mid - 1, acc + 2, 3, 4, accent);
        }
        else r.dot (mid, acc + 4, ink::rule);
        if ((v.slides >> i) & 1)
        {
            const int shaft = columns == 32 ? 2 : 3;
            r.box (mid - shaft, slide + 4, 2 * shaft + 1, 1, c);
            r.dot (mid + shaft - 2, slide + 2, c);
            r.dot (mid + shaft - 1, slide + 3, c);
            r.dot (mid + shaft - 1, slide + 5, c);
            r.dot (mid + shaft - 2, slide + 6, c);
        }
        else r.dot (mid, slide + 4, ink::rule);
    }
}

inline void sequence (const Raster& r, const PatternView& v, int x, int y, int w, int h, bool large, bool playing)
{
    const int n = stepCount (v);
    if (!n)
    {
        r.box (x, y, w, h, ink::field);
        r.box (x + 4, y + h / 2 - 13, w - 8, 1, ink::grid);
        r.box (x + 4, y + h / 2 + 13, w - 8, 1, ink::grid);
        r.caution (x + 6, y + h / 2 - 4, 11, 8, ink::orange);
        r.text (x + 21, y + h / 2 - 4, "Awaiting clock", ink::paper);
        return;
    }
    const PitchAxis axis (v);
    if (large)
    {
        score (r, v, axis, x, y, w, h, playing);
        return;
    }
    const int columns = n <= 16 ? 16 : 32;
    for (int i = 0; i < n; ++i)
    {
        const int a = x + i * w / columns, b = x + (i + 1) * w / columns;
        const bool current = playing && v.running && v.current == i;
        const auto c = current ? ink::live : ink::mint;
        if (current) r.box (a, y, b - a, h, ink::cursor);
        if (i % 4 == 0) r.box (a, y, 1, h - 5, ink::grid);
        const int line = axis.linear (v.pitch[i], y + 3, y + h - 8);
        if ((v.gates >> i) & 1) r.box (a + 1, line - 1, std::max (1,b - a - 2), 3, c);
        else r.dot ((a + b) / 2, line, c);
        if ((v.accents >> i) & 1) r.dot ((a + b) / 2, y + h - 4, current ? c : ink::pink);
        if ((v.slides >> i) & 1) r.box ((a + b) / 2, y + h - 2, std::max (1,(b - a) / 2), 1, current ? c : ink::orange);
        if (current) r.box (a, y, b - a, 2, ink::live);
    }
}

inline void scopePanel (const Raster& r, const char* name)
{
    r.box (0, 4, r.w - 4, r.h - 9, ink::field);
    r.box (0, 4, r.w - 4, 2, ink::orange);
    r.caution (r.w - 30, 4, 26, 2, ink::bg);
    r.box (5, 10, 63, 12, ink::grid);
    r.text (8, 12, name, ink::paper);
    r.box (0, r.h - 6, r.w - 4, 1, ink::rule);
}

inline void response (const Raster& r, const TextContext& ctx, float cutoff, const char* reading)
{
    const int kind = int (ctx.filter);
    const int left = 8, right = r.w - 8, top = 44, bottom = r.h - 24;
    const int width = right - left;
    const float k = feedbackOf (kind, ctx.resonance), nominal = nominalHz (kind, cutoff);
    const double ref = filterGain2 (kind, k, ctx.spread,
                                     kind == filterTwinPeak ? std::exp2 (ctx.spread * 0.5) : 1e-3);
    auto py = [&] (double db) {
        return top + int (std::lround ((24.0 - std::clamp (db, -36.0, 24.0)) / 60.0 * (bottom - top)));
    };
    auto px = [&] (double hz) {
        return left + int (std::lround (std::clamp (std::log10 (hz / 20.0) / 3.0, 0.0, 1.0) * (width - 1)));
    };
    scopePanel (r, "RESPONSE");
    r.text (left, 28, filterChoices[kind], ink::paper);
    r.right (right, 28, reading, ink::paper);
    for (double db : { 0.0, -24.0 })
        r.box (left, py (db), width, 1, ink::grid);
    for (int hz : { 100, 1000, 10000 })
        for (int j = top; j <= bottom; j += 4) r.dot (px (hz), j, ink::rule);

    const int edge = px (cutoffHz (cutoff, ctx));
    for (int j = top; j <= bottom; j += 3) r.dot (edge, j, ink::muted);
    r.box (edge - 2, bottom + 1, 5, 2, ink::orange);
    // Two samples per pixel retain the narrow resonant peaks at high feedback.
    int previous = 0;
    for (int i = 0; i <= 2 * (width - 1); ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, double (i) / (2 * (width - 1)));
        const double gain = filterGain2 (kind, k, ctx.spread, hz / nominal);
        const int y = py (10.0 * std::log10 (gain / ref + 1e-12));
        if (i == 0) previous = y;
        const int x = left + i / 2, lo = std::min (previous, y), hi = std::max (previous, y);
        // A displaced pink edge under the mint line, clipped to the scope.
        if (x < right - 1 && hi < bottom - 1) r.dot (x + 1, hi + 2, ink::pink);
        r.box (x, lo, 1, hi - lo + 1, ink::mint);
        previous = y;
    }
    r.text (left, bottom + 8, "20 Hz", ink::muted);
    r.right (right, bottom + 8, "20 kHz", ink::muted);
}
} // namespace tbmpc::plots
