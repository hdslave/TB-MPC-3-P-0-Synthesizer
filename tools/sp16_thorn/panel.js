(async()=>{
 const root=document.getElementById('thorn-crown-metal'),panel=root.querySelector('.panel');
 const canvas=panel.querySelector('canvas'),keys=panel.querySelector('.keys'),out=canvas.getContext('2d');
 const A=THORN_ASSETS, load=src=>new Promise((ok,fail)=>{const i=new Image();i.onload=()=>ok(i);i.onerror=fail;i.src=src;});
 const [wordmark,sprite,crest,depth,crestPoster]=await Promise.all([load(A.logo),load(A.face),load(A.texture),load(A.depth),load(A.poster),...Object.entries(A.fonts).map(async([id,src])=>document.fonts.add(await new FontFace(id,'url('+src+')').load()))]);
 // Native artwork coordinates. The small jogs pass between the curved bevels.
 const logoScale=161/wordmark.naturalWidth,letterGap=1;
 const seams=[[[0,0],[0,108]],[[209,0],[209,108]],
   [[333,0],[333,97],[336,98],[336,100],[333,101],[333,108]],
   [[362,0],[362,108]],[[522,0],[522,9],[523,10],[523,13],[522,14],[522,108]],
   [[680,0],[680,108]]];
 const letterClips=seams.slice(0,-1).map((left,i)=>{
   const path=new Path2D(),polygon=left.concat(seams[i+1].slice().reverse());
   polygon.forEach(([x,y],j)=>j?path.lineTo(12+x*logoScale,12+y*logoScale):path.moveTo(12+x*logoScale,12+y*logoScale));
   path.closePath();return path;
 });
 function drawWordmark(){
   letterClips.forEach((clip,i)=>{
     g.save();g.translate((i-2)*letterGap,0);g.clip(clip);
     g.drawImage(wordmark,12,12,161,wordmark.naturalHeight*logoScale);g.restore();
   });
 }
 const crestFrame={x:186,y:6,w:332,h:90};
 const camera=document.createElement('canvas'),cameraScale=globalThis.captureThornPlates?4:2;
 camera.width=crestFrame.w*cameraScale;camera.height=crestFrame.h*cameraScale;
 let render3d;try{render3d=buildCrestView(camera,crest,depth);}catch(e){console.warn('Crest camera fallback',e);}
 const fallback=document.createElement('video');fallback.muted=true;fallback.playsInline=true;fallback.loop=true;fallback.preload='auto';
 function useFilm(){render3d=null;panel.dataset.renderer='film';if(!fallback.getAttribute('src'))fallback.src=A.film;}
 if(!render3d)useFilm();else panel.dataset.renderer='mesh';
 camera.addEventListener('webglcontextlost',e=>{e.preventDefault();useFilm();});
 const gameNotes=['F2','C3','F3','G1','G2','C2','F2','C3','C#2','C#2','F2','G2','C#2','F2','G#1','C#2'];
 const semis=[5,12,17,-5,7,0,5,12,1,1,5,7,1,5,-4,1];
 const pattern=thornStudyPattern;
 const names=['STEP','NOTE','GATE','ACCENT','SLIDE'],speech=['Step','Note','Gate','Accent','Slide'];
 const shotNames=['WIDE','WING L','CORE','WING R'];
 const two=n=>String(n).padStart(2,'0'),clamp=(v,lo,hi)=>Math.min(hi,Math.max(lo,v));
 const motionPref=matchMedia('(prefers-reduced-motion: reduce)');
 const options={motion:!motionPref.matches,speed:1};
 let seq=pattern.map(v=>v.slice()),step=2,rotation=0,seed=0x4d35,playing=false,beat=-100,clock=0;
 let lastAccentTime=-Infinity;
 let cameraOverride=null,globalMotion=options.motion,lastCameraRender=-1;
 const cameraRuns=()=>cameraOverride??options.motion;
 let time=0,camTime=0,frameId=0,lastTime=null,visible=true,needsPaint=true;
 const bed=document.createElement('canvas');bed.width=canvas.width;bed.height=canvas.height;
 const staticCtx=bed.getContext('2d');let g=staticCtx;
 const faceTile=sprite.width/8,faceFrames=33;
 const faceBuffer=document.createElement('canvas');
 faceBuffer.width=faceBuffer.height=faceTile;
 const faceCtx=faceBuffer.getContext('2d');
 let facePose=17,lastFacePose=-1;
 function drawFace(destination=g,pose){
   if(pose!==undefined)facePose=pose;
   else if(options.motion){
     const turn=.47+.32*Math.sin(time*Math.PI*2/4.8+.18);
     facePose=(faceFrames-1)*turn;
   }
   if(facePose!==lastFacePose){
     const lo=Math.floor(facePose),mix=facePose-lo;
     faceCtx.clearRect(0,0,faceTile,faceTile);
     function poseLayer(frame,weight){
       faceCtx.globalAlpha=weight;
       faceCtx.drawImage(sprite,frame%8*faceTile,Math.floor(frame/8)*faceTile,faceTile,faceTile,0,0,faceTile,faceTile);
     }
     // Add the weighted premultiplied views so the fin tips keep their alpha through a turn.
     faceCtx.globalCompositeOperation='lighter';poseLayer(lo,1-mix);
     if(mix)poseLayer(lo+1,mix);
     lastFacePose=facePose;
   }
   destination.drawImage(faceBuffer,717.5,-.85,59,59);
   return Math.round(facePose);
 }
 function rect(x,y,w,h,fill){g.fillStyle=fill;g.fillRect(x,y,w,h);}
 function path(points,fill){g.beginPath();points.forEach(([x,y],n)=>n?g.lineTo(x,y):g.moveTo(x,y));g.closePath();g.fillStyle=fill;g.fill();}
 function line(x,y,xx,yy,col,lw=1){g.beginPath();g.moveTo(x,y);g.lineTo(xx,yy);g.lineWidth=lw;g.strokeStyle=col;g.stroke();}
 function gradient(x,y,xx,yy,stops){const grd=g.createLinearGradient(x,y,xx,yy);stops.forEach(([t,v])=>grd.addColorStop(t,v));return grd;}
 function letters(s,x,y,size,col,face='SlinkSans',align='left'){g.fillStyle=col;g.font=size+'px '+face;g.textAlign=align;g.textBaseline='middle';g.fillText(s,x,y);}
 const caption=(s,x,y,size,col,align)=>letters(s,x,y,size,col,'SlinkSans',align);
 const noteAt=i=>gameNotes[(i+rotation)%16];
 function chamfer(x,y,w,h,fill,edge,clip=5){
   path([[x,y],[x+w-clip,y],[x+w,y+clip],[x+w,y+h],[x+clip,y+h],[x,y+h-clip]],edge);
   path([[x+1,y+1],[x+w-clip-.2,y+1],[x+w-1,y+clip+.3],[x+w-1,y+h-1],[x+clip+.3,y+h-1],[x+1,y+h-clip-.3]],fill);
 }
 function stripe(x,y,w,h,colors){
   return gradient(x,y,x+w*.09,y+h,colors.map(([t,c])=>[t,c]));
 }
 function bracket(x,y,w,h,color){for(const [xx,yy,sx,sy] of [[x,y,1,1],[x+w,y,-1,1],[x,y+h,1,-1],[x+w,y+h,-1,-1]]){
   line(xx,yy,xx+sx*5,yy,color,.75);line(xx,yy,xx,yy+sy*3,color,.75);}}
 const silver=[[0,'#faf9ff'],[.05,'#c3cadf'],[.37,'#dbe0ec'],[.47,'#c9cdda'],[.50,'#9fa8bf'],[.58,'#b8bfd3'],[1,'#858fae']];
 const selectionStops=[[0,'#adb7ef'],[.08,'#7c86dc'],[.42,'#585cbd'],[.52,'#4b4cab'],[1,'#33317e']];
 const selectionEdge='#a5addf';
 function selectionFill(ctx,y,h){
   const fill=ctx.createLinearGradient(0,y,0,y+h);
   for(const [stop,color] of selectionStops)fill.addColorStop(stop,color);
   return fill;
 }
 const shell=[[0,'#69728b'],[.19,'#d3dcf2'],[.41,'#7892c0'],[.5,'#333950'],[.88,'#80739d'],[1,'#b4afd3']];
 const purple=[[0,'#302041'],[.28,'#251737'],[1,'#110d21']];
 function seedCode(x,y,w,h){const bits=w/40;for(let i=0;i<20;i++){
   const v=(seed>>(i%16))&1;rect(x+i*bits*2,y,bits*(v?1.52:.60),h,i%6===0?'#ced7ff':'#919fbe');}}
 function actionButton(x,y,w,h,label,main){
   const fill=main?selectionFill(g,y,h):stripe(x,y,w,h,purple);
   chamfer(x,y,w,h,fill,main?selectionEdge:'#7b7299',4);
   caption(label,x+w/2,y+h/2,main?16:13,'#faf9ff','center');
 }
 function symbol(row,i,cx,cy,ink,sel){
   const on=seq[row-2][i];
   if(row===2){const s=10.1;if(on){rect(cx-s/2,cy-s/2,s,s,ink);
      for(let n=-3;n<4;n+=3)rect(cx+n,cy+2,1,2,sel?'#33499a':'#5a5b84');}
     else{g.strokeStyle=ink;g.lineWidth=1.25;g.strokeRect(cx-s/2+.6,cy-s/2+.6,s-1.2,s-1.2);}return;}
   if(!on){rect(cx-3.1,cy-.75,6.2,1.5,sel?'#d6dcfa':'#938aa8');return;}
   if(row===3){path([[cx,cy-5.5],[cx+5.5,cy],[cx,cy+5.5],[cx-5.5,cy]],ink);return;}
   line(cx-5.8,cy+4.3,cx+3.1,cy-2.7,ink,2);path([[cx+6,cy-5.1],[cx+4.3,cy+.9],[cx-.7,cy-3.5]],ink);
 }
 const bounds=new Map();
 function key(id,x,y,w,h,aria,fn){
   const b=document.createElement('button');b.type='button';b.className='key cursor-interaction';b.dataset.id=id;
   Object.assign(b.style,{left:x/780*100+'%',top:y/285*100+'%',width:w/780*100+'%',height:h/285*100+'%'});
   b.setAttribute('aria-label',aria);b.addEventListener('click',fn);keys.append(b);bounds.set(id,b);return b;
 }
 const scoreRows={step:103,pitch:123,note:143,gate:177,accent:200,slide:222,end:245};
 function matrix(makeKeys,plate,selected){
   const gutter=5, label=58,x=7,w=766,cw=(w-label-gutter*3)/16;
   const {step,pitch,note,gate,accent,slide,end}=scoreRows;
   const tops=[step,note,gate,accent,slide],bottoms=[pitch,gate,accent,slide,end];
   const xxAt=i=>x+label+i*cw+Math.floor(i/4)*gutter;
   const railH=note-pitch,ww=cw-1.1;
   chamfer(x,pitch,label-3,railH-1,'#231b32','#71738c',3);
   caption('PITCH',x+5,pitch+railH/2,12,'#e9eaf6');
   for(let i=0;i<16;i++){
     const xx=xxAt(i),sel=i===selected;
     rect(xx,pitch,ww,railH-1,sel?'#302c62':'#100d1a');
     for(let dy=4;dy<railH-2;dy+=5)rect(xx,pitch+dy,ww,.65,'#302840');
     if(sel)rect(xx,pitch,ww,1,selectionEdge);
   }
   if(!plate){
     const min=Math.min(...semis),max=Math.max(...semis);
     const py=i=>note-4-(semis[(i+rotation)%16]-min)*(railH-9)/Math.max(1,max-min);
     for(let i=0;i<16;i++){
       const xx=xxAt(i),yy=py(i),fg=i===selected?'#f1efff':'#b6c8fa';
       if(i && seq[2][i-1]){
         const prev=xxAt(i-1)+ww-4;
         g.beginPath();g.moveTo(prev,py(i-1));g.bezierCurveTo(prev+4,py(i-1),xx,yy,xx+4,yy);
         g.lineWidth=1;g.strokeStyle='#b9b1cb';g.stroke();
       }
       if(seq[0][i])rect(xx+4,yy-1,ww-8,3,fg);
       else{g.lineWidth=1;g.strokeStyle=fg;g.strokeRect(xx+4.5,yy-1,ww-9,3);}
       if(seq[1][i])path([[xx+ww/2-2,pitch+4],[xx+ww/2,pitch+2],[xx+ww/2+2,pitch+4]],'#b6c8fa');
     }
   }
   for(let r=0;r<5;r++){
     const yy=tops[r],rh=bottoms[r]-yy,ink=r===1?'#151629':'#e9eaf6';
     const lab=r===1?stripe(x,yy,label,rh,silver):stripe(x,yy,label,rh,[[0,'#332840'],[.55,'#251c34'],[1,'#19132a']]);
     chamfer(x,yy,label-3,rh-1,lab,'#71738c',3);caption(names[r],x+5,yy+rh/2,r===1?17:13,ink);
     for(let i=0;i<16;i++){
       const bank=Math.floor(i/4),xx=x+label+i*cw+bank*gutter, ww=cw-1.1,sel=i===selected;
       if(i%4===0&&i>0){line(xx-3.5,yy+2,xx-1,yy+.4,'#76778f',.7);}
       const bg=r===1?'#000':sel?selectionFill(g,yy,rh):
          gradient(xx,yy,xx,yy+rh,[[0,bank%2?'#30213f':'#262139'],[1,bank%2?'#1a112c':'#141224']]);
       rect(xx,yy,ww,rh-1,bg);line(xx+.5,yy+.6,xx+ww-.5,yy+.6,sel?selectionEdge:r===1?'#484b59':'#52465f',.75);
       if(r===1&&sel){
         g.strokeStyle=selectionEdge;g.lineWidth=1.5;g.strokeRect(xx+.75,yy+.75,ww-1.5,rh-2.5);
       }
       if(r===1&&!sel){line(xx+.7,yy+2,xx+.7,yy+rh-3,'#373a44',.65);line(xx+ww-1,yy+3,xx+ww-1,yy+rh-2,'#14151a',.8);}
       if(!sel&&r===0&&i%4===0)rect(xx+2,yy+2,7,1,'#aba9c0');
       if(sel&&r===0)path([[xx+2,yy+2],[xx+8,yy+2],[xx+2,yy+8]],'#f2f4ff');
       if(plate)continue;
       const cx=xx+ww/2,cy=yy+rh/2,fg=sel?'#fff':r===1?'#13172c':'#d6d9ea';
       if(r===0)caption(two(i+1),cx,cy,15,fg,'center');
       else if(r===1)scorePitch(g,noteAt(i),cx,cy,23,ww-5);
       else symbol(r,i,cx,cy,fg,sel);
       if(makeKeys)key(r+'-'+i,xx,r===1?pitch:yy,ww,r===1?gate-pitch:rh,(r<=1?'Select step '+(i+1):speech[r]+' on step '+(i+1)),()=>hit(r,i));
     }
   }
 }
 function miniPitch(x,y,w,h){for(let i=0;i<16;i++){
   const lv=(semis[(i+rotation)%16]+5)/22, yy=y+2+(1-lv)*(h-4),u=x+i*w/16;
   rect(u,yy,w/16-1.2,1.3,i===step?'#cabaff':'#7c8cc6');if(seq[2][i]&&i<15){
     const n=(semis[(i+1+rotation)%16]+5)/22;line(u+w/16-1,yy,x+(i+1)*w/16,y+2+(1-n)*(h-4),'#7689c0',.7);}
 }}
 function bedDraw(first=false,plate=false,selected=step,density=2){
   g=staticCtx;g.setTransform(density,0,0,density,0,0);
   rect(0,0,780,285,'#080812');
   const edge=gradient(0,0,780,90,shell);
   chamfer(6,5,173,92,'#10101b',edge,8);drawWordmark();
   line(12,48,173,48,'#7c7691',.8);line(12,50,173,50,'#292735',.75);
   caption('SEED',15,62,10,'#abadc4');if(!plate){caption(seed.toString(16).toUpperCase().padStart(4,'0'),14,81,17,'#edeef7');seedCode(72,59,98,26);}
   chamfer(185,5,334,92,'#05050d',edge,8);
   chamfer(525,5,188,92,stripe(525,5,188,92,purple),edge,8);
   drawStepGlass(g);
   caption('STEP',534,16,10,'#d2d4ea');if(!plate)caption('/16',615,83,11,'#c1bfdc','right');
   if(!plate){
     miniPitch(625,17,77,23);
     actionButton(624,60,81,29,playing?'Stop':'Play',true);
     actionButton(720,68,53,26,'Regen',false);
   }
   matrix(first,plate,selected);
   chamfer(7,253,185,25,'#201730','#7b758e',4);if(!plate)caption('C / CHROMATIC',17,266,11,'#ddd6eb');
   for(let i=0;i<17;i++)line(129+i*3.1,271,136+i*3.1,260,'#77678c',.9);
   line(201,252,369,252,'#525b79',.7);line(201,278,369,278,'#4c4860',.7);
   bracket(376,254,165,23,'#7380a1');if(!plate)miniPitch(382,256,154,16);
   line(552,265,771,265,'#454359',.8);
   for(let i=0;i<16;i++){const x=558+13.6*i,hi=!plate&&i===step;rect(x,hi?258:262,hi?6:(i%4===0?2:1),hi?14:(i%4===0?9:6),hi?'#8facff':i%4===0?'#cacbdd':'#938ca9');}
   g=out;
 }
 function cameraLabel(){
   const t=camTime%20;return t<3||t>=18.5?0:t<9?1:t<12.6?2:3;
 }
 function syncUI(){
   for(let r=0;r<5;r++)for(let i=0;i<16;i++){
     const b=bounds.get(r+'-'+i);b.setAttribute('aria-pressed',String(r<2?i===step:!!seq[r-2][i]));
     if(r===1)b.setAttribute('aria-label','Select step '+(i+1)+', '+noteAt(i));
   }
   if(globalMotion!==options.motion){globalMotion=options.motion;cameraOverride=null;lastAccentTime=-Infinity;}
   const cameraOn=cameraRuns(),transportLabel=playing?'Stop sequence preview':'Play sequence preview';
   bounds.get('camera').setAttribute('aria-label',cameraOn?'Pause 3D viewer':'Play 3D viewer');
   bounds.get('transport').setAttribute('aria-label',transportLabel);
   root.querySelector('[data-do=transport]').textContent=playing?'Stop':'Play';
   root.querySelector('[data-do=motion]').textContent=options.motion?'Hold motion':'Resume motion';
   if(!cameraOn)fallback.pause();
   for(const [r,k] of ['gate','accent','slide'].entries())root.querySelector('[data-do='+k+']').setAttribute('aria-pressed',String(!!seq[r][step]));
   root.querySelector('[data-detail]').textContent=two(step+1)+' / 16 · '+noteAt(step);panel.dataset.step=String(step+1);panel.dataset.motion=String(options.motion);
 }
 function update(){bedDraw();syncUI();needsPaint=true;schedule();}
 function strikeAccent(){
   if(options.motion&&seq[0][step]&&seq[1][step])lastAccentTime=time;
 }
 function hit(r,i){step=(i+16)%16;if(r>=2)seq[r-2][step]^=1;beat=time;strikeAccent();
   root.querySelector('.sr').textContent='Step '+(step+1)+', '+(r<2?noteAt(step):speech[r]+(seq[r-2][step]?' on':' off'));update();}
 function regen(){seed=(25173*seed+13849)&65535;rotation=(rotation+5+(seed%7))%16;seq=pattern.map(arr=>arr.map((_,i)=>arr[(i+rotation)%16]));beat=time;update();}
 function tick(){step=(step+1)%16;if(step%4===0||seq[0][step]&&seq[1][step])beat=time;strikeAccent();update();}
 function transport(){playing=!playing;if(playing){beat=time;strikeAccent();clock=setInterval(tick,125);}else{clearInterval(clock);clock=0;}update();}
 function toggleMotion(){options.motion=!options.motion;cameraOverride=null;lastTime=null;update();}
 function toggleCamera(){cameraOverride=!cameraRuns();lastTime=null;update();}
 const panelStorm=(ctx,origin)=>makeWhipStorm(ctx,{composition:makeFooterPanorama(167),detail:2,...origin});
 const drawStorm=panelStorm(out);
 function paintStorm(at,draw=drawStorm){draw(at,seq[0],seq[1],seq[2],at-lastAccentTime);}
 const paintCounter=makeStepCounter('62px SlinkSans');
 let counterBeat=-100;
 function paint(){
   g=out;g.setTransform(1,0,0,1,0,0);g.drawImage(bed,0,0);g.setTransform(2,0,0,2,0,0);
   const {x,y,w,h}=crestFrame;
   g.save();g.beginPath();g.moveTo(x,y);g.lineTo(x+w-7.2,y);g.lineTo(x+w,y+7.3);g.lineTo(x+w,y+h);g.lineTo(x+7.3,y+h);g.lineTo(x,y+h-7.3);g.clip();
   if(render3d){
     if(lastCameraRender!==camTime){render3d(camTime);lastCameraRender=camTime;}
     g.drawImage(camera,x,y,w,h);
   }
   else if(fallback.readyState>=2){const desired=camTime%20,diff=Math.abs(fallback.currentTime-desired);
     fallback.playbackRate=options.speed;
     if(!fallback.seeking&&(diff>.22&&diff<19.75))fallback.currentTime=desired;
     if(cameraRuns()&&!document.hidden&&visible){if(fallback.paused)fallback.play().catch(()=>{});}else fallback.pause();
     g.drawImage(fallback,x,y,w,h);}
   else g.drawImage(crestPoster,x,y,w,h);
   g.restore();
   if(options.motion)counterBeat=beat;
   paintCounter(g,step+1,time-counterBeat);
   const pose=drawFace();
   const shine=.5+.5*Math.cos(time*.52);
   line(724,58,769,58,gradient(724,58,769,58,[[0,'#242235'],[.25+.45*shine,'#9baac9'],[1,'#332541']]),.8);
   paintStorm(time);const xx=382+((time*.065)%1)*151;
   const sweep=gradient(xx-8,0,xx+8,0,[[0,'rgba(131,158,252,0)'],[.5,'rgba(157,180,255,.28)'],[1,'rgba(131,158,252,0)']]);rect(xx-8,256,16,18,sweep);
   panel.dataset.pose=String(pose);panel.dataset.shot=shotNames[cameraLabel()];needsPaint=false;
 }
 function schedule(){if(!frameId&&visible&&!document.hidden)frameId=requestAnimationFrame(frame);}
 function frame(now){
   frameId=0;if(!visible||document.hidden){lastTime=null;return;}
   const moving=options.motion||cameraRuns();
   if(lastTime!==null && !needsPaint && moving && now-lastTime<1000/30-.5){
     // The footer can move on each display refresh while the crest keeps its own cadence.
     if(options.motion)paintStorm(time+(now-lastTime)/1000);
     schedule();return;
   }
   const dt=lastTime===null?0:Math.max(0,Math.min((now-lastTime)/1000,.15));lastTime=now;
   if(options.motion)time+=dt;
   if(cameraRuns())camTime+=dt*options.speed;
   if(needsPaint||moving)paint();
   if(moving)schedule();else lastTime=null;
 }
 bedDraw(true);
 key('camera',crestFrame.x,crestFrame.y,crestFrame.w,crestFrame.h,'Pause 3D viewer',toggleCamera);
 key('transport',624,60,81,29,'Play sequence preview',transport);key('regen',718,66,57,30,'Regenerate pattern',regen);
 const mobileActions={prev:()=>hit(0,step-1),next:()=>hit(0,step+1),transport,gate:()=>hit(2,step),accent:()=>hit(3,step),slide:()=>hit(4,step),regen,motion:toggleMotion};
 root.querySelector('.compact').addEventListener('click',e=>mobileActions[e.target.dataset.do]?.());
 keys.addEventListener('keydown',e=>{const m=e.target.dataset.id?.match(/^(\d+)-(\d+)$/);const d={ArrowUp:[-1,0],ArrowDown:[1,0],ArrowLeft:[0,-1],ArrowRight:[0,1]}[e.key];if(!m||!d)return;e.preventDefault();bounds.get(clamp(+m[1]+d[0],0,4)+'-'+((+m[2]+d[1]+16)%16)).focus();});
 function suspend(){lastTime=null;if(frameId)cancelAnimationFrame(frameId);frameId=0;if(playing)transport();fallback.pause();}
 new IntersectionObserver(([e])=>{visible=e.isIntersecting;if(visible){needsPaint=true;schedule();}else suspend();}).observe(root);
 document.addEventListener('visibilitychange',()=>{if(document.hidden)suspend();else{needsPaint=true;schedule();}});
 motionPref.addEventListener('change',()=>{options.motion=!motionPref.matches;lastTime=null;update();});
 fallback.addEventListener('loadeddata',()=>{needsPaint=true;schedule();});fallback.addEventListener('seeked',()=>{needsPaint=true;schedule();});
 if(globalThis.captureThornPlates){
   globalThis.captureThornPlates({bed,bedDraw,makeStepCounter,scorePitch,scoreRows,selectionEdge,selectionFill,drawStorm:paintStorm,panelStorm,
     drawCrest:t=>{render3d(t);return camera;},
     drawButton:(ctx,label)=>{const saved=g;g=ctx;actionButton(622,55,81,29,label,true);g=saved;},
     faceFrames,drawPose:(pose,ctx)=>drawFace(ctx,pose)});
 }
 syncUI();paint();schedule();root.dataset.ready='true';
 if(globalThis.Tweak){const tweak=new Tweak({container:panel,onChange:()=>{lastTime=null;update();}});
   tweak.addToggle(options,'motion',{label:'Panel motion'});tweak.addSlider(options,'speed',{label:'Camera speed',min:.5,max:1.5,step:.25,unit:'×'});}
})().catch(e=>{document.getElementById('thorn-crown-metal').dataset.error=String(e);console.error(e);});
