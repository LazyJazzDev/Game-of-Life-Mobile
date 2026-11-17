const fs=require('fs'),vm=require('vm');
const source=fs.readFileSync('ios/agent/hashlife/life.js','utf8')+'\n'+fs.readFileSync('ios/agent/hashlife/adapter.js','utf8');
vm.runInThisContext(source+'\nglobalThis.engineLife=life');
const canon=c=>c.map(p=>p.join(',')).sort().join(';');
function step(c){let counts=new Map(),old=new Set(c.map(p=>p.join(',')));for(let [x,y]of c)for(let dx=-1;dx<=1;dx++)for(let dy=-1;dy<=1;dy++)if(dx||dy){let k=[x+dx,y+dy].join(',');counts.set(k,(counts.get(k)||0)+1);}return [...counts].filter(([k,n])=>n===3||n===2&&old.has(k)).map(([k])=>k.split(',').map(Number));}
for(let trial=0;trial<12;trial++){let cells=[];for(let y=-12;y<=12;y++)for(let x=-12;x<=12;x++)if(((x*1237+y*713+trial*991)>>>0)%7<2)cells.push([x,y]);let c=cells;for(let n=0;n<40;n++){if(canon(engineLife.evolve(cells,n))!==canon(c))throw Error('Mismatch '+trial+' '+n);c=step(c);}}
const glider=[[1,0],[2,1],[0,2],[1,2],[2,2]];
if(canon(engineLife.evolve(glider,1048576))!==canon(glider.map(([x,y])=>[x+262144,y+262144])))throw Error('Long run coordinates');
console.log('PASS HashLife vs independent sparse B3/S23: 480 cases, negative coordinates, exact long run');
const assert=require('node:assert/strict');
assert.deepEqual(engineLife.parseRLE('# comment\nx = 4, y = 12, rule = B3/S23\n2o10$3bo!'),[[0,0],[1,0],[3,10]]);
assert.deepEqual(engineLife.parseRLE('o2$2bo!'),[[0,0],[2,2]]);
assert.deepEqual(engineLife.parseRLE('!'),[]);
for(const bad of ['o','0o!','o!o','x = 1, y = 1, rule = B36/S23\no!','999999999o!','o?!!','1!'])assert.throws(()=>engineLife.parseRLE(bad));
for(let trial=0;trial<100;trial++) {
  const cells=[];
  for(let i=0;i<100;i++)cells.push([((i*731+trial*17)%311)-155,((i*137+trial*53)%509)-254]);
  cells.push(cells[0]);
  const {rle,origin}=engineLife.encodeRLE(cells);
  const restored=engineLife.parseRLE(rle).map(([x,y])=>[x+origin[0],y+origin[1]]);
  assert.equal(canon(restored),canon([...new Map(cells.map(p=>[p.join(','),p])).values()]));
}
assert.deepEqual(engineLife.encodeRLE([]),{rle:'x = 0, y = 0, rule = B3/S23\n!',origin:[0,0]});
assert.throws(()=>engineLife.encodeRLE([[NaN,0]]));
console.log('PASS RLE codecs: compressed empty rows, headers, malformed input, 100 sparse signed-coordinate round trips');
