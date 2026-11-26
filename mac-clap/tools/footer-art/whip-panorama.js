// Model space matches the 167 × 24 compact study. The heavy roots come in over
// the top or bottom edge; free tips overlap the neighbouring whips in one depth buffer.
function makeFooterPanorama(){
  const width=672;
  const ribbons=[
    {x:0,width:1,hand:1,phase:0,points:[
      [-48,34],[-28,25],[-13,19],[10,16],[31,5],[52,7],[69,17],
      [84,18],[94,12],[89,6],[82,5],[100,4],[133,9]]},
    {x:0,width:.77,hand:-1,phase:0,points:[
      [221,-12],[199,0],[179,4],[155,6],[140,16],[124,19],[109,15],
      [107,9],[116,6],[111,3.4],[92,6.5],[64,17],[45,13]]},
    {x:153,width:.90,hand:1,phase:1.45,points:[
      [-27,34],[-12,20],[7,10],[24,9],[47,17],[65,19],[82,13],
      [93,3],[102,1],[105,6],[97,10],[112,15],[137,13],[164,4],[182,6]]},
    {x:173,width:.74,hand:-1,phase:3.80,points:[
      [205,-12],[188,-1],[166,3],[145,13],[126,17],[103,13],
      [85,5],[70,6],[65,13],[74,17],[80,13],[63,10],[43,15],[23,20]]},
    {x:330,width:1.02,hand:1,phase:4.20,points:[
      [-36,35],[-22,21],[-5,17],[14,16],[31,7],[49,3],[68,10],
      [79,17],[76,21],[65,20],[57,14],[76,11],[101,13],[128,19],[151,13]]},
    {x:332,width:.70,hand:-1,phase:2.15,points:[
      [186,-12],[169,-1],[150,7],[136,18],[120,21],[102,18],
      [88,10],[84,4],[91,2],[99,7],[90,11],[72,12],[48,5],[26,8]]},
    {x:497,width:.90,hand:1,phase:2.78,points:[
      [-40,35],[-23,25],[-8,14],[13,6],[34,8],[50,17],[68,20],
      [84,14],[88,7],[79,5],[69,10],[84,15],[109,11],[135,3],[156,8]]},
    {x:507,width:.83,hand:-1,phase:5.35,points:[
      [201,-12],[184,-1],[167,5],[148,8],[130,15],[111,17],[98,10],
      [101,4],[112,4],[116,9],[108,15],[88,18],[66,14],[46,7],[29,10]]}
  ];
  const cells=[
    {x:-3,seed:1222,beat:.40,phase:0}, {x:165,seed:8039,beat:2.13,phase:1.73},
    {x:331,seed:27071,beat:4.69,phase:3.37}, {x:507,seed:49493,beat:1.91,phase:5.08}
  ];
  const fields=[10,56,133,205,281,351,429,500,589,648].map((x,i)=>({x,band:i%3}));

  // A mark crossing the horizontal film seam must also project through the
  // opposite edge in the same pose. Interior marks are drawn just once.
  function wrap(marks,extent){
    return marks.flatMap(mark=>{
      const [lo,hi]=extent(mark);
      return [0,-width,width].map(shift=>mark.x+shift)
        .filter(x=>x+hi>=0 && x+lo<width)
        .map(x=>({...mark,x}));
    });
  }
  return {
    width,
    ribbons:wrap(ribbons,r=>{
      const xs=r.points.map(p=>p[0]);
      // Include the oscillating tips, swept quills and glints at their full reach.
      return [Math.min(...xs)-58,Math.max(...xs)+58];
    }),
    cells:wrap(cells,()=>[-90,255]),
    fields:wrap(fields,()=>[-3,47])
  };
}
