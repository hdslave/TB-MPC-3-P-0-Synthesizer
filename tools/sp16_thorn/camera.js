// A continuous camera move through the front, wing and core of the relief.
const cameraBeats = [
  // Seconds, yaw, pitch, roll (degrees), target X/Y and magnification.
  [0,    0,   0,  0,   0,      0,      1.00],
  [2,   -8,   3, -1,  -0.05,   0.012,  1.04],
  [5,  -31,  12, -6,  -0.46,   0.050,  2.18],
  [8,  -18,  -7, -2,  -0.26,   0.032,  2.36],
  [11,   6,  -9,  3,   0.015,  0.018,  3.05],
  [14,  29,   7,  6,   0.48,   0.012,  2.05],
  [17,  17,  11,  2,   0.29,   0.025,  1.61],
  [20,   0,   0,  0,   0,      0,      1.00]
];
function cameraPose(seconds){
  const t=((seconds%20)+20)%20;
  let a=0;while(a<cameraBeats.length-2 && t>=cameraBeats[a+1][0]) a++;
  const b=a+1, p=cameraBeats[a], q=cameraBeats[b],dt=q[0]-p[0], u=(t-p[0])/dt;
  const prev=cameraBeats[a===0?cameraBeats.length-2:a-1],next=cameraBeats[b===cameraBeats.length-1?1:b+1];
  const prevTime=prev[0]-(a===0?20:0), nextTime=next[0]+(b===cameraBeats.length-1?20:0);
  return p.slice(1).map((v,k)=>{
    const j=k+1,m0=.80*dt*(q[j]-prev[j])/(q[0]-prevTime),m1=.80*dt*(next[j]-v)/(nextTime-p[0]);
    return (2*u*u*u-3*u*u+1)*v+(u*u*u-2*u*u+u)*m0+(-2*u*u*u+3*u*u)*q[j]+(u*u*u-u*u)*m1;
  });
}
function buildCrestView(canvas,photo,depth){
  const gl=canvas.getContext('webgl',{alpha:false,antialias:true,depth:true,preserveDrawingBuffer:true,powerPreference:'low-power'});
  if(!gl)return null;
  const vertex=`
    attribute vec3 position;attribute vec2 uv;attribute vec3 normal;
    uniform vec3 angles;uniform vec3 aim;uniform float magnify;uniform float aspect;uniform float lens;
    varying vec2 posUV;varying float sparkle;varying float rear;
    void main(){
      float sy=sin(angles.x),cy=cos(angles.x),sp=sin(angles.y),cp=cos(angles.y),sr=sin(angles.z),cr=cos(angles.z);
      mat3 yaw=mat3(cy,0.,sy, 0.,1.,0., -sy,0.,cy);
      mat3 pitch=mat3(1.,0.,0., 0.,cp,sp, 0.,-sp,cp);
      mat3 roll=mat3(cr,sr,0., -sr,cr,0., 0.,0.,1.);
      vec3 view=roll*pitch*yaw*(position-aim);
      float cameraDist=3.7/magnify;
      float w=cameraDist-view.z;
      gl_Position=vec4(view.x*lens,view.y*lens*aspect, 0.22*w-0.55,w);
      vec3 n=normalize(roll*pitch*yaw*normal);
      vec3 r=reflect(normalize(view-vec3(0,0,cameraDist)),n);
      float ribbon=pow(max(0.,1.-abs(dot(r,normalize(vec3(.16,.90,-.28)))-.32)*2.0),9.);
      float rim=pow(1.-clamp(abs(n.z),0.,1.),2.2);
      sparkle=(.86*ribbon+rim*.36)*min(1.,abs(angles.x)*3.7+abs(angles.y)*2.);
      rear=step(normal.z,0.);posUV=uv;
    }`;
  const fragment=`
    precision mediump float;uniform sampler2D plate;varying vec2 posUV;varying float sparkle;varying float rear;
    void main(){
      vec3 source=texture2D(plate,posUV).rgb;float peaks=max(max(source.r,source.g),source.b);
      if(peaks<.043)discard;
      vec3 base=source*vec3(.96,.976,1.02);
      vec3 highlight=mix(vec3(.31,.48,.97),vec3(.92,.94,1.),smoothstep(.12,.66,peaks));
      base=mix(base,vec3(.045,.05,.16)+base*vec3(.40,.43,.70),rear*.76);
      gl_FragColor=vec4(base+highlight*sparkle*(.23+.20*peaks),1.);
    }`;
  function shader(kind,source){
    const s=gl.createShader(kind);gl.shaderSource(s,source);gl.compileShader(s);
    if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;
  }
  const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vertex));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
  if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  const df=document.createElement('canvas');df.width=depth.width;df.height=depth.height;
  const dg=df.getContext('2d');dg.drawImage(depth,0,0);const samples=dg.getImageData(0,0,df.width,df.height).data;
  const nx=250,ny=84;
  const sample=(i,j,c)=>samples[4*(Math.round((df.width-1)*Math.max(0,Math.min(nx,i))/nx)+df.width*Math.round((df.height-1)*Math.max(0,Math.min(ny,j))/ny))+c];
  const z=(i,j,side)=>sample(i,j,side)/255*.92-.45;
  const stride=nx+1, count=stride*(ny+1),vertices=new Float32Array(count*2*8),idx=[];
  for(let side=0;side<2;side++)for(let j=0;j<=ny;j++)for(let i=0;i<=nx;i++){
    const dzdx=(z(i+1,j,side)-z(i-1,j,side))*nx/4,dzdy=(z(i,j-1,side)-z(i,j+1,side))*ny*.75;
    const sign=1-2*side,k=(side*count+j*stride+i)*8;
    vertices.set([2*i/nx-1,(1-2*j/ny)/3,z(i,j,side),i/nx,j/ny,-dzdx*sign,-dzdy*sign,sign],k);
  }
  for(let j=0;j<ny;j++)for(let i=0;i<nx;i++){
    if(!(sample(i,j,2)||sample(i+1,j,2)||sample(i,j+1,2)||sample(i+1,j+1,2)))continue;
    const a=j*stride+i,b=a+1,c=a+stride,d=c+1;
    idx.push(a,c,b,b,c,d,a+count,b+count,c+count,b+count,d+count,c+count);
  }
  const vb=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,vb);gl.bufferData(gl.ARRAY_BUFFER,vertices,gl.STATIC_DRAW);
  for(const [name,n,offset] of [['position',3,0],['uv',2,12],['normal',3,20]]){
    const loc=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(loc);gl.vertexAttribPointer(loc,n,gl.FLOAT,false,32,offset);
  }
  const ib=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,ib);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array(idx),gl.STATIC_DRAW);
  const tex=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,tex);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  gl.texImage2D(gl.TEXTURE_2D,0,gl.RGB,gl.RGB,gl.UNSIGNED_BYTE,photo);
  gl.enable(gl.DEPTH_TEST);gl.clearColor(.009,.009,.026,1);
  const loc=name=>gl.getUniformLocation(program,name);
  const angles=loc('angles'),aim=loc('aim'),zoom=loc('magnify'),lens=loc('lens');
  gl.uniform1f(loc('aspect'),canvas.width/canvas.height);gl.viewport(0,0,canvas.width,canvas.height);
  return seconds=>{
    const [yaw,pitch,roll,panX,panY,mag]=cameraPose(seconds);
    gl.uniform3f(angles,yaw*Math.PI/180,pitch*Math.PI/180,roll*Math.PI/180);gl.uniform3f(aim,panX,panY,.045);gl.uniform1f(zoom,mag);
    // Push in on the wide pass until the wingtips cross the aperture. Ease the
    // extra framing away as the camera reaches its existing macro positions.
    const u=Math.max(0,Math.min(1,(mag-1)/1.03));
    gl.uniform1f(lens,3.06*(1.11+.33*(1-u*u*(3-2*u))));
    gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.drawElements(gl.TRIANGLES,idx.length,gl.UNSIGNED_SHORT,0);
  };
}
