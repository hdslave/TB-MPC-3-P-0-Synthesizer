// Original panel lettering. The upright stems share a 24-unit cap height.
// Diagonal cuts and stepped bowls carry through to the smaller legends.
const machineForms = {
  A: [20, 'M0 24V8L8 0H20V24H15V16H5V24Z M5 12H15V4H10L5 9Z'],
  B: [19, 'M0 0H15L19 4V9L16 12L19 15V20L15 24H0Z M5 4V10H12L14 8V4Z M5 14V20H14V16L12 14Z'],
  C: [19, 'M6 0H19V5H8L5 8V19H19V21L16 24H0V6Z'],
  D: [20, 'M0 0H12L20 8V18L14 24H0Z M5 4V20H12L15 17V10L9 4Z'],
  E: [18, 'M0 0H18L15 4H5V10H15L12 14H5V20H18V24H0Z'],
  F: [18, 'M0 0H18L15 4H5V10H16L12 15H5V21L2 24H0Z'],
  G: [20, 'M6 0H20V4H8L5 7V20H15V15H10V11H20V21L17 24H0V6Z'],
  L: [16, 'M0 0H5V19H16V21L13 24H0Z'],
  N: [20, 'M0 24V0H5L15 13V0H20V24H16L5 10V24Z'],
  O: [20, 'M6 0H20V18L14 24H0V6Z M5 8V20H12L15 17V4H9Z'],
  P: [19, 'M0 0H16L19 3V11L14 16H5V21L2 24H0Z M5 4V12H12L14 10V4Z'],
  R: [20, 'M0 0H16L19 3V10L14 15L20 21V24H15L7 16H5V24H0Z M5 4V12H11L14 9V4Z'],
  S: [19, 'M5 0H19V4H7L5 6V10H14L19 15V20L15 24H0V20H13L14 19V15H5L0 10V5Z'],
  T: [19, 'M0 0H19L16 5H12V21L9 24H7V5H0Z'],
  Y: [20, 'M0 0H5V6L10 11L15 6V0H20V8L12 16V24H7V15L0 8Z'],

  // The digits have equal advances. Zero has one keyed opening, eight has two.
  0: [18, 'M5 0H18V19L13 24H0V5Z M5 7V20H11L13 18V11H10L13 8V4H8Z'],
  1: [18, 'M2 5L8 0H13V20H17V24H2V20H8V6L5 9H2Z'],
  2: [18, 'M0 0H15L18 3V10L13 15H7L5 17V20H18V24H0V15L5 10H12L13 9V4H0Z'],
  3: [18, 'M0 0H18V9L15 12L18 15V20L14 24H0V20H11L13 18V15L11 14H5V10H10L13 7V4H0Z'],
  4: [18, 'M2 0H7V13H11V6L16 1V13H18V17H16V24H11V17H0V2Z'],
  5: [18, 'M0 0H18L15 4H5V9H13L18 14V20L14 24H0V20H12L13 19V16L11 14H0Z'],
  6: [18, 'M7 0H18L14 4H9L5 8V10H15L18 13V20L14 24H0V7Z M5 14V20H11L13 18V14Z'],
  7: [18, 'M0 0H18V6L9 15V24H4V13L13 4H0Z'],
  8: [18, 'M4 0H18V9L15 12L18 15V20L14 24H0V15L3 12L0 9V4Z M5 5V8L7 10H11L13 8V4H6Z M5 16V20H12L13 19V16L11 14H7Z'],
  9: [18, 'M4 0H18V17L11 24H1L5 20H9L13 16V14H4L0 10V4Z M5 5V9L6 10H13V4H6Z'],
  '#': [19, 'M5 1H9L8 6H12L13 1H17L16 6H19V10H15L14 14H18V18H13L12 23H8L9 18H5L4 23H0L1 18H0V14H2L3 10H0V6H4Z M7 10L6 14H10L11 10Z'],
  '-': [10, 'M0 10H10V14H0Z']
};

const machineGlyphs = new Map(Object.entries(machineForms).map(([letter, [width, outline]]) => [
  letter, { width, path: new Path2D(outline), scale: letter === '#' ? 0.72 : 1 }
]));

function machineText(ctx, text, x, cy, height, maxWidth, ink, align = 'center') {
  const glyphs = Array.from(text.toUpperCase(), letter => machineGlyphs.get(letter));
  if (!glyphs.length || glyphs.some(glyph => !glyph)) return;

  const tracking = 2.7;
  const width = glyphs.reduce((sum, glyph) => sum + glyph.width * glyph.scale, 0)
                + tracking * (glyphs.length - 1);
  const sy = height / 24;
  const sx = Math.min(sy, maxWidth / width);

  ctx.save();
  ctx.translate(align === 'center' ? x - width * sx / 2 : x, cy - height / 2);
  ctx.scale(sx, sy);
  ctx.fillStyle = ink;
  for (const glyph of glyphs) {
    ctx.save();
    ctx.scale(glyph.scale, glyph.scale);
    ctx.fill(glyph.path, 'evenodd');
    ctx.restore();
    ctx.translate(glyph.width * glyph.scale + tracking, 0);
  }
  ctx.restore();
}
