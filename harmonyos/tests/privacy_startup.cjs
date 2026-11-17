const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/pages/Index.ets', 'utf8');
const methods = source.slice(source.indexOf('  private checkPrivacy()'), source.indexOf('  private async showPrivacy()'));
const output = ts.transpileModule(`class Harness {
 pageAlive = true; privacyObserved = true; privacyRequested = false;
 privacyBusy = false; privacyAgreed = false; privacyError = ''; starts = 0; exits = 0;
 privacy: any;
 text(key: string) { return key; }
 startGame() { this.starts++; }
 stopGame() { this.privacyAgreed = false; }
 context() { return {terminateSelf: async () => { this.exits++; }}; }
 ${methods}
} exports.Harness = Harness;`, {compilerOptions: {target: ts.ScriptTarget.ES2020}}).outputText;
const exportsObject = {};
vm.runInNewContext(output, {exports: exportsObject});
(async () => {
 let agreed = false, requests = 0, resolve;
 const h = new exportsObject.Harness();
 h.privacy = { isAgreed: () => agreed, request: () => { requests++; return new Promise(r => { resolve = r; }); }};
 h.checkPrivacy(); h.checkPrivacy();
 assert.equal(requests, 1, 'startup opens consent automatically without duplicate foreground prompts');
 agreed = true; resolve(); await new Promise(r => setImmediate(r));
 assert.equal(h.starts, 1); assert.equal(h.exits, 0);
 h.checkPrivacy(); assert.equal(requests, 1); assert.equal(h.starts, 1);
 const declined = new exportsObject.Harness();
 declined.privacy = {isAgreed: () => false, request: async () => {}};
 await declined.requestPrivacy(); assert.equal(declined.exits, 1); assert.equal(declined.starts, 0);
 const failed = new exportsObject.Harness();
 failed.privacy = {isAgreed: () => false, request: async () => {throw Error('unavailable');}};
 await failed.requestPrivacy(); assert.equal(failed.privacyError, 'privacy_unavailable'); assert.equal(failed.privacyBusy, false);
 const gone = new exportsObject.Harness();
 gone.privacy = {isAgreed: () => true, request: async () => {gone.pageAlive = false;}};
 await gone.requestPrivacy(); assert.equal(gone.starts, 0); assert.equal(gone.exits, 0);
 console.log('Automatic privacy startup: accept, decline, duplicate events, service failure and late completion passed.');
})().catch(e => {console.error(e); process.exitCode = 1;});
