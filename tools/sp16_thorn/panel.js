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
 const camera=document.createElement('canvas');camera.width=crestFrame.w*2;camera.height=crestFrame.h*2;
 let render3d;try{render3d=buildCrestView(camera,crest,depth);}catch(e){console.warn('Crest camera fallback',e);}
 const fallback=document.createElement('video');fallback.muted=true;fallback.playsInline=true;fallback.loop=true;fallback.preload='auto';
 function useFilm(){render3d=null;panel.dataset.renderer='film';if(!fallback.getAttribute('src'))fallback.src=A.film;}
 if(!render3d)useFilm();else panel.dataset.renderer='mesh';
 camera.addEventListener('webglcontextlost',e=>{e.preventDefault();useFilm();});
 const gameNotes=['F2','C3','F3','G1','G2','C2','F2','C3','C#2','C#2','F2','G2','C#2','F2','G#1','C#2'];
 const semis=[5,12,17,-5,7,0,5,12,1,1,5,7,1,5,-4,1];
 const pattern=[[1,1,1,1,0,0,1,1,0,1,1,1,0,1,1,0],[0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],[0,0,1,0,0,0,1,0,0,0,0,0,1,0,0,1]];
 const names=['STEP','NOTE','GATE','ACC','SLD'],speech=['Step','Note','Gate','Accent','Slide'];
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
 function drawFace(){
   if(options.motion){
     const age=time-beat;
     const hit=age>=0&&age<.37?Math.sin(Math.PI*age/.37)**2:0;
     const turn=.47+.32*Math.sin(time*Math.PI*2/4.8+.18);
     facePose=(faceFrames-1)*clamp(turn+.30*hit,0,1);
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
   g.drawImage(faceBuffer,717.5,-.85,59,59);
   return Math.round(facePose);
 }
 function rect(x,y,w,h,fill){g.fillStyle=fill;g.fillRect(x,y,w,h);}
 function path(points,fill){g.beginPath();points.forEach(([x,y],n)=>n?g.lineTo(x,y):g.moveTo(x,y));g.closePath();g.fillStyle=fill;g.fill();}
 function line(x,y,xx,yy,col,lw=1){g.beginPath();g.moveTo(x,y);g.lineTo(xx,yy);g.lineWidth=lw;g.strokeStyle=col;g.stroke();}
 function gradient(x,y,xx,yy,stops){const grd=g.createLinearGradient(x,y,xx,yy);stops.forEach(([t,v])=>grd.addColorStop(t,v));return grd;}
 function letters(s,x,y,size,col,face='SlinkSans',align='left'){g.fillStyle=col;g.font=size+'px '+face;g.textAlign=align;g.textBaseline='middle';g.fillText(s,x,y);}
 const mono=(s,x,y,size,col,align)=>letters(s,x,y,size,col,'Menlo,monospace',align);
 const noteAt=i=>gameNotes[(i+rotation)%16];
 function chamfer(x,y,w,h,fill,edge,clip=5){
   path([[x,y],[x+w-clip,y],[x+w,y+clip],[x+w,y+h],[x+clip,y+h],[x,y+h-clip]],edge);
   path([[x+1,y+1],[x+w-clip-.2,y+1],[x+w-1,y+clip+.3],[x+w-1,y+h-1],[x+clip+.3,y+h-1],[x+1,y+h-clip-.3]],fill);
 }
 function stripe(x,y,w,h,colors){
   return gradient(x,y,x+w*.09,y+h,colors.map(([t,c])=>[t,c]));
 }
 const silver=[[0,'#faf9ff'],[.05,'#c3cadf'],[.37,'#dbe0ec'],[.47,'#c9cdda'],[.50,'#9fa8bf'],[.58,'#b8bfd3'],[1,'#858fae']];
 const cobalt=[[0,'#9bd0ff'],[.07,'#639cff'],[.43,'#466bdf'],[.50,'#415ac6'],[1,'#273d9c']];
 const shell=[[0,'#69728b'],[.19,'#d3dcf2'],[.41,'#7892c0'],[.5,'#333950'],[.88,'#80739d'],[1,'#b4afd3']];
 const purple=[[0,'#302041'],[.28,'#251737'],[1,'#110d21']];
 function seedCode(x,y,w,h){
   // Four groups read the four hex digits, high bit first. All bars pass through
   // one oblique crease, where the foil changes from cobalt to polished silver.
   const groupGap=3,cell=(w-3*groupGap)/16;
   const topInk=gradient(x,y,x+w,y+h*.35,[[0,'#6274b1'],[.4,'#dbe7ff'],[.68,'#8782b8'],[1,'#a8bce8']]);
   const bottomInk=gradient(x,y+h,x+w,y+h*.5,[[0,'#78769c'],[.45,'#455891'],[.84,'#d0d6f4'],[1,'#707caa']]);
   for(let i=0;i<16;i++){
     const bit=(seed>>>(15-i))&1;
     const xx=x+i*cell+Math.floor(i/4)*groupGap,bar=cell*(bit ? .83 : .35);
     const fold=y+h*(.76-.49*i/15);
     path([[xx+2,y],[xx+2+bar,y],[xx+bar,fold],[xx,fold]],topInk);
     path([[xx,fold+.85],[xx+bar,fold+.85],[xx+bar+1,y+h],[xx+1,y+h]],bottomInk);
   }
 }
 function actionButton(x,y,w,h,label,main){
   const fill=main?stripe(x,y,w,h,cobalt):stripe(x,y,w,h,purple);
   chamfer(x,y,w,h,fill,main?'#9cb6fc':'#7b7299',4);
   letters(label,x+w/2,y+h/2+.7,15,'#faf9ff','SlinkSans','center');
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
 // A sliver of cold foil crosses the margins of the selected step as a single cut.
 // Draw its margins only, so pitch names and the three articulation marks sit in clear ink.
 function printPlayhead(x,y,w,h,row){
   g.save();g.translate(x,y);
   const foil=gradient(0,0,w,h,[[0,'#d5e8ff'],[.18,'#658ef9'],[.48,'#2c4fa6'],[.73,'#5632a5'],[1,'#b195f1']]);
   const edge=w-2, drift=[.2,2.5,5.1,5.8,6.4][row];
   path([[edge-drift-3,1],[edge,1],[edge,h-1],[edge-drift,h-1]],foil);
   line(1.4,1,1.4,h-1,'#8498fa',.75);
   if(row===0){
     path([[2,2],[10,2],[2,10]],'#eef6ff');
     line(2,12,12,2,'#849dfa',.85);
   }else if(row===4){
     path([[edge-1,h-8],[edge-1,h-2],[edge-8,h-2]],'#dfdbff');
   }
   g.restore();
 }
 function matrix(makeKeys){
   const gutter=5, label=58,x=7,y=103,w=766,h=142,cw=(w-label-gutter*3)/16;
   const hh=[22,41,27,26,26];let yy=y;
   for(let r=0;r<5;r++){
     const rh=hh[r],ink=r===1?'#151629':'#e9eaf6';
     const lab=r===1?stripe(x,yy,label,rh,silver):stripe(x,yy,label,rh,[[0,'#332840'],[.55,'#251c34'],[1,'#19132a']]);
     chamfer(x,yy,label-3,rh-1,lab,'#71738c',3);letters(names[r],x+6,yy+rh/2+.3,r===1?16:13,ink);
     for(let i=0;i<16;i++){
       const bank=Math.floor(i/4),xx=x+label+i*cw+bank*gutter, ww=cw-1.1,sel=i===step;
       if(i%4===0&&i>0){line(xx-3.5,yy+2,xx-1,yy+.4,'#76778f',.7);}
       const bg=sel?gradient(xx,yy,xx+ww,yy+rh,[[0,'#556cca'],[.34,'#3659c4'],[.53,'#303f9d'],[.55,'#292f81'],[1,'#43287c']]):r===1?stripe(xx,yy,ww,rh,silver):
          gradient(xx,yy,xx,yy+rh,[[0,bank%2?'#30213f':'#262139'],[1,bank%2?'#1a112c':'#141224']]);
       rect(xx,yy,ww,rh-1,bg);line(xx+.5,yy+.6,xx+ww-.5,yy+.6,sel?'#a4bfff':r===1?'#fcf9ff':'#52465f',.75);
       if(r===1&&!sel){line(xx+.7,yy+2,xx+.7,yy+rh-3,'#eff0fa',.65);line(xx+ww-1,yy+3,xx+ww-1,yy+rh-2,'#72778e',.8);}
       if(!sel&&r===0&&i%4===0)rect(xx+2,yy+2,7,1,'#aba9c0');
       if(sel)printPlayhead(xx,yy,ww,rh-1,r);
       const cx=xx+ww/2,cy=yy+rh/2,fg=sel?'#fff':r===1?'#13172c':'#d6d9ea';
       if(r===0)mono(two(i+1),cx,cy+.4,13,fg,'center');
       else if(r===1)letters(noteAt(i),cx,cy+.6,noteAt(i).length>2?19:25,fg,'SlinkPitch','center');
       else symbol(r,i,cx,cy,fg,sel);
       if(makeKeys)key(r+'-'+i,xx,yy,ww,rh,(r<=1?'Select step '+(i+1):speech[r]+' on step '+(i+1)),()=>hit(r,i));
     }yy+=rh;
   }
 }
 function miniPitch(x,y,w,h){for(let i=0;i<16;i++){
   const lv=(semis[(i+rotation)%16]+5)/22, yy=y+2+(1-lv)*(h-4),u=x+i*w/16;
   rect(u,yy,w/16-1.2,1.3,i===step?'#cabaff':'#7c8cc6');if(seq[2][i]&&i<15){
     const n=(semis[(i+1+rotation)%16]+5)/22;line(u+w/16-1,yy,x+(i+1)*w/16,y+2+(1-n)*(h-4),'#7689c0',.7);}
 }}
 function bedDraw(first=false){
   g=staticCtx;g.setTransform(2,0,0,2,0,0);
   rect(0,0,780,285,'#080812');
   const edge=gradient(0,0,780,90,shell);
   chamfer(6,5,173,92,'#10101b',edge,8);drawWordmark();
   line(12,48,173,48,'#7c7691',.8);line(12,50,173,50,'#292735',.75);
   mono('SEED',13,64,10,'#abadc4');mono(seed.toString(16).toUpperCase().padStart(4,'0'),12,83,17,'#edeef7');seedCode(72,59,98,26);
   chamfer(185,5,334,92,'#05050d',edge,8);
   chamfer(525,5,188,92,stripe(525,5,188,92,purple),edge,8);
   drawStepGlass(g);
   mono('STEP',534,16,10,'#d2d4ea');mono('/16',615,83,11,'#c1bfdc','right');
   miniPitch(625,17,77,23);actionButton(622,55,81,29,playing?'Stop':'Play',true);
   actionButton(720,63,53,26,'Regen',false);
   matrix(first);
   chamfer(7,253,185,25,'#201730','#7b758e',4);mono('C / CHROMATIC',15,266,11,'#ddd6eb');
   for(let i=0;i<17;i++)line(129+i*3.1,271,136+i*3.1,260,'#77678c',.9);
   chamfer(200,249,343,32,'#111025',edge,6);
   // The slash is the edge of the storm window and the first rule of the score.
   line(456.5,250,444.5,280,'#8798ce',.8);
   line(459,250,447,280,'#533c87',.65);
   for(let bank=0;bank<4;bank++)rect(460+bank*19,276,bank===Math.floor(step/4)?15:4,1,bank===Math.floor(step/4)?'#abbaff':'#525174');
   miniPitch(461,252,75,21);
   line(552,265,771,265,'#454359',.8);
   for(let i=0;i<16;i++){const x=558+13.6*i,hi=i===step;rect(x,hi?258:262,hi?6:(i%4===0?2:1),hi?14:(i%4===0?9:6),hi?'#8facff':i%4===0?'#cacbdd':'#938ca9');}
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
 const drawStorm=makeWhipStorm(out);
 function paintStorm(at){drawStorm(at,seq[0],seq[1],seq[2],at-lastAccentTime);}
 const paintCounter=makeStepCounter();
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
   paintStorm(time);
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
 key('transport',622,55,81,29,'Play sequence preview',transport);key('regen',719,61,55,30,'Regenerate pattern',regen);
 const mobileActions={prev:()=>hit(0,step-1),next:()=>hit(0,step+1),transport,gate:()=>hit(2,step),accent:()=>hit(3,step),slide:()=>hit(4,step),regen,motion:toggleMotion};
 root.querySelector('.compact').addEventListener('click',e=>mobileActions[e.target.dataset.do]?.());
 keys.addEventListener('keydown',e=>{const m=e.target.dataset.id?.match(/^(\d+)-(\d+)$/);const d={ArrowUp:[-1,0],ArrowDown:[1,0],ArrowLeft:[0,-1],ArrowRight:[0,1]}[e.key];if(!m||!d)return;e.preventDefault();bounds.get(clamp(+m[1]+d[0],0,4)+'-'+((+m[2]+d[1]+16)%16)).focus();});
 function suspend(){lastTime=null;if(frameId)cancelAnimationFrame(frameId);frameId=0;if(playing)transport();fallback.pause();}
 new IntersectionObserver(([e])=>{visible=e.isIntersecting;if(visible){needsPaint=true;schedule();}else suspend();}).observe(root);
 document.addEventListener('visibilitychange',()=>{if(document.hidden)suspend();else{needsPaint=true;schedule();}});
 motionPref.addEventListener('change',()=>{options.motion=!motionPref.matches;lastTime=null;update();});
 fallback.addEventListener('loadeddata',()=>{needsPaint=true;schedule();});fallback.addEventListener('seeked',()=>{needsPaint=true;schedule();});
 syncUI();paint();schedule();root.dataset.ready='true';
 if(globalThis.Tweak){const tweak=new Tweak({container:panel,onChange:()=>{lastTime=null;update();}});
   tweak.addToggle(options,'motion',{label:'Panel motion'});tweak.addSlider(options,'speed',{label:'Camera speed',min:.5,max:1.5,step:.25,unit:'×'});}
})().catch(e=>{document.getElementById('thorn-crown-metal').dataset.error=String(e);console.error(e);});
