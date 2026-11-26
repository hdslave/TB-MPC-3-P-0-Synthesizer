// Razor Choir pitch monograms, in a 40 by 42 cap box.
const razorForms = {
  A:"M0 42 Q10 31 13 19 L8 22 Q19 13 24 0 Q23 13 31 21 L26 19 Q29 30 40 38 Q28 33 24 29 L22 25 Q17 24 12 28 L5 39 Z M16 21 Q20 20 23 21 L21 12 Z",
    B:"M0 41 Q7 25 8 12 L3 14 Q10 9 12 2 L13 9 Q22 8 34 1 Q32 12 23 19 Q30 19 39 12 Q35 30 16 37 L7 39 L14 31 Q27 27 29 23 Q18 22 13 26 Z M14 19 Q24 14 26 9 L16 13Z",
    C:"M38 1 Q32 15 25 19 L29 10 Q15 13 9 25 Q15 38 32 26 L39 19 Q35 33 23 38 Q14 41 6 33 L1 41 L4 28 L0 26 Q8 21 12 14 Q20 6 30 7 Z",
    D:"M1 41 Q6 26 9 11 L3 15 L11 4 Q19 8 31 0 L27 8 Q41 9 37 21 Q33 33 10 38 L15 31 Q28 26 30 18 Q31 12 17 12 L12 27Z",
    E:"M3 12 Q19 9 38 1 L30 11 L15 15 L12 21 Q23 19 33 14 L24 25 L12 26 L8 32 Q24 34 38 23 Q31 38 5 38 L0 42 L5 27 L1 28 L7 19 L10 13Z",
    F:"M1 12 Q18 10 39 1 Q33 9 29 13 L29 9 L15 15 L12 21 Q23 19 34 14 L25 24 L13 26 Q8 36 0 42 L5 27 L1 28 L7 19 L10 13Z",
    G:"M39 0 Q34 13 27 16 L30 9 Q15 13 9 25 Q12 34 19 33 Q26 32 31 25 L21 28 L25 20 Q32 20 40 16 L34 32 Q28 33 22 38 Q11 40 6 33 L1 41 L4 28 L0 26 Q8 21 13 14 Q20 6 31 6Z"
};

const chromeLetters = new Map();
const stripLights = [
  [0,8,11,25], [.09,31,41,79], [.19,123,139,181], [.28,228,237,255],
  [.33,91,108,157], [.41,29,36,62], [.50,51,41,93], [.56,113,106,171],
  [.61,38,46,80], [.67,12,16,33], [.72,50,70,138], [.79,184,202,243],
  [.835,255,255,255], [.88,179,197,230], [.95,72,90,135], [1,16,21,42]
];
const chromeRamp = Array.from({length:256}, (_, i) => {
  const t = i / 255;
  const j = stripLights.findIndex((stop, n) => n > 0 && stop[0] >= t);
  const a = stripLights[j - 1], b = stripLights[j];
  return [1, 2, 3].map(k => a[k] + (b[k] - a[k]) * (t - a[0]) / (b[0] - a[0]));
});

