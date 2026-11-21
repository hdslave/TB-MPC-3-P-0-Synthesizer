// Included after PatternView in screen.h. Plot coordinates are native pixels.
#pragma once

namespace tbmpc::plots
{
using hdui::Raster;
namespace ink = hdui::ink;

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

inline void bank (const Raster& r, const PatternView& v, int x, int y, int w, int h,
                  int first, int count, bool large, bool playing)
{
    const int n = stepCount (v);
    r.box (x, y, w, h, ink::field);
    if (n == 0)
    {
        r.text (x + 6, y + h / 2 - 4, "Awaiting clock", ink::muted);
        return;
    }
    // Keep four stable beat divisions on short patterns. Variable column boundaries
    // use the entire chart, including step 32; a one-step pattern gets a single cell.
    const int left = x + (large ? 36 : 3), right = x + w - 3;
    const bool wrapped = large && n > 16;
    const int columns = large || n <= 16 ? 16 : 32;
    const int top = y + (wrapped ? 12 : 5);
    const int bottom = y + h - (large ? (wrapped ? 19 : 38) : 7);
    const auto limits = std::minmax_element (v.pitch, v.pitch + n);
    const int low = int (std::floor (*limits.first / 1536.0)) * 12;
    const int high = std::max (low + 12, int (std::ceil (*limits.second / 1536.0)) * 12);
    auto py = [&] (int cv) { return bottom - (cv - low * 128) * (bottom - top) / ((high - low) * 128); };

    for (int slot = 0; slot < count; ++slot)
    {
        const int i = first + slot;
        const int a = left + (right - left) * slot / columns;
        const int b = left + (right - left) * (slot + 1) / columns;
        const bool current = playing && v.running && v.current == i;
        const bool gate = (v.gates >> i) & 1, accent = (v.accents >> i) & 1, slide = (v.slides >> i) & 1;
        if (current)
            r.box (a, y + 1, b - a, h - 2, ink::cursor);
        if (i % 4 == 0)
            for (int j = top; j <= bottom; j += 4) r.dot (a, j, ink::rule);
        for (int semitone = low; semitone <= high; semitone += 12)
            r.box (a, py (semitone * 128), b - a, 1, ink::grid);

        const int pitchY = py (v.pitch[i]), inset = large ? 2 : 1;
        const auto colour = current ? ink::live : (gate ? ink::paper : ink::muted);
        const int length = std::max (2, b - a - inset - 1);
        if (gate)
            r.box (a + inset, pitchY - 1, length, 3, colour);
        else if (large)
            r.frame (a + inset, pitchY - 1, length, 3, colour);
        else
            r.dot ((a + b) / 2, pitchY, colour);

        const int mid = (a + b) / 2;
        if (large)
        {
            pitchLabel (r, mid, wrapped ? y + 2 : bottom + 4, v.pitch[i], current ? ink::live : ink::muted);
            const int ay = y + h - (wrapped ? 13 : 17), sy = y + h - (wrapped ? 5 : 7);
            if (accent) // square, independent of gate
                r.box (mid - 2, ay - 2, 4, 4, current ? ink::live : ink::soft);
            else
                r.dot (mid, ay, ink::rule);
            if (slide) // outgoing arrow, including the wrap from the last step
            {
                r.box (mid - 3, sy, 6, 1, colour);
                r.dot (mid, sy - 2, colour);
                r.dot (mid, sy + 2, colour);
                r.dot (mid + 1, sy - 1, colour);
                r.dot (mid + 1, sy + 1, colour);
            }
            else
                r.dot (mid, sy, ink::rule);
        }
        else
        {
            if (accent) r.dot (mid, y + h - 3, ink::soft);
            if (slide) r.box (mid, y + h - 2, std::max (1, b - mid), 1, ink::soft);
        }
        if (current) r.box (a + 1, y, std::max (1,b - a - 1), 2, ink::live);
    }
    if (large)
    {
        if (wrapped)
        {
            char name[32];
            std::snprintf (name, sizeof (name), "%02d-%02d", first + 1, first + count);
            r.text (x + 1, y + 2, name, ink::soft);
        }
        else
        {
            char name[12];
            noteName (high, name);
            r.text (x + 3, top - 2, name, ink::muted);
            noteName (low, name);
            r.text (x + 3, bottom - 4, name, ink::muted);
        }
        r.text (x + 3, y + h - (wrapped ? 16 : 20), "ACC", ink::muted);
        r.text (x + 3, y + h - (wrapped ? 8 : 10), "SLD", ink::muted);
    }
}

inline void sequence (const Raster& r, const PatternView& v, int x, int y, int w, int h, bool large, bool playing)
{
    const int n = stepCount (v);
    if (large && n > 16)
    {
        bank (r, v, x, y, w, h / 2 - 1, 0, 16, true, playing);
        bank (r, v, x, y + h / 2 + 1, w, h / 2 - 1, 16, n - 16, true, playing);
    }
    else bank (r, v, x, y, w, h, 0, n, large, playing);
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
    r.box (0, 4, r.w - 4, r.h - 9, ink::field);
    r.text (left, 12, "RESPONSE", ink::muted);
    r.text (left, 28, filterChoices[kind], ink::paper);
    r.right (right, 28, reading, ink::paper);
    for (double db : { 0.0, -24.0 })
        r.box (left, py (db), width, 1, ink::grid);
    for (int hz : { 100, 1000, 10000 })
        for (int j = top; j <= bottom; j += 4) r.dot (px (hz), j, ink::rule);

    const int edge = px (cutoffHz (cutoff, ctx));
    for (int j = top; j <= bottom; j += 3) r.dot (edge, j, ink::muted);
    r.box (edge - 2, bottom + 1, 5, 2, ink::paper);
    // Two samples per pixel retain the narrow resonant peaks at high feedback.
    int previous = 0;
    for (int i = 0; i <= 2 * (width - 1); ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, double (i) / (2 * (width - 1)));
        const double gain = filterGain2 (kind, k, ctx.spread, hz / nominal);
        const int y = py (10.0 * std::log10 (gain / ref + 1e-12));
        if (i == 0) previous = y;
        r.box (left + i / 2, std::min (previous, y), 1, std::abs (previous - y) + 1, ink::live);
        previous = y;
    }
    r.text (left, bottom + 8, "20 Hz", ink::muted);
    r.right (right, bottom + 8, "20 kHz", ink::muted);
}
} // namespace tbmpc::plots
