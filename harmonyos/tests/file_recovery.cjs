// Exercise the production file-completion and error-recovery methods without ArkUI.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
const source = fs.readFileSync('harmonyos/entry/src/main/ets/pages/Index.ets', 'utf8');
function method(start, end) { return source.slice(source.indexOf(start), source.indexOf(end)); }
const code = `${method('class FileOperationError', '@Entry')}
class Harness {
 librarySave = false;
 context() { return {cacheDir: '/cache'}; }
 request = 0; stats = {fileRevision: 0}; opened = true; fileBusy = true;
 text(key: string): string { return key; }
 send(command: Object): void { send(command); }
 alert(title: string, message: string): void { alerts.push(message); }
 ${method('  private async completeFile(', '  private patternDirectory(')}
 ${method('  private async finishFile(', '  // The built-in patterns')}
 ${method('  private systemFile()', '  // ---- Grid size popover ----')}
}
exports.Harness = Harness;`;
let state, nextError, commands, alerts, pickerResult;
const exportsObject = {};
vm.runInNewContext(ts.transpileModule(code, { compilerOptions: { target: ts.ScriptTarget.ES2020 } }).outputText,
 { exports: exportsObject, Error, JSON, Math, Promise, setTimeout: fn => fn(),
 picker: {DocumentSelectOptions: class {}, DocumentSaveOptions: class {},
 DocumentViewPicker: class {async select() {return pickerResult;} async save() {return pickerResult;}}},
 fs: {openSync() {throw Error('cancelled picker must never read a file');}},
 native: { status: () => JSON.stringify(state) }, alerts: alerts = [],
 send(command) { commands.push(command); state = {fileRevision: command.request, fileRequest: 0, error: '', fileError: nextError}; }
 });
(async () => {
 const h = new exportsObject.Harness();
 commands = []; state = {fileRevision: 0, fileRequest: 1}; nextError = "Pattern cells must be '.' or 'O'";
 await h.finishFile(() => h.completeFile('bad.cells'));
 assert.equal(h.fileBusy, false);
 assert.deepEqual(alerts, ['invalid_pattern_file']);
 assert.equal(commands.length, 1, 'failed parse already completed: do not send a second cancellation');
 nextError = ''; h.fileBusy = true;
 await h.finishFile(() => h.completeFile('valid.cells'));
 assert.equal(h.fileBusy, false); assert.equal(commands.length, 2);
 assert.equal(alerts.length, 1, 'successful retry must not repeat the previous error');
 state.fileRequest = 1; h.fileBusy = true;
 await h.finishFile(async () => { throw new Error('picker read failed'); });
 assert.equal(commands.at(-1).path, '', 'IO failure must cancel the pending native file request');
 assert.equal(h.fileBusy, false); assert.equal(alerts.length, 2);
 assert.equal(alerts[1], 'file_operation_failed', 'never display a raw English platform exception');
 state.fileRequest = 1; h.fileBusy = true;
 await h.finishFile(() => h.completeFile(''));
 assert.equal(h.fileBusy, false); assert.equal(alerts.length, 2, 'cancel is not an error');
 for (const save of [false, true]) {
  for (const result of [undefined, null, []]) {
   pickerResult = result; h.librarySave = save; state.fileRequest = save ? 2 : 1; h.fileBusy = true;
   const count = alerts.length;
   h.systemFile(); await new Promise(r => setImmediate(r));
   assert.equal(h.fileBusy, false); assert.equal(alerts.length, count, 'closing either picker must be silent');
   assert.equal(state.fileRequest, 0);
  }
 }
 console.log('File recovery: malformed import, successful retry, IO failure and cancellation passed.');
})().catch(e => { console.error(e); process.exitCode = 1; });
