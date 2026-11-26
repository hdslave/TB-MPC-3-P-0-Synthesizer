// Offline plate extraction. The selected column, numbers and pitches remain
// independent, so any generated pattern can use the same small set of cuts.
function nativePlate(kind,i){
  const {bed,bedDraw,paintCounter,scoreFigures,scorePitch,drawStorm}=window.plates;
  const cv=document.createElement('canvas'),ctx=cv.getContext('2d');
  const panel=document.querySelector('#thorn-crown-metal canvas');
  function caption(text,x,y,size,color,alignment){
    ctx.font=size+'px SlinkSans';ctx.fillStyle=color;
    ctx.textAlign=alignment;ctx.textBaseline='middle';ctx.fillText(text,x,y);
  }
  function from(source,x,y,w,h){
    cv.width=w;cv.height=h;ctx.imageSmoothingQuality="high";
    ctx.drawImage(source,2*x,2*y,2*w,2*h,0,0,w,h);
  }
  if(kind==='backdrop'||kind==='selected'){
    bedDraw(false,true,kind==='selected'?0:-1);
    if(kind==='backdrop')from(bed,0,0,780,285);else from(bed,65,103,42,142);
  }else if(kind==='button'){
    const names=['PLAY','STOP','SYNC','NOTE'];
    window.plates.drawButton(names[i]);
    from(panel,622,55,81,29);
  }else if(kind==='counter'){
    from(bed,528,20,91,70);ctx.translate(-528,-20);paintCounter(ctx,i,1);
  }else if(kind==='number'||kind==='pitch'||kind==='pitchSelected'){
    const number=kind==='number', w=42, h=number?20:37;
    cv.width=w*4;cv.height=h*4;ctx.scale(4,4);
    if(number)caption(String(i+1).padStart(2,'0'),w/2,h/2,15,'#f4f2ff','center');
    else{
      const pitch=i-256+36;
      const note=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][(pitch%12+12)%12]+(Math.floor(pitch/12)-1);
      scorePitch(ctx,note,w/2,h/2,23,37,kind==='pitchSelected');
    }
  }else if(kind==='bankLabel'){
    const names=['PERFORM','VOICE','GLIDE','LFO','PATTERN','CLOCK'];
    cv.width=76*4;cv.height=20*4;ctx.scale(4,4);
    caption(names[i],38,10,13,'#f4f2ff','center');
  }else if(kind==='plotTitle'){
    const names=['DIODE','TWIN PEAK','18 DB','PITCH LFO','CUTOFF LFO'];
    cv.width=155*4;cv.height=24*4;ctx.scale(4,4);
    caption(names[i],1,12,17,'#e2e6ff','left');
  }else if(kind==='face'){
    window.plates.drawPose(i);from(panel,717,0,61,58);
  }else if(kind==='crest'){
    from(window.plates.drawCrest(i/30),0,0,332,90);
  }else if(kind==='storm'){
    drawStorm(i/30);from(panel,201,253,168,25);
  }
  return cv.toDataURL().split(',')[1];
}
