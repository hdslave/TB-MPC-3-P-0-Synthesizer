// Offline plate extraction. The selected column, numbers and pitches remain
// independent, so any generated pattern can use the same small set of cuts.
const stormPlate=document.createElement('canvas');
stormPlate.width=168*4;
stormPlate.height=25*4;
const stormContext=stormPlate.getContext('2d');
const art=window.plates, density=4;
art.bed.width=780*density;art.bed.height=285*density;
const counter=art.makeStepCounter('62px SlinkSans',density);
const nativeStorm=art.panelStorm(stormContext);
function nativePlate(kind,i){
  const {bed,bedDraw,scorePitch,drawStorm}=art;
  const cv=document.createElement('canvas'),ctx=cv.getContext('2d');
  function caption(text,x,y,size,color,alignment){
    ctx.font=size+'px SlinkSans';ctx.fillStyle=color;
    ctx.textAlign=alignment;ctx.textBaseline='middle';ctx.fillText(text,x,y);
  }
  function from(source,x,y,w,h,scale=1){
    cv.width=w*scale;cv.height=h*scale;ctx.imageSmoothingQuality="high";
    ctx.drawImage(source,density*x,density*y,density*w,density*h,0,0,cv.width,cv.height);
  }
  if(kind==='backdrop'||kind==='selected'){
    bedDraw(false,true,kind==='selected'?0:-1,density);
    if(kind==='backdrop')from(bed,0,0,780,285);else from(bed,65,103,42,142);
  }else if(kind==='selectionRamp'){
    cv.width=1;cv.height=64;
    ctx.fillStyle=window.plates.selectionFill(ctx,0,cv.height);
    ctx.fillRect(0,0,1,cv.height);
  }else if(kind==='button'){
    const names=['PLAY','STOP','SYNC','NOTE'];
    from(bed,622,55,81,29,density);ctx.setTransform(density,0,0,density,-622*density,-55*density);art.drawButton(ctx,names[i]);
  }else if(kind==='counter'){
    from(bed,528,20,91,70,density);ctx.setTransform(density,0,0,density,-528*density,-20*density);counter(ctx,i,1);
  }else if(kind==='number'||kind==='pitch'){
    const number=kind==='number', w=42, h=number?20:37;
    cv.width=w*4;cv.height=h*4;ctx.scale(4,4);
    if(number)caption(String(i+1).padStart(2,'0'),w/2,h/2,15,'#f4f2ff','center');
    else{
      const pitch=i-256+36;
      const note=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][(pitch%12+12)%12]+(Math.floor(pitch/12)-1);
      scorePitch(ctx,note,w/2,h/2,23,37);
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
    cv.width=61*4;cv.height=58*4;
    ctx.setTransform(4,0,0,4,-717*4,0);
    ctx.fillStyle='#080812';ctx.fillRect(717,0,61,58);
    window.plates.drawPose(i,ctx);
  }else if(kind==='crest'){
    return window.plates.drawCrest(i).toDataURL().split(',')[1];
  }else if(kind==='storm'){
    stormContext.setTransform(4,0,0,4,-201*4,-253*4);
    stormContext.fillStyle='#080812';stormContext.fillRect(201,253,168,25);
    drawStorm(i,nativeStorm);
    return stormPlate.toDataURL().split(',')[1];
  }
  return cv.toDataURL().split(',')[1];
}
