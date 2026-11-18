const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/pages/Index.ets', 'utf8');
const method = source.slice(source.indexOf('  private async agentNative('), source.indexOf('  private queueAgentPreview('));
const output = ts.transpileModule('class Bridge {\n' + method + '\n}\nexports.Bridge = Bridge;', {
  compilerOptions: { target: ts.ScriptTarget.ES2020 }
}).outputText;
let tick = 0, request, scenario, bridge;
const native = {
  command(value) { request = JSON.parse(value); },
  status() {
    tick++;
    if (scenario === 'cancel') bridge.agentVisualRun++;
    const pending = scenario !== 'timeout' && tick <= 350;
    return JSON.stringify({ confirmationRequest: pending ? 1 : 0,
      agentId: pending || scenario === 'timeout' ? 0 : request.request,
      agentReply: JSON.stringify({ ok: scenario !== 'decline', confirmed: scenario !== 'decline', error: scenario === 'decline' ? 'user_declined' : undefined }) });
  }
};
const context = { exports: {}, native, setTimeout: callback => queueMicrotask(callback) };
vm.runInNewContext(output, context);
async function run(mode) {
  tick = 0; scenario = mode;
  bridge = Object.assign(new context.exports.Bridge(), {
    agentVisualRun: 1, agentNativeInFlight: 0, agentSequence: 0, pageAlive: true, agent: { busy: true }
  });
  const result = JSON.parse(await bridge.agentNative({ action: 'reset' }));
  assert.equal(bridge.agentNativeInFlight, 0);
  return result;
}
(async () => {
  assert.equal((await run('accept')).confirmed, true);
  assert.ok(tick > 200, 'human confirmation must survive the normal tool deadline');
  assert.equal((await run('decline')).error, 'user_declined');
  assert.equal((await run('cancel')).error, 'cancelled');
  assert.equal((await run('timeout')).error, 'timeout_do_not_repeat_mutation');
  assert.equal(tick, 200);
  console.log('PASS actual Harmony native bridge: long confirmation wait, accept, decline, cancellation and ordinary timeout');
})().catch(error => { console.error(error); process.exitCode = 1; });
