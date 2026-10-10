// Exercise the production page's consent and lifecycle methods with sensor/storage doubles.
// Run: node harmonyos/tests/privacy_consent.cjs /path/to/typescript/lib/typescript.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require(process.argv[2]);
const source = fs.readFileSync('harmonyos/entry/src/main/ets/pages/Index.ets', 'utf8');
const methods = ['aboutToAppear', 'open', 'leave', 'startGravity', 'stopGravity',
  'foregroundChanged', 'onPageShow', 'onPageHide', 'setGravityConsent'];
const bodies = methods.map(name => {
  const match = source.match(new RegExp(`^  (?:private )?${name}\\([^]*?^  }`, 'm'));
  assert.ok(match, `Missing production method ${name}`);
  return match[0];
});
const fields = ['opened', 'sensorActive', 'gravityAllowed', 'privacyChoiceKnown', 'pageVisible',
  'foreground', 'entryAngle'].map(name => {
  const match = source.match(new RegExp(`private ${name}: [^;]+;`));
  assert.ok(match, `Missing production field ${name}`);
  return match[0];
});
const code = ts.transpileModule(`class Page { ${fields.join('\n')} ${bodies.join('\n')} } globalThis.Page = Page;`,
  { compilerOptions: { target: ts.ScriptTarget.ES2020 } }).outputText;
let persisted = '', failSave = false, starts = 0, stops = 0, lastReading;
const store = { getSync: () => persisted, putSync: (_, value) => { store.pending = value; },
  flushSync: () => { if (failSave) throw Error('disk'); persisted = store.pending; } };
const sandbox = { preferences: { getPreferencesSync: () => store },
  sensor: { SensorId: { GRAVITY: 1 }, on: (_, callback) => { starts++; lastReading = callback; }, off: () => stops++ },
  AppStorage: { get: () => '' }, setInterval: () => 1, Math };
vm.createContext(sandbox); vm.runInContext(code, sandbox);
function page() {
  const p = new sandbox.Page(); p.foreground = true; p.commands = []; p.prompts = 0;
  p.context = () => ({}); p.text = s => s; p.alert = () => {}; p.trackSafeArea = () => {};
  p.lockOrientation = () => {}; p.send = c => p.commands.push(c); p.privacySettings = () => p.prompts++;
  p.aboutToAppear(); return p;
}
let p = page(); p.open(); p.onPageShow();
assert.equal(starts, 0, 'No readings before first consent, including upgrades'); assert.equal(p.prompts, 1);
p.setGravityConsent(false); p.foregroundChanged(); assert.equal(starts, 0, 'Decline must not subscribe');
p = page(); p.open(); assert.equal(p.prompts, 0); assert.equal(starts, 0, 'Decline survives relaunch');
p.setGravityConsent(true); assert.equal(starts, 1); assert.equal(persisted, 'enabled');
lastReading({ x: 9, y: 0 }); assert.ok(p.commands.some(c => c.type === 'icons' && c.angle !== 0));
p.foreground = false; p.foregroundChanged(); assert.equal(stops, 1);
let count = p.commands.length; lastReading({ x: 9, y: 0 }); assert.equal(p.commands.length, count, 'Ignore queued background reading');
p.foreground = true; p.foregroundChanged(); assert.equal(starts, 2);
p.onPageHide(); assert.equal(stops, 2); p.foregroundChanged(); assert.equal(starts, 2, 'Hidden page stays unsubscribed');
p.onPageShow(); assert.equal(starts, 3);
p.setGravityConsent(false); assert.equal(stops, 3); count = p.commands.length;
lastReading({ x: 9, y: 0 }); assert.equal(p.commands.length, count, 'Ignore queued reading after revocation');
p.onPageShow(); assert.equal(starts, 3);
failSave = true; p.setGravityConsent(true); assert.equal(starts, 3, 'Failed persistence cannot enable readings');
assert.equal(p.gravityAllowed, false); failSave = false;
p.setGravityConsent(true); p.leave(); assert.equal(p.sensorActive, false);
console.log('PASS: consent, decline persistence, opt-in, revocation, queued callbacks, foreground/page lifecycle and save failure');
