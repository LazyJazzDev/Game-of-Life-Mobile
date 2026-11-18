const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const c=vm.createContext({console,Date:{now:()=>0}});
vm.runInContext(fs.readFileSync('ios/agent/hashlife/life.js','utf8')+'\n'+fs.readFileSync('ios/agent/hashlife/adapter.js','utf8')+'\n'+fs.readFileSync('mobile/agent-preview.js','utf8'),c);
const run=code=>JSON.parse(vm.runInContext('JSON.stringify('+code+')',c));
assert.equal(run('agentPreviewFrame([],0,"result",life.encodeRLE)').rle,'x = 1, y = 1, rule = B3/S23\n!');
assert.throws(()=>run('agentPreviewFrame([[NaN,0]],0,"input",life.encodeRLE)'));
const large=run('agentPreviewFrame([[-10,-20],[999990,999980]],12,"result",life.encodeRLE)');
assert.ok(large.stride>1);assert.equal(large.population,2);
c.rle=large.rle;assert.equal(run('life.parseRLE(rle)').length,2);
vm.runInContext('var frames=[];life.preview=(cells,generation,phase)=>frames.push({generation,phase,cells,frame:agentPreviewFrame(cells,generation,phase,life.encodeRLE)});var input=[[0,0],[1,0],[2,0]];var result=life.evolve(input,13);',c);
const frames=run('frames');assert.deepEqual(frames.map(f=>f.generation),[0,1,3,5,7,9,11,13]);
for(const f of frames){assert.equal(f.frame.stride,1);c.rle=f.frame.rle;const decoded=run('life.parseRLE(rle)');assert.deepEqual(decoded,f.generation%2?[[0,0],[0,1],[0,2]]:[[0,0],[1,0],[2,0]]);}
assert.deepEqual(run('input'),[[0,0],[1,0],[2,0]]);
assert.deepEqual(run('result'),[[1,-1],[1,0],[1,1]]);
console.log('PASS exact native RLE preview snapshots, empty/signed inputs, explicit wide-world display scale, real HashLife checkpoints and unchanged simulation');

vm.runInContext('frames=[];life.evolve(input,1024);',c);
assert.deepEqual(run('frames.map(f=>f.generation)'),[0,1,257,513,769,897,961,1024]);
console.log('PASS instantaneous power-of-two HashLife runs still emit bounded intermediate states');
