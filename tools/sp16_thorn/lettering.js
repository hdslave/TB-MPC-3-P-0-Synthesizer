// Original display cuts on a 24-unit cap. Curved stems pinch at the waist,
// then open into pointed terminals. The two crossbars of the sharp stay straight.
const acidForms = {
  A: [20,'M0 24 Q5 15 9 4 L16 0 Q15 12 20 24 L13 24 L12 18 L6 18 L4 24Z M7 14 L12 14 L12 6Z'],
  B: [18,'M2 1 Q7 2 12 1 C20 -1 21 7 15 11 C22 13 18 23 10 24 L0 24 Q4 15 2 1Z M7 5 L7 10 C13 11 16 3 10 4Z M7 14 L6 20 C15 22 17 12 10 14Z'],
  C: [18,'M18 2 L16 9 Q11 2 7 8 C2 15 7 23 14 18 L18 14 Q16 24 8 24 C-3 24 -1 11 4 5 Q9 -2 18 2Z'],
  D: [19,'M2 1 Q7 3 12 1 C24 -1 20 22 10 24 L0 24 Q4 14 2 1Z M7 6 Q9 12 6 19 C14 23 18 5 12 5Z'],
  E: [17,'M1 1 Q9 2 17 0 L15 6 Q10 4 7 6 L7 10 L16 8 L13 14 L7 14 L6 19 Q11 21 17 16 L14 24 L0 24 Q4 13 1 1Z'],
  F: [17,'M1 1 Q8 3 17 0 L15 7 Q11 4 7 6 L7 11 L16 9 L13 15 L7 15 Q7 20 5 24 L0 24 Q4 14 1 1Z'],
  G: [19,'M18 2 L17 9 Q12 3 8 7 C2 13 5 23 12 19 L13 15 L9 15 L12 11 L19 11 Q16 17 17 21 C6 28 -2 23 1 12 C3 3 11 -1 18 2Z'],
  L: [16,'M2 2 L9 0 Q6 11 6 19 Q11 21 16 16 L14 24 L0 24 Q4 12 2 2Z'],
  N: [20,'M1 2 L6 0 Q9 6 15 13 L14 2 L20 0 Q17 14 18 24 L14 24 L6 12 Q8 18 5 24 L0 24 Q3 12 1 2Z'],
  O: [19,'M11 0 C24 -2 20 19 11 23 C-2 29 -3 11 4 4 Q7 1 11 0Z M13 5 C7 2 2 19 7 20 C12 23 17 7 13 5Z'],
  P: [18,'M1 1 Q6 3 12 1 C22 -2 20 16 9 16 L6 16 L6 23 L0 24 Q4 13 1 1Z M7 6 L7 12 C14 14 17 4 11 5Z'],
  R: [19,'M1 1 Q6 3 12 1 C21 -2 21 11 13 14 Q13 19 19 23 L12 24 Q9 20 9 15 L6 16 L6 23 L0 24 Q4 13 1 1Z M7 6 L7 12 C14 14 17 4 11 5Z'],
  S: [18,'M18 1 L16 8 Q11 4 7 6 C2 9 8 10 12 11 C22 14 14 25 5 24 Q2 24 0 22 L2 16 Q7 22 12 17 Q14 15 7 14 C-3 12 1 3 8 1 Q12 0 18 1Z'],
  T: [18,'M0 2 Q7 4 18 0 L16 7 L12 6 Q10 17 11 23 L4 24 Q7 15 7 7 L0 8Z'],
  Y: [19,'M0 2 L6 0 Q6 6 10 11 Q14 6 14 2 L19 0 Q19 8 12 15 Q10 20 12 23 L5 24 Q7 18 7 15 Q1 9 0 2Z'],
  0: [17,'M10 1 C22 -2 17 17 13 21 C6 28 -3 23 1 12 Q4 2 10 1Z M12 5 C8 1 2 18 6 20 C11 24 16 7 12 5Z'],
  1: [12,'M1 6 L9 1 L12 1 Q8 11 8 20 L11 20 L10 24 L0 24 L1 20 L4 19 L6 7 L1 10Z'],
  2: [17,'M1 5 Q6 0 12 1 C20 2 17 10 12 14 L6 19 Q11 21 17 17 L14 24 L0 24 L1 19 Q4 15 9 11 C16 5 11 2 6 6 L2 10Z'],
  3: [17,'M1 3 Q9 2 17 0 L16 6 L10 11 C23 11 16 25 5 24 L0 22 L2 17 Q8 22 12 17 Q14 13 5 14 L5 11 L11 6 L1 8Z'],
  4: [18,'M9 1 L14 0 L5 14 L11 14 L13 8 L18 7 L16 14 L18 14 L17 18 L15 18 L15 23 L9 24 L10 18 L0 18 L0 14Z'],
  5: [17,'M3 2 Q10 3 17 0 L15 6 L7 6 L6 10 C21 7 18 23 8 24 Q3 25 0 22 L2 17 Q8 22 12 17 C15 12 8 12 2 15Z'],
  6: [17,'M16 0 L15 6 Q9 5 6 11 C20 4 20 23 8 24 C-5 26 0 7 9 2Z M6 15 C3 20 9 23 12 17 C14 12 9 13 6 15Z'],
  7: [17,'M1 2 Q10 3 17 0 L16 6 Q8 14 7 23 L1 24 Q3 14 11 6 L0 8Z'],
  8: [17,'M10 0 C19 -1 20 7 13 12 C21 15 16 24 7 24 C-1 25 -3 17 4 12 C-1 8 3 1 10 0Z M10 5 C5 5 5 10 9 10 C14 9 15 4 10 5Z M7 14 C1 18 6 23 11 18 C14 15 10 13 7 14Z'],
  9: [17,'M8 1 C21 -3 18 14 11 21 Q7 24 1 24 L2 19 Q8 19 11 13 C-3 19 -2 4 8 1Z M8 6 C2 12 9 13 12 9 C15 4 11 3 8 6Z'],
  '#': [16,'M5 2 L8 2 L7 8 L11 8 L12 2 L15 2 L14 8 L16 8 L16 11 L13 11 L12 15 L15 15 L15 18 L12 18 L11 23 L8 23 L9 18 L5 18 L4 23 L1 23 L2 18 L0 18 L0 15 L3 15 L4 11 L1 11 L1 8 L4 8Z M7 11 L6 15 L10 15 L11 11Z'],
  '-': [10,'M0 12 L10 10 L9 14 L0 16Z']
};
const acidGlyphs=new Map(Object.entries(acidForms).map(([letter,[width,d]])=>[letter,{width,path:new Path2D(d)}]));
function acidText(ctx,text,x,cy,height,maxWidth,ink,align='center',wave=false){
  const glyphs=Array.from(text.toUpperCase(),ch=>acidGlyphs.get(ch));
  if(glyphs.some(g=>!g))return;
  const space=2.2;
  const scale=glyphs.map(g=>g===acidGlyphs.get('#') ? .66 : 1);
  const advance=glyphs.reduce((sum,g,i)=>sum+g.width*scale[i]+space,0)-space;
  const sy=height/24,sx=Math.min(sy,maxWidth/(advance+2.4));
  ctx.save();ctx.translate(x-(align==='center'?(advance+2.4)*sx/2:0)+2.4*sx,cy-height/2);
  ctx.transform(sx,0,-.10*sx,sy,0,0);ctx.fillStyle=ink;
  for(let i=0;i<glyphs.length;i++){
    const glyph=glyphs[i],k=scale[i];
    ctx.save();ctx.translate(0,k<1?1.5:wave?1.45*Math.sin(i*1.55):0);
    ctx.scale(k,k);
    if(wave){
      ctx.translate(-.9,1.8);ctx.fillStyle='#8277c3';ctx.fill(glyph.path,'evenodd');
      ctx.translate(.9,-1.8);ctx.fillStyle=ink;
    }
    ctx.fill(glyph.path,'evenodd');ctx.restore();ctx.translate(glyph.width*k+space,0);
  }
  ctx.restore();
}
