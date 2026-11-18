const assert = require('node:assert/strict');
const fs = require('node:fs');
let suiteFinished=false;
process.on('beforeExit',()=>assert.ok(suiteFinished,'test stopped with an unresolved async step'));
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/agent/LifeAgent.ets', 'utf8');
const output = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 } }).outputText;
let activeHandlers;
let respond, destroyed = 0, requests = [];
const http = { RequestMethod: { POST:'POST' }, createHttp() {
  const handlers = {};
  return { on(event, handler) { handlers[event] = handler; }, async requestInStream(url, options) {
    assert.equal(url,'https://api.deepseek.com/chat/completions');
    activeHandlers=handlers;
    assert.equal(options.connectTimeout,30000);
    assert.equal(options.readTimeout,0,'reasoning must not have a fixed read deadline');
    const requestBody=JSON.parse(options.extraData);
    assert.equal(requestBody.thinking.type,'enabled');assert.ok(Array.isArray(requestBody.messages));assert.ok(!('input' in requestBody));
    assert.ok(requests.length < 1200, "fixture exhausted: unexpected nonterminating tool loop");
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
  const events=[];
  if(message.reasoning_content) events.push({choices:[{delta:{reasoning_content:message.reasoning_content}}]});
  if(message.content) events.push({choices:[{delta:{content:message.content}}]});
  for(const [index,call] of (message.tool_calls || []).entries()) {
    const raw=call.function.arguments, middle=Math.floor(raw.length/2);
    events.push({choices:[{delta:{tool_calls:[{index,id:call.id,function:{name:call.function.name,arguments:raw.slice(0,middle)}}]}}]});
    events.push({choices:[{delta:{tool_calls:[{index,function:{arguments:raw.slice(middle)}}]}}]});
  }
  events.push({choices:[{delta:{},finish_reason:finish}]});
  return {status:200,body:events.map(e=>'data: '+JSON.stringify(e)).join('\n\n')+'\n\ndata: [DONE]\n\n'};
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
  assert.equal(requests[1].messages.at(-1).tool_call_id,'a');assert.ok(requests[1].messages.some(i=>i.reasoning_content==='internal test reasoning'));
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
  // EOF or server failure before response.completed must never execute a tool.
  const beforeFailureCalls=continued;
  respond=async()=>({status:200,body:'data: '+JSON.stringify({choices:[{delta:{tool_calls:[{index:0,id:'partial',function:{name:'canvas_edit',arguments:'{}'}}]}}]})+'\n\n'});
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
  let sent=requests.at(-1).messages;
  assert.ok(sent.some(m=>m.content==='Build and verify the entire eight-bit circuit'));
  assert.ok(sent.some(m=>m.role==='tool' && m.tool_call_id==='saved' && m.content.includes('work_saved')));
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
  round=0; resumed.execute=async(name)=>{assert.equal(name,'canvas_save');return '{"ok":true,"saved":true}';};
  respond=async()=>++round===1 ? reply({tool_calls:[{id:'save-resumed',function:{name:'canvas_save',arguments:'{"name":"Resumed design"}'}}]}) : reply({content:'Inspect before resuming'});
  await resumed.send('继续');sent=requests.at(-1).messages;
  assert.ok(sent.some(m=>m.role==='assistant'),'assistant messages must survive replay');
  assert.equal(actionCount,2,'resuming must not replay interrupted actions automatically');
  assert.ok(sent.some(m=>m.content==='Build and verify the entire eight-bit circuit'));
  assert.ok(sent.some(m=>m.tool_call_id==='saved'));
  assert.ok(sent.some(m=>m.content?.includes('may already have taken effect')));
  assert.ok(!sent.some(m=>m.tool_calls?.some(c=>['edit1','edit2'].includes(c.id))));
  assert.ok(!sent.some(m=>['edit1','edit2'].includes(m.tool_call_id)));
  // Confirmation stays inside the same tool loop. Completion cannot bypass a
  // saved layout, and a name collision goes back to the model for renaming.
  const workflow = new exportsObject.LifeAgent(); workflow.key='test'; workflow.consent=true; workflow.systemPrompt='Workflow test';
  let confirm, ready; const waitingConfirmation=new Promise(resolve=>{ready=resolve;});
  const savedNames=[];let saveAttempts=0;round=0;requests=[];
  workflow.execute=async(name,args)=>{
    if(name==='canvas_control') {ready();return new Promise(resolve=>{confirm=resolve;});}
    if(name==='canvas_save') {savedNames.push(args.name);return ++saveAttempts===1 ? '{"ok":false,"error":"name_exists"}' : '{"ok":true,"saved":true}';}
    assert.equal(name,'canvas_edit');return '{"ok":true}';
  };
  const call=(name,args)=>reply({tool_calls:[{id:'workflow-'+round,function:{name,arguments:JSON.stringify(args)}}]});
  respond=async()=>{
    ++round;assert.ok(round<=6);
    if(round===1)return call('canvas_control',{action:'reset'});
    if(round===2)return call('canvas_edit',{cells:[[0,0,1]]});
    if(round===3||round===6)return call('task_complete',{summary:'Saved design',evidence:['verified']});
    return call('canvas_save',{name:round===4?'My design':'My design revised'});
  };
  const workflowTask=workflow.send('Design a pattern');await waitingConfirmation;
  assert.equal(requests.length,1);assert.equal(workflow.busy,true);
  confirm('{"ok":true,"confirmed":true,"applied":true}');await workflowTask;
  assert.equal(round,6);assert.deepEqual(savedNames,['My design','My design revised']);
  assert.ok(workflow.trace.some(t=>JSON.parse(t.result).error==='layout_save_required'));
  assert.equal(workflow.lines.at(-1).text,'Saved design');
  // Declining also resumes automatically and preserves the negative result.
  round=0;requests=[];workflow.execute=async()=>'{"ok":false,"applied":false,"error":"user_declined"}';
  respond=async()=>++round===1 ? call('canvas_control',{action:'randomize'}) : reply({content:'Kept the canvas'});
  await workflow.send('Try another layout');assert.equal(round,2);
  assert.ok(requests[1].messages.some(i=>i.role==='tool'&&i.content.includes('user_declined')));
  console.log('PASS confirmation continuation, declined action, mandatory save and model-selected rename');
  a.consent=false;const before=requests.length;await a.send('no consent');assert.equal(requests.length,before);assert.ok(destroyed>0);
  assert.ok(requests[0].model==='deepseek-flash');
  const transport=new exportsObject.LifeAgent();transport.key='test';transport.consent=true;transport.systemPrompt='Test';
  let resolveResponse;
  respond=()=>new Promise(resolve=>{resolveResponse=resolve;});
  let sawReceipt=false;
  transport.changed=()=>{if(transport.lines.some(l=>l.text.includes('已收到你的任务'))) sawReceipt=true;};
  const slow=transport.send('Investigate');
  assert.ok(sawReceipt,'receipt must be visible before any server response');
  assert.ok(transport.progress.includes('连接'));
  activeHandlers.headersReceive?.({});
  assert.ok(transport.progress.includes('等待服务端接收'));
  const event=Buffer.from('data: '+JSON.stringify({choices:[{delta:{reasoning_content:'thinking'}}]})+'\n\n');
  activeHandlers.dataReceive(event.buffer.slice(event.byteOffset,event.byteOffset+event.byteLength));
  assert.ok(transport.progress.includes('服务端已接收'));
  assert.ok(transport.busy);
  resolveResponse(reply({content:'Finished after reasoning'}));await slow;
  assert.equal(transport.lines.at(-1).text,'Finished after reasoning');
  respond=async()=>{throw Object.assign(new Error('Operation timeout'),{code:2300028});};
  await transport.send('Network failure');assert.ok(transport.lines.at(-1).text.includes('未能连接'));
  respond=async()=>{activeHandlers.headersReceive?.({});throw Object.assign(new Error('Operation timeout'),{code:2300028});};
  await transport.send('No acceptance');assert.ok(transport.lines.at(-1).text.includes('尚未收到任务接收确认'));
  respond=async()=>{
    activeHandlers.dataReceive(event.buffer.slice(event.byteOffset,event.byteOffset+event.byteLength));
    throw Object.assign(new Error('Operation timeout'),{code:2300028});
  };
  await transport.send('Stream interruption');assert.ok(transport.lines.at(-1).text.includes('不表示推理失败'));
  assert.ok(!transport.lines.at(-1).text.includes('Operation timeout'));
  let briefRounds=0;
  const retained='正在检查闪烁子的周期';
  respond=async()=>{
    if(++briefRounds===1) return reply({content:retained,tool_calls:[{id:'read',function:{name:'canvas_state',arguments:'{}'}}]});
    assert.equal(transport.progress,retained);
    activeHandlers.headersReceive?.({});
    activeHandlers.dataReceive(event.buffer.slice(event.byteOffset,event.byteOffset+event.byteLength));
    assert.equal(transport.progress,retained,'network phases must not overwrite model feedback');
    return reply({content:'Done'});
  };
  transport.execute=async()=>'{"ok":true}';
  await transport.send('Keep the work brief');assert.equal(briefRounds,2);
  console.log('PASS immediate receipt, acceptance status, unlimited reasoning read and phase-specific network errors');
  suiteFinished=true;
  console.log('PASS DeepSeek SSE UTF-8/tool fragments, reasoning replay, completion recovery, cancellation, retained context, consent and HTTP errors');
})().catch(e=>{console.error(e);process.exit(1);});
