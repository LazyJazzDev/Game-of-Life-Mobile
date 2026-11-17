const fs=require('fs'),vm=require('vm');
const source=fs.readFileSync('ios/agent/hashlife/life.js','utf8')+'\n'+fs.readFileSync('ios/agent/hashlife/adapter.js','utf8');
vm.runInThisContext(source+'\nglobalThis.engineLife=life');
const canon=c=>c.map(p=>p.join(',')).sort().join(';');
function step(c){let counts=new Map(),old=new Set(c.map(p=>p.join(',')));for(let [x,y]of c)for(let dx=-1;dx<=1;dx++)for(let dy=-1;dy<=1;dy++)if(dx||dy){let k=[x+dx,y+dy].join(',');counts.set(k,(counts.get(k)||0)+1);}return [...counts].filter(([k,n])=>n===3||n===2&&old.has(k)).map(([k])=>k.split(',').map(Number));}
for(let trial=0;trial<12;trial++){let cells=[];for(let y=-12;y<=12;y++)for(let x=-12;x<=12;x++)if(((x*1237+y*713+trial*991)>>>0)%7<2)cells.push([x,y]);let c=cells;for(let n=0;n<40;n++){if(canon(engineLife.evolve(cells,n))!==canon(c))throw Error('Mismatch '+trial+' '+n);c=step(c);}}
const glider=[[1,0],[2,1],[0,2],[1,2],[2,2]];
if(canon(engineLife.evolve(glider,1048576))!==canon(glider.map(([x,y])=>[x+262144,y+262144])))throw Error('Long run coordinates');
console.log('PASS HashLife vs independent sparse B3/S23: 480 cases, negative coordinates, exact long run');
