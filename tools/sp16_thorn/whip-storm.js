// Two flexible metal ribbons crossing a bank of forked lightning.
function makeWhipStorm(ctx){
  const width=167,height=24,tau=2*Math.PI;
  // The right cut meets the pitch trace in the same aperture.
  const aperture=new Path2D();
  aperture.moveTo(201,250);aperture.lineTo(456,250);aperture.lineTo(444,280);
  aperture.lineTo(206,280);aperture.lineTo(201,275);aperture.closePath();
  const cosine=Math.cos,sine=Math.sin,clip=(n,lo,hi)=>Math.min(hi,Math.max(lo,n));
  const pack=(r,g,b)=>(255<<24)|(clip(b,0,255)<<16)|(clip(g,0,255)<<8)|clip(r,0,255);
  const gradient=[
    [-1,58,42,101],[-.87,127,46,223],[-.77,40,17,89],[-.60,42,49,115],[-.56,62,91,204],[-.51,41,45,87],
    [-.43,119,105,158],[-.39,217,206,243],[-.32,113,102,146],[-.15,64,65,90],
    [.10,24,27,54],[.24,69,85,121],[.36,143,169,202],[.48,235,245,255],
    [.56,239,245,255],[.61,144,167,206],[.76,77,87,132],[1,151,164,197]
  ];
  // One small studio reflection map per finish; both flanks see the same fixed light cards.
  const lutSize=128,silver=0,blue=1,polish=2,inlay=3,bore=4;
  const finishes=Array.from({length:5},()=>new Uint32Array(lutSize*lutSize));
  for(let y=0;y<lutSize;y++)for(let x=0;x<lutSize;x++){
    const nx=x*2/(lutSize-1)-1,ny=y*2/(lutSize-1)-1,nz=Math.sqrt(Math.max(0,1-nx*nx-ny*ny));
    const rx=2*nx*nz,ry=2*ny*nz,rz=2*nz*nz-1;
    const level=clip(ry+.17*rx+.07*rz,-1,1);
    let j=1;while(j<gradient.length-1&&gradient[j][0]<level)j++;
    const a=gradient[j-1],b=gradient[j],t=(level-a[0])/(b[0]-a[0]);
    const card=Math.exp(-((rx+.74+.20*ry)**2)/.014)*(.26+.74*Math.max(0,rz));
    const cyan=Math.exp(-((rx-.75+.12*ry)**2)/.046)*(.48+.52*Math.max(0,rz));
    const r=a[1]+(b[1]-a[1])*t+82*card-10*cyan;
    const g=a[2]+(b[2]-a[2])*t+90*card+15*cyan;
    const bl=a[3]+(b[3]-a[3])*t+87*card+73*cyan;
    const i=y*lutSize+x;
    finishes[silver][i]=pack(r,g,bl);
    finishes[blue][i]=pack(r*.62+4,g*.78+9,bl*.99+18);
    finishes[polish][i]=pack(r*1.11+9,g*1.08+10,bl*1.04+10);
    finishes[inlay][i]=pack(r*.68+42,g*.47+17,bl*.79+60);
    finishes[bore][i]=pack(r*.51+3,g*.54+4,bl*.60+12);
  }

  const metal=document.createElement('canvas'),metalCtx=metal.getContext('2d');
  const scene=document.createElement('canvas'),ink=scene.getContext('2d');
  let scale=0,W,H,pixels,colour,depth;
  const stations=93;
  const projected=new Float32Array(stations*2*5);
  function triangle(ia,ib,ic,map,charge){
    const v=projected;
    const ax=v[ia],ay=v[ia+1],bx=v[ib],by=v[ib+1],cx=v[ic],cy=v[ic+1];
    const abx=bx-ax,aby=by-ay,acx=cx-ax,acy=cy-ay,det=abx*acy-aby*acx;
    if(det>=-.0001)return;
    const xmin=Math.max(0,Math.ceil(Math.min(ax,bx,cx)-.5)),xmax=Math.min(W-1,Math.floor(Math.max(ax,bx,cx)-.5));
    const ymin=Math.max(0,Math.ceil(Math.min(ay,by,cy)-.5)),ymax=Math.min(H-1,Math.floor(Math.max(ay,by,cy)-.5));
    const dbx=acy/det,dby=-acx/det,dcx=-aby/det,dcy=abx/det;
    const za=v[ia+2],zb=v[ib+2]-za,zc=v[ic+2]-za;
    const xa=v[ia+3],xb=v[ib+3]-xa,xc=v[ic+3]-xa;
    const ya=v[ia+4],yb=v[ib+4]-ya,yc=v[ic+4]-ya;
    for(let y=ymin;y<=ymax;y++){
      let b=(xmin+.5-ax)*dbx+(y+.5-ay)*dby,c=(xmin+.5-ax)*dcx+(y+.5-ay)*dcy;
      for(let x=xmin,i=y*W+xmin;x<=xmax;x++,i++,b+=dbx,c+=dcx){
        if(b<0||c<0||b+c>1)continue;
        const z=za+zb*b+zc*c;if(z<=depth[i])continue;
        depth[i]=z;
        const sample=map[((ya+yb*b+yc*c+.5)|0)*lutSize+((xa+xb*b+xc*c+.5)|0)];
        colour[i]=charge ? pack((sample&255)+charge,((sample>>>8)&255)+charge*.92,
                                ((sample>>>16)&255)+charge*1.31) : sample;
      }
    }
  }
  const ribbons=[
    {points:[[-13,19],[10,16],[31,5],[52,7],[69,17],[84,18],[94,12],[89,6],[82,5],[100,4],[133,9]],width:1,hand:1},
    {points:[[179,4],[155,6],[140,16],[124,19],[109,15],[107,9],[116,6],[111,3.4],[92,6.5],[64,17],[45,13]],width:.77,hand:-1}
  ];
  const cross=[[-1,0,polish],[-.90,.11,blue],[-.52,.24,silver],[-.30,.27,bore],[-.24,.29,silver],
    [.06,.44,silver],[.48,.27,blue],[.84,.14,polish],[1,0,polish],[.77,-.12,blue],[.04,-.24,inlay],[-.72,-.11,blue]];
  const lanes=cross.length;
  function cat(a,b,c,d,u){
    return b+.5*u*((c-a)+u*(2*a-5*b+4*c-d+u*(3*b-a-3*c+d)));
  }
  // Bake the resting curves. A travelling bend reaches the thin end late, briefly folding it back.
  for(const r of ribbons){
    r.rest=[];r.spine=new Float32Array(stations*4);r.mesh=new Float32Array(stations*lanes*3);
    const path=r.points,n=path.length-1;
    for(let j=0;j<stations;j++){
      const t=j/(stations-1)*n,k=Math.min(n-1,Math.floor(t)),u=t-k;
      r.rest.push([0,1].map(i=>cat(path[Math.max(0,k-1)][i],path[k][i],path[k+1][i],path[Math.min(n,k+2)][i],u)));
    }
  }
  function renderLash(r,phase,gates,accents,slides,pulseAge){
    const pulseStation=(pulseAge/.48*1.3-.15)*(stations-1);
    const spine=r.spine,mesh=r.mesh,hand=r.hand,offset=hand===1?0:2.51;
    for(let j=0;j<stations;j++){
      const u=j/(stations-1),k=j*4,p=phase+offset-3.9*u;
      const surge=(.5+.5*cosine(p))**4;
      const index=(r.rest[j][0]/width*16),lo=clip(Math.floor(index),0,15),hi=Math.min(15,lo+1),f=clip(index-lo,0,1);
      const mix=row=>row[lo]*(1-f)+row[hi]*f;
      const gate=mix(gates),acc=mix(accents),slide=mix(slides);
      spine[k]=r.rest[j][0]+hand*u*u*(29*(surge-.40)+9*sine(p*2+.44)) + hand*slide*u*2;
      spine[k+1]=r.rest[j][1]+(1.4+6.2*u)*sine(p*2+.8*u)*(.72+.28*gate)+2.7*(surge-.5);
      spine[k+2]=(hand===1?1.6:-.9)+3*sine(5.6*u+phase+offset)+slide;
      spine[k+3]=.30+1.03*sine(phase*2-7.6*u+offset)+.53*sine(phase-12*u+.4)+.26*acc;
    }
    for(let j=0;j<stations;j++){
      const u=j/(stations-1),k=j*4,p=Math.max(0,j-1)*4,q=Math.min(stations-1,j+1)*4;
      const dx=spine[q]-spine[p],dy=spine[q+1]-spine[p+1],inv=1/(Math.hypot(dx,dy)||1);
      const nx=-dy*inv,ny=dx*inv,cs=cosine(spine[k+3]),sn=sine(spine[k+3]);
      const taper=(.045+3.35*(1-u)**.77+1.65*sine(Math.PI*u)**2)*r.width;
      // Swept quills grow out of one edge of the ribbon. Their back edges taper to a single point.
      const quill=(center,span)=>{const t=(u-center)/span;return t<0?Math.max(0,1+t*5):Math.max(0,1-t)**2;};
      const hook=quill(.34,.17)+.80*quill(.55,.13)+.52*quill(.75,.11);
      for(let l=0;l<lanes;l++){
        const [s,h]=cross[l],outer=Math.max(0,-s)**2,reach=taper*s-outer*hook*4.65;
        const ridge=taper*h,spread=reach*cs-ridge*sn,sweep=outer*hook*13.3;
        const at=(j*lanes+l)*3;
        mesh[at]=spine[k]+nx*spread-dx*inv*sweep;
        mesh[at+1]=spine[k+1]+ny*spread-dy*inv*sweep;
        mesh[at+2]=spine[k+2]+reach*sn+ridge*cs;
      }
    }
    for(let l=0;l<lanes;l++){
      const next=(l+1)%lanes;
      for(let j=0;j<stations;j++){
        const k=(j*lanes+l)*3,kb=(j*lanes+next)*3,bx=mesh[kb]-mesh[k],by=mesh[kb+1]-mesh[k+1],bz=mesh[kb+2]-mesh[k+2];
        for(let side=0;side<2;side++){
          const lane=side?next:l,at=(j*lanes+lane)*3,pr=(Math.max(0,j-1)*lanes+lane)*3,ne=(Math.min(stations-1,j+1)*lanes+lane)*3;
          const tx=mesh[ne]-mesh[pr],ty=mesh[ne+1]-mesh[pr+1],tz=mesh[ne+2]-mesh[pr+2];
          let ux=bx,uy=by,uz=bz;
          if(lane!==0&&lane!==5&&lane!==8&&lane!==10){
            const prev=(j*lanes+(lane+lanes-1)%lanes)*3,following=(j*lanes+(lane+1)%lanes)*3;
            const ax=mesh[at]-mesh[prev],ay=mesh[at+1]-mesh[prev+1],az=mesh[at+2]-mesh[prev+2];
            const cx=mesh[following]-mesh[at],cy=mesh[following+1]-mesh[at+1],cz=mesh[following+2]-mesh[at+2];
            const f=1/Math.sqrt(ax*ax+ay*ay+az*az+1e-9),g=1/Math.sqrt(cx*cx+cy*cy+cz*cz+1e-9);
            ux=ax*f+cx*g;uy=ay*f+cy*g;uz=az*f+cz*g;
          }
          const normalX=ty*uz-tz*uy,normalY=tz*ux-tx*uz,normalZ=tx*uy-ty*ux,half=63.5;
          const inv=half/(Math.hypot(normalX,normalY,normalZ)||1),v=(j*2+side)*5;
          projected[v]=mesh[at]*scale;projected[v+1]=mesh[at+1]*scale;projected[v+2]=mesh[at+2];
          projected[v+3]=half+normalX*inv;projected[v+4]=half-normalY*inv;
        }
      }
      const material=cross[l][2], base=finishes[material];
      for(let j=0;j<stations-1;j++){
        const i=j*10;
        // A slit milled into each belly reveals the other lash or a lightning fork beneath.
        if((l===2||l===3||l===10)&&j>25&&j<39)continue;
        // Each silver bevel and the violet reverse can catch the same travelling
        // light. The depth buffer still cuts it off where the whips cross.
        let charge=0;
        if((l===2||l===4||l===5||material===polish||material===inlay) && pulseAge>=0 && pulseAge<.48){
          const distance=(j-pulseStation)/11;
          charge=155*Math.max(0,1-distance*distance)**2;
        }
        triangle(i,i+15,i+10,base,charge);triangle(i,i+5,i+15,base,charge);
      }
    }
  }

  // Each strike grows along a different, repeatable fracture. Side channels inherit a parent
  // junction, and the bright return stroke retraces the same path after the leader passes.
  function strike(seed){
    let state=seed;const noise=()=>{state=(Math.imul(1664525,state)+1013904223)>>>0;return state/4294967296;};
    const reverse=noise()>.5,entry=-5+noise()*16,exit=15+noise()*13;
    // Subdivide the channel recursively: broad changes of direction break into finer,
    // asymmetric fractures. Smaller branches reuse actual nodes in the parent path.
    function crack(x,y,ex,ey,levels,rough){
      const pts=[[x,y]];
      function split(ax,ay,bx,by,n,spread){
        if(!n){pts.push([bx,by]);return;}
        const dx=bx-ax,dy=by-ay,len=Math.hypot(dx,dy)||1;
        const t=.32+.35*noise(),bend=(noise()*2-1)*spread;
        const mx=ax+dx*t-dy/len*bend,my=clip(ay+dy*t+dx/len*bend,-5,29);
        split(ax,ay,mx,my,n-1,spread*(.45+.18*noise()));
        split(mx,my,bx,by,n-1,spread*(.47+.16*noise()));
      }
      split(x,y,ex,ey,levels,rough);return pts;
    }
    function branch(pts,join,weight){
      const full=new Path2D();full.moveTo(...pts[0]);
      for(let i=1;i<pts.length;i++)full.lineTo(...pts[i]);
      return {pts,join,weight,full};
    }
    const dir=reverse?-1:1;
    const main=crack(reverse?176:-9,entry,reverse?-9:176,exit,7,17),arcs=[branch(main,0,1)];
    for(let j=0;j<8;j++){
      const i=8+Math.floor(j*14+noise()*9),[x,y]=main[i],ex=x+dir*(12+noise()*35),ey=y+(noise()>.48?1:-1)*(8+noise()*21);
      const fork=crack(x,y,ex,ey,5,6.3);arcs.push(branch(fork,i/128,.74-.13*noise()));
      for(const k of [9,19]){
        const [fx,fy]=fork[k],twig=crack(fx,fy,fx+dir*(4+10*noise()),fy+(k===9?-1:1)*(2+10*noise()),3,2.4);
        arcs.push(branch(twig,i/128+k/160,.31));
      }
    }
    const cloudX=35+noise()*96,groundX=cloudX+dir*(39+14*noise());
    const down=crack(cloudX,-4,groundX,27,6,8.7);
    arcs.push(branch(down,.28,.66));
    for(const k of [15,33,42]){
      const [fx,fy]=down[k];arcs.push(branch(crack(fx,fy,fx+dir*19,fy+13,4,4.2),.28+k/180,.37));
    }
    return arcs;
  }
  const storms=Array.from({length:6},(_,i)=>strike(1222+i*33461));
  function drawStorm(arcs,age,accent,front){
    if(age<0||age>2)return;
    const leader=clip(age/.37,0,1),fade=Math.exp(-1.95*Math.max(0,age-.33));
    const returnStroke=Math.exp(-((age-.49)**2)/.008)+.6*Math.exp(-((age-.83)**2)/.015);
    const energy=clip(age/.12,0,1)*clip((2-age)/.36,0,1)*fade*(.80+.37*returnStroke+.12*accent);
    if(energy<.008)return;
    ink.save();ink.lineJoin='round';ink.lineCap='round';ink.globalCompositeOperation='lighter';
    for(let j=0;j<arcs.length;j++){
      const arc=arcs[j],v=arc.pts;
      if(front&&j>2)continue;
      const extent=(j?clip((leader-arc.join)*3,0,1):leader)*(v.length-1);
      if(extent<=0)continue;
      let path=arc.full;
      if(extent<v.length-1){
        const last=Math.floor(extent),f=extent-last;path=new Path2D();path.moveTo(...v[0]);
        for(let k=1;k<=last;k++)path.lineTo(...v[k]);
        path.lineTo(v[last][0]+f*(v[last+1][0]-v[last][0]),v[last][1]+f*(v[last+1][1]-v[last][1]));
      }
      const weight=arc.weight,power=energy*weight;
      if(front){
        ink.globalAlpha=clip(power*(.12+.55*returnStroke),0,1);ink.lineWidth=.35*weight+.12;ink.strokeStyle='#c7d6ff';ink.stroke(path);
      }else{
        ink.globalAlpha=.12*power;ink.lineWidth=4.2*weight;ink.strokeStyle='#5525e5';ink.stroke(path);
        ink.globalAlpha=.40*power;ink.lineWidth=1.65*weight;ink.strokeStyle='#5854ff';ink.stroke(path);
        ink.globalAlpha=.92*power;ink.lineWidth=.63*weight+.11;ink.strokeStyle=j%3?'#9d8dff':'#88baff';ink.stroke(path);
        ink.globalAlpha=power;ink.lineWidth=.26*weight+.12;ink.strokeStyle='#eff6ff';ink.stroke(path);
      }
    }
    if(front&&age<1.05){
      // A white return pulse runs back along the broken channel. Its violet afterimage lingers.
      const pts=arcs[0].pts,idx=clip((age-.30)/.63,0,1)*(pts.length-1);
      const pulse=sine(Math.PI*clip((age-.28)/.71,0,1))**2;
      for(let k=Math.max(1,Math.floor(idx)-5);k<=Math.min(pts.length-1,Math.ceil(idx)+4);k++){
        const heat=pulse*Math.exp(-.10*(k-idx)**2);
        ink.globalAlpha=heat*.86;ink.strokeStyle='#e7eaff';ink.lineWidth=.92;
        ink.beginPath();ink.moveTo(...pts[k-1]);ink.lineTo(...pts[k]);ink.stroke();
      }
    }
    ink.restore();
  }
  // A two-ink checker wraps over a turning belt. The cross-section goes edge-on
  // twice per turn, pinching a broad printed tile down to a single engraved line.
  const columns=36, rows=4, stride=rows+1;
  const printX=new Float32Array((columns+1)*stride);
  const printY=new Float32Array(printX.length);
  function printedField(phase){
    ink.fillStyle='#070617';ink.fillRect(0,0,width,height);
    for(let i=0;i<=columns;i++){
      const u=i/columns;
      const twist=tau*(1.16*u-.13)+.69*sine(phase-.8)+.19*sine(phase*2+tau*u);
      const centre=14+5.6*sine(tau*(1.5*u-.09)+phase);
      const radius=6+2.5*cosine(tau*u-phase);
      const x=-11+(width+22)*u+2.4*sine(twist*2+phase);
      for(let j=0;j<=rows;j++){
        const v=j*2/rows-1, at=i*stride+j;
        printX[at]=x+v*(3.7+1.9*sine(twist));
        printY[at]=centre+radius*v*cosine(twist)+1.7*(1-v*v)*sine(twist);
      }
    }
    for(let colour=0;colour<2;colour++){
      ink.beginPath();
      for(let i=0;i<columns;i++)for(let j=0;j<rows;j++){
        if((i+j)%2!==colour)continue;
        const a=i*stride+j,b=a+stride;
        ink.moveTo(printX[a],printY[a]);ink.lineTo(printX[b],printY[b]);
        ink.lineTo(printX[b+1],printY[b+1]);ink.lineTo(printX[a+1],printY[a+1]);ink.closePath();
      }
      ink.fillStyle=colour?'#49328a':'#17112d';ink.fill();
    }
    // A misregistered pair of lines detaches at the folds like ink on foil.
    for(const side of [0,rows]){
      ink.beginPath();
      for(let i=0;i<=columns;i++){
        const k=i*stride+side,y=printY[k]+(side?1:-1)*1.3;
        if(i===0)ink.moveTo(printX[k],y);else ink.lineTo(printX[k],y);
      }
      ink.lineWidth=.37;ink.strokeStyle=side?'#7272c0':'#6f429d';ink.stroke();
    }
  }
  function tipLight(r,phase,power){
    const s=r.spine,j=r.hand===1?stations-3:stations-7;
    ink.save();ink.globalCompositeOperation='lighter';
    // Shift two hairline contours off the narrowing tip, like a violet screen-print misregistration.
    for(let pass=1;pass<=2;pass++){
      ink.beginPath();
      for(let k=61;k<stations;k++){
        const u=(k-61)/(stations-62),d=pass*sine(Math.PI*u)*(.65+.6*power);
        const x=s[k*4]+d*2.1,y=s[k*4+1]+d*r.hand;
        if(k===61)ink.moveTo(x,y);else ink.lineTo(x,y);
      }
      ink.globalAlpha=(.19+.22*power)/pass;ink.strokeStyle=pass===1?'#8758ff':'#4681ff';
      ink.lineWidth=.27;ink.stroke();
    }
    const x=s[j*4],y=s[j*4+1],shine=power**2;
    const size=.25+2.4*shine;
    ink.translate(x,y);ink.rotate(-.36+.13*sine(phase));ink.globalAlpha=shine;
    const halo=ink.createRadialGradient(0,0,0,0,0,3.7);
    halo.addColorStop(0,'rgba(185,205,255,.85)');halo.addColorStop(.24,'rgba(95,111,251,.62)');halo.addColorStop(1,'rgba(91,49,245,0)');
    ink.fillStyle=halo;ink.fillRect(-3.7,-3.7,7.4,7.4);
    ink.beginPath();ink.moveTo(-2.0*size,0);ink.lineTo(-.32,.24);ink.lineTo(0,size);
    ink.lineTo(.24,.20);ink.lineTo(2.0*size,0);ink.lineTo(.24,-.2);ink.lineTo(0,-size);ink.lineTo(-.30,-.2);ink.closePath();
    ink.fillStyle='#e3ebff';ink.fill();ink.restore();
  }
  return function render(seconds,gates,accents,slides,pulseAge=Infinity){
    const res=clip(Math.ceil(ctx.getTransform().a*1.5),3,8);
    if(scale!==res){
      scale=res;W=metal.width=scene.width=width*res;H=metal.height=scene.height=height*res;
      pixels=metalCtx.createImageData(W,H);colour=new Uint32Array(pixels.data.buffer);depth=new Float32Array(W*H);
    }
    const time=((seconds%8)+8)%8,phase=tau*time/8;
    ink.setTransform(scale,0,0,scale,0,0);printedField(phase);
    const beat=time*6/8+.40,index=Math.floor(beat),age=beat-index;
    const active=storms[index%6],previous=storms[(index+5)%6],acc=accents[(index%6)*3],priorAcc=accents[((index+5)%6)*3];
    drawStorm(previous,age+1,priorAcc,false);drawStorm(active,age,acc,false);
    colour.fill(0);depth.fill(-1e5);
    for(const r of ribbons)renderLash(r,phase,gates,accents,slides,pulseAge);
    metalCtx.putImageData(pixels,0,0);ink.setTransform(1,0,0,1,0,0);ink.drawImage(metal,0,0);
    ink.setTransform(scale,0,0,scale,0,0);drawStorm(active,age,acc,true);
    tipLight(ribbons[0],phase,(.5+.5*cosine(phase*3-.6))**3);
    tipLight(ribbons[1],phase,(.5+.5*cosine(phase*3+2.2))**4);
    ctx.save();ctx.clip(aperture);
    ctx.drawImage(scene,201,250,255,30);
    ctx.restore();
  };
}
