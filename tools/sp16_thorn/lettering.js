// Original outlines on a 24-unit cap. Flat rails end in a long bevel.
// The round sides share a smooth bend into those rails.
const scoreForms = {
  A: [24, 'M0 24L11.2 3.4Q12.8 .4 16.1 .4L20.8 0L24 24L17.9 20.2L17.3 16.2H9.1L5.9 21.2Z M10.8 12.6H16.8L15.5 4.5Z'],
  B: [24, 'M0 1.1L15.8 0Q23.7 0 23.7 5.5Q23.7 10.3 17.2 12Q22.1 12.4 22.1 16.7Q22.1 23.6 11.4 24H0L5.4 19.3H11.8Q16.7 19.3 16.7 16.4Q16.7 13.5 12.3 13.5H5.4V17.1L0 21.8Z M5.4 10.1H12.4Q17.9 10.1 17.9 6.6Q17.9 3.6 13.7 3.6H5.4Z'],
  C: [24, 'M24 0L18.6 5.8L18.9 3.8H13.9Q5.5 3.8 5.5 13.8Q5.5 19.7 11.7 19.7H16.2Q20.2 19.7 23.1 17.2L17.4 23.6L10.1 24Q0 24 0 14.1Q0 1.1 12.5 .6Z'],
  D: [24, 'M0 0H11.6Q24 0 24 9.9Q24 23.4 9.3 24H0L5.7 19.4H10Q18.4 19.4 18.4 10.5Q18.4 3.6 11.1 3.6H5.5V17.2L0 21.9Z'],
  E: [23, 'M0 1L23 0L18.6 4H6L0 8.3Z M0 10.5L5.5 6.3V10.5H19.6L15.3 14.1H5.5V20H23L18 24H0Z'],
  F: [24, 'M0 1L24 0L19.2 4.1H6L0 8.3Z M0 10.5L5.5 6.3V10.5H20L15.3 14.2H5.5V18.7L0 24Z'],
  G: [24, 'M24 0L18.6 5.8L18.9 3.8H13.7Q5.5 3.8 5.5 13.7Q5.5 19.9 12 19.9H16.8L17.7 15H10.8L15.7 10.8H23.8L22.2 21.7L18.5 24H10.1Q0 24 0 14.1Q0 1.1 12.5 .6Z'],
  L: [18, 'M0 2.5L5.3 0V20H18L14.1 24H0Z'],
  N: [24, 'M0 24V0H4.4L18.8 15.7V2.5L24 0V24H19.7L5.2 8.2V21.6Z'],
  O: [25, 'M12.7 0H15Q25 0 25 10.2Q25 24 12 24H10Q0 24 0 13.7Q0 0 12.7 0Z M12.6 3.8Q5.5 3.8 5.5 13.5Q5.5 20.2 11.3 20.2H12.7Q19.5 20.2 19.5 10.5Q19.5 3.8 14 3.8Z'],
  P: [23, 'M0 1L14 .1Q22.8 0 22.8 6.8Q22.8 15.3 12.4 15.3H7.3L11.3 11.6H12.4Q17.3 11.6 17.3 7Q17.3 3.8 13 3.8H5.4V19.3L0 24Z'],
  R: [23, 'M0 24V0H14.5Q22.5 0 22.5 6.6Q22.5 13 15 14.7L23 24H19Q16.7 24 15.4 22.3L8.7 14.9H5.4V21Z M5.4 11.5H12Q17.1 11.5 17.1 7Q17.1 3.7 13 3.7H5.4Z'],
  S: [22, 'M22 0L18 4H10Q5.6 4 5.6 6.8Q5.6 8.8 9 9.6L15.7 11.2Q22 12.7 22 17.1Q22 24 12.2 24H0L3.7 20H12.3Q16.4 20 16.4 17.4Q16.4 15.6 13.1 14.8L6.5 13Q0 11.3 0 7Q0 0 10 0Z'],
  T: [22, 'M0 0H22L18 4H13.6V20.5L8.1 24V4H0Z'],
  Y: [23, 'M0 0H5.6L11.2 10.6L18 1.6Q19.2 0 21.5 0H23L13.3 14.5V20.7L8 24V14.4Z'],

  // Tabular figures for aligning steps across the score.
  0: [19, 'M10.2 0H12Q19 0 19 8.8V13.6Q19 24 8.7 24H7.4Q0 24 0 15.4V10.6Q0 0 10.2 0Z M9.8 3.8Q5 3.8 4.9 10.4V15.3Q4.9 20.3 8.6 20.3Q14.2 20.3 14.2 13.6V8.9Q14.2 3.8 10.8 3.8Z'],
  1: [19, 'M1 11Q9 6 15 0L10 16Q9 21 14 22L3 24Q5 18 7 9Z'],
  2: [19, 'M1 7Q4 0 14 0L19 1Q18 8 11 12L5 18Q11 21 19 15L14 23L0 24Q2 17 8 12Q16 5 12 4Q7 3 1 7Z'],
  3: [19, 'M0 5Q8 3 19 0L17 6L11 11Q23 8 16 19Q10 25 0 24L3 18Q8 22 13 16Q16 12 7 14L7 11L13 5Z'],
  4: [19, 'M0 14.7L10.8 0H16.4V14.5H19V18.4H16.4V24H11.5V18.4H0Z M4.4 14.5H11.5V4.6Z'],
  5: [19, 'M2 0H19L15.5 3.8H6.1L5.2 9.4H10.9Q18.7 9.4 18.7 15.4Q18.7 24 8.7 24H0L3.7 20.2H8.6Q13.7 20.2 13.7 16Q13.7 13.1 9.5 13.1H.2Z'],
  6: [19, 'M17.5 0L13.8 3.7Q6.7 3.8 5 10.3Q7.2 9 11.8 9Q19 9 19 15.3Q19 24 9.2 24Q0 24 0 15Q0 0 14.2 0Z M4.9 15.2Q4.7 20.5 9.1 20.5Q14.3 20.5 14.3 15.8Q14.3 12.6 10.3 12.6Q6.7 12.6 4.9 15.2Z'],
  7: [19, 'M0 0H19V3.6L8.2 20.9L1.7 24L13.2 4H0Z'],
  8: [19, 'M10.4 0Q19 0 19 6.4Q19 9.9 14.7 12Q18.2 13.1 18.2 16.6Q18.2 24 8.1 24Q0 24 0 17.2Q0 13.2 4.6 11.1Q1.2 9.5 1.2 6.9Q1.2 0 10.4 0Z M10.5 3.4Q6 3.4 6 6.9Q6 9.6 9.5 9.6Q14.1 9.6 14.1 6Q14.1 3.4 10.5 3.4Z M9.1 13.3Q4.9 13.3 4.9 17.4Q4.9 20.6 8.6 20.6Q13.4 20.6 13.4 16.6Q13.4 13.3 9.1 13.3Z'],
  9: [19, 'M9.9 0Q19 0 19 9Q19 24 4.8 24H1L4.7 20.3Q12.3 20 14 13.7Q11.5 15 7.4 15Q0 15 0 8.7Q0 0 9.9 0Z M9.8 3.5Q4.8 3.5 4.8 8.3Q4.8 11.4 8.8 11.4Q12.1 11.4 14.1 8.9Q14.3 3.5 9.8 3.5Z'],
  '#': [22, 'M5.5 0H9.8L9 5H14L14.8 0H19L18.1 5H22V9.3H17.4L16.6 14.5H21V19H16L15.1 24H10.8L11.6 19H6.8L6 24H1.7L2.6 19H0V14.5H3.4L4.2 9.3H.7V5H5Z M8.4 9.3L7.5 14.5H12.4L13.3 9.3Z'],
  '-': [12, 'M.5 10H12L10.5 14H0Z']
};

