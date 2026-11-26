// The glass spans the counter and transport. Its curved grid flattens into the pitch display.
function drawStepGlass(g){
  g.save();
  g.beginPath();
  g.moveTo(527,7);g.lineTo(704,7);g.lineTo(711,14);
  g.lineTo(711,95);g.lineTo(534,95);g.lineTo(527,88);g.closePath();g.clip();
  const night=g.createLinearGradient(530,85,691,6);
  night.addColorStop(0,'#080b24');night.addColorStop(.41,'#17132b');night.addColorStop(1,'#20162e');
  g.fillStyle=night;g.fillRect(525,5,188,92);
  const glass=g.createRadialGradient(554,75,5,554,75,78);
  glass.addColorStop(0,'rgba(51,64,156,.26)');glass.addColorStop(.72,'rgba(37,28,91,.11)');glass.addColorStop(1,'rgba(26,18,56,0)');
  g.fillStyle=glass;g.fillRect(526,6,138,90);
  const wire=g.createLinearGradient(525,6,711,66);
  wire.addColorStop(0,'#40355e');wire.addColorStop(.36,'#38305c');wire.addColorStop(.67,'#30263f');wire.addColorStop(1,'#392a48');
  g.strokeStyle=wire;g.lineWidth=.56;g.beginPath();
  for(let x=498;x<724;x+=15){
    const bow=Math.exp(-(((x-547)/49)**2));
    g.moveTo(x-13*bow,6);g.bezierCurveTo(x+9*bow,31,x+24*bow,63,x+17*bow,96);
  }
  for(let y=-8;y<110;y+=15){
    g.moveTo(525,y-4);g.bezierCurveTo(550,y-5,568,y+5,593,y+4);
    g.bezierCurveTo(622,y+4,639,y,661,y);g.lineTo(713,y);
  }
  g.stroke();g.restore();
}

function makeStepCounter(font='66px SlinkItalic'){
  const colours=['#afbeff','#958cf5','#985fdf','#7043a7'];
  const metrics=document.createElement('canvas').getContext('2d');metrics.font=font;
  const digitOrigins=Array.from({length:10},(_,i)=>{
    const ink=metrics.measureText(String(i));
    return 14+(ink.actualBoundingBoxLeft-ink.actualBoundingBoxRight)*.5;
  });
  const tile=document.createElement('canvas');tile.width=182;tile.height=140;
  const g=tile.getContext('2d');g.setTransform(2,0,0,2,-1056,-40);
  let lastNumber=0,lastPulse=-1;
  function drawInk(number,pulse){
    const digits=String(number).padStart(2,'0');
    g.clearRect(528,20,91,70);g.save();
    g.font=font;g.textBaseline='alphabetic';g.textAlign='left';g.lineJoin='round';
    function pair(stroke){
      for(let i=0;i<2;i++){
        const digit=digits[i],x=i*29.5+digitOrigins[+digit];
        if(stroke)g.strokeText(digit,x,0);else g.fillText(digit,x,0);
      }
    }
    // Keep the reading plane fixed. Only the four outlines travel into the glass.
    for(let n=4;n>=1;n--){
      const d=n/4,reach=1+pulse*.34;
      g.save();g.translate(534+(19.5*d+1.75*d*d)*reach,84-(11.7*d+2.8*d*d)*reach);
      g.transform(1-.038*d,0,-.026*d,1-.045*d,0,0);
      g.fillStyle='rgba(12,11,38,.31)';pair(false);
      g.strokeStyle=colours[n-1];g.lineWidth=1.03-.09*d;pair(true);
      g.restore();
    }
    g.translate(534,84);
    g.shadowColor='rgba(120,139,255,.61)';g.shadowBlur=4.8;
    g.strokeStyle='#5d659e';g.lineWidth=1;pair(true);g.shadowBlur=0;
    const pearl=g.createLinearGradient(0,-47,9,0);
    pearl.addColorStop(0,'#ffffff');pearl.addColorStop(.59,'#f8f9ff');pearl.addColorStop(1,'#d9e2ff');
    g.fillStyle=pearl;pair(false);g.restore();
  }
  return function draw(target,number,beatAge){
    const u=Math.min(1,Math.max(0,beatAge/.36));
    const pulse=Math.sin(Math.PI*u)**2*(1-u);
    if(number!==lastNumber||pulse!==lastPulse){
      drawInk(number,pulse);lastNumber=number;lastPulse=pulse;
    }
    target.drawImage(tile,528,20,91,70);
  };
}
