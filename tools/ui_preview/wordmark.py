"""Fit the original chrome letterforms to the two MPC nameplates."""
from PIL import Image


def fit_wordmark(master, ink_width, height, gaps, background):
    # Source cuts lie in the dark channels between the tubes. Trim a few master
    # rows to level the upright stems while retaining the S's rounded overshoot.
    cuts = (0, 332, 530, 575, 829, master.width)
    vertical_trim = ((0, 0), (2, 0), (2, 3), (2, 2), (0, 1))
    title = Image.new(master.mode, (ink_width + sum(gaps), height), background)
    x = 0
    for i, (top, bottom) in enumerate(vertical_trim):
        left, right = cuts[i:i + 2]
        width = round(right * ink_width / master.width) - round(left * ink_width / master.width)
        letter = master.resize((width, height), Image.Resampling.LANCZOS,
                               box=(left, top, right, master.height - bottom))
        title.paste(letter, (x, 0))
        x += width
        if i < len(gaps):
            x += gaps[i]
    return title
