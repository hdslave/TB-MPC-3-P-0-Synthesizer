// Offline plate extraction. The selected column, numbers and pitches remain
// independent, so any generated pattern can use the same small set of cuts.
function nativePlate(kind,i){
  const {bed,bedDraw,paintCounter,acidText,drawStorm}=window.plates;
  const cv=document.createElement('canvas'),ctx=cv.getContext('2d');
  const panel=document.querySelector('#thorn-crown-metal canvas');
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
  }else if(kind==='number'||kind==='pitch'){
    const pitch=i-256+36;
    const note=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][(pitch%12+12)%12]+(Math.floor(pitch/12)-1);
    const w=42,h=kind==='pitch'?37:20;cv.width=w*4;cv.height=h*4;ctx.scale(4,4);
    acidText(ctx,kind==='pitch'?note:String(i+1).padStart(2,'0'),w/2,h/2,
             kind==='pitch'?23:12,kind==='pitch'?37:34,'white');
  }else if(kind==='face'){
    window.plates.drawPose(i);from(panel,717,0,61,58);
  }else if(kind==='storm'){
    drawStorm(i/12);from(panel,201,253,168,25);
  }
  return cv.toDataURL().split(',')[1];
}