function chromeLetter(letter) {
  if (chromeLetters.has(letter)) return chromeLetters.get(letter);
  const unit = 4, w = 44 * unit, h = 46 * unit;
  const canvas = document.createElement('canvas');
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext('2d', {willReadFrequently:true});
  ctx.setTransform(unit, 0, 0, unit, 2 * unit, 2 * unit);
  ctx.fill(new Path2D(razorForms[letter]), 'evenodd');
  const image = ctx.getImageData(0, 0, w, h), rgba = image.data;
  const distance = new Float32Array(w * h);
  for (let i = 0; i < distance.length; i++) distance[i] = rgba[4*i+3] > 128 ? 1e4 : 0;

  // Distance to the cut edge determines the slope of each chrome bevel.
  for (let y = 1; y < h-1; y++) for (let x = 1; x < w-1; x++) {
    const i = y*w+x;
    distance[i] = Math.min(distance[i], 1+distance[i-1], 1+distance[i-w],
                           1.414+distance[i-w-1], 1.414+distance[i-w+1]);
  }
  for (let y = h-2; y > 0; y--) for (let x = w-2; x > 0; x--) {
    const i = y*w+x;
    distance[i] = Math.min(distance[i], 1+distance[i+1], 1+distance[i+w],
                           1.414+distance[i+w+1], 1.414+distance[i+w-1]);
  }

  const smooth = new Float32Array(w*h), field = new Float32Array(w*h);
  const kernel = [1,4,6,4,1];
  for (let y = 2; y < h-2; y++) for (let x = 2; x < w-2; x++) {
    const i = y*w+x;
    for (let k = -2; k <= 2; k++) smooth[i] += distance[i+k] * kernel[k+2]/16;
  }
  for (let y = 2; y < h-2; y++) for (let x = 2; x < w-2; x++) {
    const i = y*w+x;
    for (let k = -2; k <= 2; k++) field[i] += smooth[i+k*w] * kernel[k+2]/16;
  }

  for (let y = 1; y < h-1; y++) for (let x = 1; x < w-1; x++) {
    const i = y*w+x, p = 4*i;
    if (!rgba[p+3]) continue;
    const depth = distance[i]/unit;
    const slope = Math.max(.23, 1-depth/(3.9*1.22));
    const nx = (field[i-1]-field[i+1])*.5*slope;
    const ny = (field[i-w]-field[i+w])*.5*slope;
    const u = x/w, v = y/h;
    const light = .60+.34*ny-.24*nx+.16*Math.sin((v*.90+u*.30)*6.28)+.03*Math.sin(8*u-4*v);
    const rgb = chromeRamp[Math.round(255*Math.min(1,Math.max(0,light)))];
    const rim = Math.min(1,.37+depth*.94);
    const flare = 73*Math.pow(Math.max(0,-.55*nx-.72*ny),5);
    const violet = .74*(1-Math.min(1,Math.abs(light-.44)*7))*21;
    rgba[p] = rgb[0]*rim+flare+violet;
    rgba[p+1] = rgb[1]*rim+flare;
    rgba[p+2] = rgb[2]*rim+flare+violet;
  }
  ctx.putImageData(image, 0, 0);
  chromeLetters.set(letter, canvas);
  return canvas;
}

const silverFigure = [[0,'#fff'],[.38,'#d4dbf1'],[.49,'#9aa6cf'],[.54,'#f8faff'],[1,'#d3d5ed']];
const steelFigure = [[0,'#3f507d'],[.35,'#161d33'],[.44,'#657295'],[.51,'#303753'],[.62,'#10162e'],[1,'#31354c']];

function scoreFigures(ctx, text, x, cy, cap, maxWidth, light) {
  const width = text.length*19+(text.length-1)*2.5+1;
  ctx.save();
  ctx.translate(x, cy-cap/2);
  ctx.scale(Math.min(cap/24, maxWidth/width), cap/24);
  ctx.translate(-width/2, 0);
  const gradient = ctx.createLinearGradient(0, 0, 4, 24);
  for (const [position,color] of (light ? silverFigure : steelFigure)) gradient.addColorStop(position,color);
  ctx.fillStyle = gradient;
  for (const ch of text) {
    ctx.fill(scoreGlyphs.get(ch).path, 'evenodd');
    ctx.translate(21.5, 0);
  }
  ctx.restore();
}

function scorePitch(ctx, note, x, cy, height, maxWidth, selected) {
  const sharp = note[1] === '#', octave = note.slice(sharp ? 2 : 1);
  const letterWidth = height*1.20, letterHeight = height*1.40, registerSize = height*.50;
  const rightWidth = Math.max(registerSize*.82, registerSize*.72*octave.length);
  const width = letterWidth-1.1+rightWidth, right = (width-rightWidth)/2;
  ctx.save();
  ctx.translate(x,cy);
  ctx.scale(Math.min(1,maxWidth/width),1);
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(chromeLetter(note[0]), -width/2, -letterHeight/2, letterWidth, letterHeight);
  if (sharp) scoreText(ctx,'#',right,-height*.28,height*.38,rightWidth,selected?'#fff':'#171b30');
  scoreFigures(ctx,octave,right,height*.23,registerSize,rightWidth+1,selected);
  ctx.restore();
}
