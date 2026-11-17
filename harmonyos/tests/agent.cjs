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
    assert.equal(url,'https://api.openai.com/v1/responses');
    const requestBody=JSON.parse(options.extraData);
    assert.equal(requestBody.store,false);assert.equal(requestBody.parallel_tool_calls,false);
    assert.ok(!('thinking' in requestBody));assert.ok(!('messages' in requestBody));
    requests.push(requestBody);
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
  const events=[], output=[];
  if(message.reasoning_content) output.push({type:'reasoning',id:'rs_test',summary:[],encrypted_content:'encrypted-test-state',created_by:{model:'test'}});
  if(message.content) {
    events.push({type:'response.output_text.delta',delta:message.content});
    output.push({type:'message',id:'msg_test',role:'assistant',phase:'commentary',status:'completed',content:[{type:'output_text',text:message.content,annotations:[]}],created_by:{model:'test'}});
  }
  for(const call of message.tool_calls || []) {
    const item={type:'function_call',id:'fc_'+call.id,call_id:call.id,name:call.function.name,arguments:call.function.arguments,status:'completed'};
    events.push({type:'response.output_item.added',item:{...item,arguments:''}});
    const raw=item.arguments,middle=Math.floor(raw.length/2);
    for(const delta of [raw.slice(0,middle),raw.slice(middle)]) events.push({type:'response.function_call_arguments.delta',item_id:item.id,delta});
    events.push({type:'response.output_item.done',item});output.push(item);
  }
  events.push(finish==='length' ? {type:'response.incomplete',response:{status:'incomplete',output,incomplete_details:{reason:'max_output_tokens'}}} :
    {type:'response.completed',response:{status:'completed',output}});
  return {status:200,body:events.map(e=>'event: '+e.type+'\ndata: '+JSON.stringify(e)).join('\n\n')+'\n\n'};
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
  assert.equal(requests[1].input.at(-1).call_id,'a');assert.ok(requests[1].input.some(i=>i.type==='reasoning' && i.encrypted_content==='encrypted-test-state'));assert.ok(!requests[1].input.some(i=>i.created_by));
  assert.ok(!JSON.stringify(a.lines).includes('internal test reasoning'));assert.ok(!JSON.stringify(requests).includes('local-test-key'));
  round=0;requests=[];
  respond=async()=> ++round===1 ? reply({tool_calls:[{id:'bad',function:{name:'canvas_edit',arguments:'{"cells":'}}]},'length') : reply({content:'Recovered'});
  await a.send('recover');assert.equal(called,1,'truncated tool call must never execute');assert.equal(requests[1].reasoning.effort,'low');assert.equal(a.lines.at(-1).text,'Recovered');
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
  assert.ok(requests.at(-3).input.at(-1).content.includes('Audit the original request'));
  respond=async()=>reply({tool_calls:[{id:'wait',function:{name:'task_wait',arguments:JSON.stringify({question:'请选择需要保留的画布',reason:'destructive_confirmation'})}}]},'tool_calls');
  await a.send('wait'); assert.equal(a.progress,'等待你的信息'); assert.equal(a.lines.at(-1).text,'请选择需要保留的画布');
  // Cancellation also interrupts a model that keeps returning prose without tools.
  round=0; respond=async()=>{if(++round===3) a.cancel();return reply({content:'还在研究'},'stop',false);};
  await a.send('cancel ongoing audit'); assert.equal(round,3);assert.equal(a.progress,'已停止');
  respond=async()=>({status:401,body:'secret server diagnostic'});
  await a.send('fail');assert.equal(a.busy,false);assert.ok(!a.lines.at(-1).text.includes('secret server diagnostic'));
  // EOF or server failure before response.completed must never execute a tool.
  const beforeFailureCalls=continued;
  respond=async()=>({status:200,body:'data: '+JSON.stringify({type:'response.output_item.done',item:{type:'function_call',call_id:'partial',name:'canvas_edit',arguments:'{}'}})+'\n\n'});
  await a.send('stream disconnected');assert.equal(a.progress,'请求失败');assert.equal(continued,beforeFailureCalls);
  respond=async()=>({status:200,body:'data: '+JSON.stringify({type:'response.failed',response:{status:'failed'}})+'\n\n'});
  await a.send('server failed');assert.equal(a.progress,'请求失败');assert.equal(continued,beforeFailureCalls);
  // Network failure must preserve the original objective and completed tool results.
  const resumed = new exportsObject.LifeAgent(); resumed.key='local-test-key'; resumed.consent=true; resumed.systemPrompt='Generic research agent';
  round=0;resumed.execute=async()=>'{"ok":true,"document":"work_saved"}';
  respond=async()=>{
    if(++round===1) return reply({tool_calls:[{id:'saved',function:{name:'research_write',arguments:'{}'}}]},'tool_calls');
    throw Error('network connection lost');
  };
  await resumed.send('Build and verify the entire eight-bit circuit');
  assert.equal(resumed.progress,'请求失败');
  respond=async()=>reply({content:'Resumed original task'});
  await resumed.send('继续');
  let sent=requests.at(-1).input;
  assert.ok(sent.some(m=>m.content==='Build and verify the entire eight-bit circuit'));
  assert.ok(sent.some(m=>m.type==='function_call_output' && m.call_id==='saved' && m.output.includes('work_saved')));
  // Cancel partway through a two-action batch. Retain the earlier complete prefix,
  // remove the incomplete chain and warn that actions may already have taken effect.
  let release, entered;
  const started=new Promise(resolve=>{entered=resolve;});
  let actionCount=0;
  resumed.execute=async()=>{
    if(++actionCount===1) return '{"ok":true}';
    entered();return new Promise(resolve=>{release=resolve;});
  };
  respond=async()=>reply({tool_calls:['edit1','edit2'].map(id=>({id,function:{name:'canvas_edit',arguments:'{}'}}))},'tool_calls');
  const interrupted=resumed.send('Apply the next two edits');await started;resumed.cancel();release('{"ok":true}');await interrupted;
  respond=async()=>reply({content:'Inspect before resuming'});
  await resumed.send('继续');sent=requests.at(-1).input;
  assert.ok(sent.some(m=>m.type==='message' && m.phase==='commentary'),'assistant phase must survive replay');
  assert.equal(actionCount,2,'resuming must not replay interrupted actions automatically');
  assert.ok(sent.some(m=>m.content==='Build and verify the entire eight-bit circuit'));
  assert.ok(sent.some(m=>m.call_id==='saved'));
  assert.ok(sent.some(m=>m.content?.includes('may already have taken effect')));
  assert.ok(!sent.some(m=>m.type==='function_call' && ['edit1','edit2'].includes(m.call_id)));
  assert.ok(!sent.some(m=>['edit1','edit2'].includes(m.call_id)));
  a.consent=false;const before=requests.length;await a.send('no consent');assert.equal(requests.length,before);assert.ok(destroyed>0);
  assert.ok(requests[0].model==='gpt-6-astra');
  console.log('PASS Responses SSE UTF-8/tool fragments, encrypted reasoning replay, completion recovery, cancellation, retained context, consent and HTTP errors');
})().catch(e=>{console.error(e);process.exit(1);});
