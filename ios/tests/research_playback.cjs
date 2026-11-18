const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const engine=fs.readFileSync('ios/agent/hashlife/life.js','utf8')+'\n'+fs.readFileSync('ios/agent/hashlife/adapter.js','utf8');
let finished=false;process.on('beforeExit',()=>assert.ok(finished,'playback test has unresolved work'));
let now=0,sequence=0;const timers=new Map(),sources=new Map();
const setTimeout=(fn,ms)=>{const id=++sequence;timers.set(id,{fn,at:now+ms});return id;};
const clearTimeout=id=>timers.delete(id);
class Blob {constructor(parts){this.source=parts.join('');}}
class Worker {
  constructor(url){const self={postMessage:data=>this.onmessage?.({data})};this.self=self;
    this.context=vm.createContext({self,console:{...console},Date:{now:()=>now}});vm.runInContext(sources.get(url),this.context);}
  postMessage(data){this.self.onmessage({data});}
  terminate(){}
}
const context=vm.createContext({console,Blob,Worker,setTimeout,clearTimeout,
  URL:{createObjectURL(blob){const id=String(++sequence);sources.set(id,blob.source);return id;},revokeObjectURL(id){sources.delete(id);}}});
vm.runInContext(fs.readFileSync('mobile/agent-preview.js','utf8')+'\n'+fs.readFileSync('mobile/agent-research.js','utf8'),context);
const frames=[];context.receive=frame=>frames.push({generation:frame.generation,at:now,rle:frame.rle});context.engine=engine;
async function drain(){while(timers.size){const [id,t]=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];timers.delete(id);now=t.at;t.fn();await Promise.resolve();}}
(async()=>{
  let complete=false;
  const task=vm.runInContext('runResearch("return life.evolve([[0,0],[1,0],[2,0]],1024)",{},engine,"javascript",receive)',context).then(v=>{complete=true;return v;});
  await Promise.resolve();
  assert.equal(frames.length,1);assert.equal(complete,false,'fast calculation must not erase pending animation');
  await drain();const result=await task;
  assert.equal(result.ok,true);
  assert.deepEqual(frames.map(f=>f.generation),[0,1,257,513,769,897,961,1024]);
  for(let i=1;i<frames.length;i++)assert.ok(frames[i].at-frames[i-1].at>=300);
  assert.ok(now>=frames.at(-1).at+300,'final frame gets visible dwell time');
  assert.equal(vm.runInContext('agentResearchRuns.size',context),0);
  frames.length=0;
  const cancelled=vm.runInContext('runResearch("return life.evolve([[0,0],[1,0],[2,0]],1024)",{},engine,"javascript",receive)',context);
  vm.runInContext('cancelResearchRuns()',context);await drain();
  assert.equal((await cancelled).error,'cancelled');assert.equal(frames.length,1);
  assert.equal(timers.size,0);
  // A script may perform hundreds of evolutions; only recent bounded snapshots
  // are retained. Final output must still correspond to the exact final run.
  frames.length=0;
  const many=vm.runInContext('runResearch("for(let i=0;i<100;i++)life.evolve([[0,0],[1,0],[2,0]],16);return {done:true}",{},engine,"javascript",receive)',context);
  await drain();assert.equal((await many).ok,true);
  assert.ok(frames.length<=13);assert.equal(frames.at(-1).generation,16);
  finished=true;
  console.log('PASS real worker HashLife snapshots: paced playback, final dwell, cancellation and bounded burst queue');
})().catch(e=>{console.error(e);process.exitCode=1;});