const scoreGlyphs = new Map(Object.entries(scoreForms).map(([letter, [width, outline]]) =>
  [letter, { width, path: new Path2D(outline) }]));
const scoreTracking = 2.2;
const scoreLean = 0.22;
const scoreOverhang = 24 * scoreLean;

function runWidth(text) {
  let width = 0;
  for (const ch of text) width += scoreGlyphs.get(ch).width + scoreTracking;
  return width - scoreTracking;
}

// x is the left edge of the slanted run, including the overhang of the top rail.
function drawRun(ctx, text, x, y, scale) {
  ctx.save();
  ctx.translate(x + scoreOverhang * scale, y);
  ctx.transform(scale, 0, -scoreLean * scale, scale, 0, 0);
  for (const ch of text) {
    const glyph = scoreGlyphs.get(ch);
    ctx.fill(glyph.path, 'evenodd');
    ctx.translate(glyph.width + scoreTracking, 0);
  }
  ctx.restore();
}

function scoreText(ctx, text, x, cy, height, maxWidth, ink, align = 'center') {
  const run = text.toUpperCase();
  const width = runWidth(run) + scoreOverhang;
  const sy = height / 24, sx = Math.min(sy, maxWidth / width);
  ctx.save();
  ctx.translate(align === 'center' ? x - width * sx / 2 : x, cy - height / 2);
  ctx.scale(sx, sy);
  ctx.fillStyle = ink;
  drawRun(ctx, run, 0, 0, 1);
  ctx.restore();
}
