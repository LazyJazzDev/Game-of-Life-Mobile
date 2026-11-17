const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/agent/LifeAgent.ets', 'utf8');
const output = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 } }).outputText;
let respond, destroyed = 0, requests = [];
const http = { RequestMethod: { POST:'POST' }, HttpDataType: { STRING:0 }, createHttp() {
  return { request(url, options) { requests.push(JSON.parse(options.extraData)); return respond(); }, destroy() { destroyed++; } };
}};
const exportsObject = {};
vm.runInNewContext(output, {exports:exportsObject, require() {return {http};}});
function reply(message, finish = 'stop') {return {responseCode:200,result:JSON.stringify({choices:[{message,finish_reason:finish}]})};}
(async () => {
  const a = new exportsObject.LifeAgent(); a.key='local-test-key'; a.consent=true;
  let called=0, pending;
  a.execute=async()=>{called++;return '{"ok":true}';};
  respond=()=>new Promise(resolve=>{pending=resolve;});
  const task=a.send('pause'); a.cancel();
  pending(reply({role:'assistant',content:'',tool_calls:[{id:'a',function:{name:'canvas_control',arguments:'{"action":"play","value":0}'}}]}));
  await task; assert.equal(called,0,'late completion must not execute after cancellation'); assert.equal(a.busy,false);
  requests=[]; let round=0;
  respond=async()=> ++round===1 ? reply({role:'assistant',content:'',reasoning_content:'test reasoning',tool_calls:[{id:'a',type:'function',function:{name:'canvas_state',arguments:'{}'}}]},'tool_calls') : reply({role:'assistant',content:'Paused.'});
  await a.send('inspect'); assert.equal(called,1);assert.equal(a.lines.at(-1).text,'Paused.');
  assert.equal(requests[1].messages.at(-1).tool_call_id,'a');
  assert.equal(requests[1].messages.at(-2).reasoning_content,'test reasoning');
  assert.ok(!JSON.stringify(requests).includes('local-test-key'),'key is never part of model context');
  respond=async()=>({responseCode:401,result:'secret server diagnostic'});
  await a.send('fail');assert.equal(a.busy,false);assert.ok(!a.lines.at(-1).text.includes('secret server diagnostic'));
  a.consent=false;const before=requests.length;await a.send('no consent');assert.equal(requests.length,before);
  assert.ok(destroyed>0);
  console.log('PASS Agent tool loop, reasoning roundtrip, cancellation, key isolation, consent and HTTP errors');
})().catch(e=>{console.error(e);process.exit(1);});
