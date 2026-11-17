const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/agent/LifeAgent.ets', 'utf8');
const output = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 } }).outputText;
let respond, destroyed = 0, requests = [];
const http = { RequestMethod: { POST:'POST' }, createHttp() {
  const handlers = {};
  return { on(event, handler) { handlers[event] = handler; }, async requestInStream(url, options) {
    requests.push(JSON.parse(options.extraData));
    const response = await respond();
    const bytes = Buffer.from(response.body);
    // Deliberately split Chinese UTF-8 characters and SSE lines across chunks.
    for (let i=0; i<bytes.length; i+=7) {
      const chunk = bytes.subarray(i,i+7);
      handlers.dataReceive?.(chunk.buffer.slice(chunk.byteOffset,chunk.byteOffset+chunk.length));
    }
    handlers.dataEnd?.(); return response.status;
  }, destroy() { destroyed++; } };
}};
class Decoder { constructor() { this.value = new TextDecoder(); } decodeToString(bytes, options) { return this.value.decode(bytes, options); } }
const exportsObject = {};
vm.runInNewContext(output, {exports:exportsObject, require(name) {return name === '@kit.ArkTS' ? {util:{TextDecoder:Decoder}} : {http};}, setInterval,clearInterval,Date});
function reply(message, finish = 'stop', terminate = true) {
  if (terminate && !message.tool_calls?.length && message.content) message = {...message, tool_calls:[{id:'done',function:{name:'task_complete',arguments:JSON.stringify({summary:message.content,evidence:['test observation']})}}]};
  const chunks = [];
  if(message.reasoning_content) chunks.push({reasoning_content:message.reasoning_content});
  if(message.content) chunks.push({content:message.content});
  for(const [index,call] of (message.tool_calls || []).entries()) {
    chunks.push({tool_calls:[{index,id:call.id,function:{name:call.function.name,arguments:''}}]});
    const raw=call.function.arguments, middle=Math.floor(raw.length/2);
    for(const part of [raw.slice(0,middle),raw.slice(middle)]) chunks.push({tool_calls:[{index,function:{arguments:part}}]});
  }
  return {status:200,body: chunks.map(delta=>'data: '+JSON.stringify({choices:[{delta}]})).join('\n\n')+'\n\ndata: '+JSON.stringify({choices:[{delta:{},finish_reason:finish}]})+'\n\ndata: [DONE]\n\n'};
}
(async () => {
  const a = new exportsObject.LifeAgent(); a.key='local-test-key'; a.consent=true; a.systemPrompt='Generic test agent';
  let called=0,pending,sawLive=false;
  a.changed=()=>{if(a.liveResponse.includes('已暂停')) sawLive=true;};
  a.execute=async()=>{called++;return '{"ok":true}';};
  respond=()=>new Promise(resolve=>{pending=resolve;});
  const task=a.send('pause'); a.cancel();
  pending(reply({content:'',tool_calls:[{id:'a',function:{name:'canvas_control',arguments:'{"action":"play","value":0}'}}]}));
  await task; assert.equal(called,0); assert.equal(a.busy,false);
  requests=[]; let round=0;
  respond=async()=> ++round===1 ? reply({content:'',reasoning_content:'internal test reasoning',tool_calls:[{id:'a',function:{name:'canvas_state',arguments:'{}'}}]},'tool_calls') : reply({content:'已暂停。'});
  await a.send('inspect');assert.equal(called,1);assert.equal(a.lines.at(-1).text,'已暂停。');assert.ok(sawLive);
  const finishedProgress=a.progress;a.cancel();assert.equal(a.progress,finishedProgress,'backgrounding an idle agent must preserve completion feedback');
  assert.equal(requests[1].messages.at(-1).tool_call_id,'a');assert.equal(requests[1].messages.at(-2).reasoning_content,'internal test reasoning');
  assert.ok(!JSON.stringify(a.lines).includes('internal test reasoning'));assert.ok(!JSON.stringify(requests).includes('local-test-key'));
  round=0;requests=[];
  respond=async()=> ++round===1 ? reply({tool_calls:[{id:'bad',function:{name:'canvas_edit',arguments:'{"cells":'}}]},'length') : reply({content:'Recovered'});
  await a.send('recover');assert.equal(called,1,'truncated tool call must never execute');assert.equal(requests[1].reasoning_effort,'low');assert.equal(a.lines.at(-1).text,'Recovered');
  // Model-authored summaries survive reasoning/tool phases and appear in chat.
  const brief='检查 OTCA 单元的周期，确认它能模拟一个元细胞';
  round=0; let held=false;
  a.execute=async()=>{assert.equal(a.progress,brief);assert.ok(a.lines.some(x=>x.text===brief));held=true;return '{"ok":true}';};
  respond=async()=>++round===1 ? reply({content:'我先核对 OTCA 规则。',tool_calls:[{id:'brief',function:{name:'canvas_state',arguments:JSON.stringify({work_brief:brief})}}]},'tool_calls') : reply({content:'规则核对完成。'});
  await a.send('report');assert.ok(held);assert.ok(a.lines.some(x=>x.text==='我先核对 OTCA 规则。'));assert.ok(a.lines.some(x=>x.text==='规则核对完成。'));
  // Cross both former limits (192 rounds and 768 calls) without truncating work.
  round=0;let longCalls=0;
  a.execute=async()=>{longCalls++;return '{"ok":true}';};
  respond=async()=> ++round<=193 ? reply({tool_calls:Array.from({length:4},(_,i)=>({id:`long-${round}-${i}`,function:{name:'canvas_state',arguments:JSON.stringify({work_brief:'继续核对当前元细胞状态'})}}))},'tool_calls') : reply({content:'全部检查完成。'});
  await a.send('long');assert.equal(round,194);assert.equal(longCalls,772);assert.equal(a.lines.at(-1).text,'全部检查完成。');
  // A prose-only refusal is not completion; keep executing the original task.
  round=0; let continued=0;
  a.execute=async()=>{continued++;return '{"ok":true}';};
  respond=async()=>{
    round++;
    if(round===1) return reply({content:'目前只完成一位，是否继续？'},'stop',false);
    if(round===2) return reply({tool_calls:[{id:'continue',function:{name:'canvas_state',arguments:'{}'}}]},'tool_calls');
    if(round===3) return reply({tool_calls:[{id:'invalid',function:{name:'task_complete',arguments:JSON.stringify({summary:'done',evidence:[]})}}]},'tool_calls');
    return reply({content:'全部验证完成'});
  };
  await a.send('full task'); assert.equal(round,4); assert.equal(continued,1);
  assert.ok(a.trace.some(x=>x.name==='task_complete' && JSON.parse(x.result).ok===false));
  assert.ok(requests.at(-3).messages.at(-1).content.includes('Audit the original request'));
  respond=async()=>reply({tool_calls:[{id:'wait',function:{name:'task_wait',arguments:JSON.stringify({question:'请选择需要保留的画布',reason:'destructive_confirmation'})}}]},'tool_calls');
  await a.send('wait'); assert.equal(a.progress,'等待你的信息'); assert.equal(a.lines.at(-1).text,'请选择需要保留的画布');
  // Cancellation also interrupts a model that keeps returning prose without tools.
  round=0; respond=async()=>{if(++round===3) a.cancel();return reply({content:'还在研究'},'stop',false);};
  await a.send('cancel ongoing audit'); assert.equal(round,3);assert.equal(a.progress,'已停止');
  respond=async()=>({status:401,body:'secret server diagnostic'});
  await a.send('fail');assert.equal(a.busy,false);assert.ok(!a.lines.at(-1).text.includes('secret server diagnostic'));
  a.consent=false;const before=requests.length;await a.send('no consent');assert.equal(requests.length,before);assert.ok(destroyed>0);
  assert.ok(requests[0].model==='deepseek-flash');
  console.log('PASS streaming UTF-8/tool fragments, live feedback, reasoning isolation, truncation recovery, cancellation, consent and HTTP errors');
})().catch(e=>{console.error(e);process.exit(1);});
