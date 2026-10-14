// Run with node harmonyos/tests/hosted_privacy.cjs (DevEco's TypeScript compiler).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const ts = require(process.env.HARMONY_TYPESCRIPT ||
  '/Applications/DevEco-Studio.app/Contents/tools/hvigor/hvigor-ohos-plugin/node_modules/typescript');
let links = [{ type: 1, id: 'policy', versionCode: 2, url: 'https://example.com/privacy' }];
let results = [];
let type = 1;
let failQuery = false;
let callback;
const manager = {
  AppPrivacyMgmtType: { FULL_MODE: 1 }, AppPrivacyLinkType: { PRIVACY_STATEMENT_LINK: 1 },
  AppPrivacyType: { PRIVACY_AGREEMENT: 1 }, AppPrivacyResultType: { FULL_MODE_AGREED: 1 },
  getAppPrivacyMgmtInfo() { if (failQuery) throw Error('unavailable'); return { type, privacyInfo: links }; },
  getAppPrivacyResult: () => results,
  requestAppPrivacyConsent: async () => {},
  disableService() { results = []; }
};
const moduleExports = {};
const source = fs.readFileSync(path.join(__dirname, '../entry/src/main/ets/privacy/HostedPrivacy.ets'), 'utf8');
const output = ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS } }).outputText;
vm.runInNewContext(output, { exports: moduleExports, require(name) {
  if (name === '@kit.AppGalleryKit') return { privacyManager: manager };
  if (name === '@kit.PerformanceAnalysisKit') return { hilog: { error() {} } };
  if (name === '@kit.BasicServicesKit') return { commonEventManager: {
    createSubscriber: async () => ({}), subscribe(_, cb) { callback = cb; }, unsubscribe() {}
  } };
  throw Error(name);
} });
(async () => {
  const privacy = new moduleExports.HostedPrivacy();
  assert.equal(privacy.isAgreed(), false, 'missing consent must not start the app');
  results = [{ type: 1, id: 'policy', versionCode: 1, result: 1 }];
  assert.equal(privacy.isAgreed(), false, 'old policy consent must not authorize an update');
  results[0].versionCode = 2;
  results[0].id = 'different-policy';
  assert.equal(privacy.isAgreed(), false, 'consent must belong to the hosted policy');
  results[0].id = 'policy';
  for (const result of [0, 2]) {
    results[0].result = result;
    assert.equal(privacy.isAgreed(), false, 'declined or re-sign required must block startup');
  }
  results[0].result = 1;
  assert.equal(privacy.isAgreed(), true);
  type = 0;
  assert.equal(privacy.isAgreed(), false, 'unconfigured hosting must not silently bypass consent');
  type = 1;
  failQuery = true;
  assert.throws(() => privacy.isAgreed(), /unavailable/);
  failQuery = false;
  let opened;
  await privacy.showPolicy({ openLink: async url => { opened = url; } });
  assert.equal(opened, links[0].url);
  privacy.revoke();
  assert.equal(privacy.isAgreed(), false);
  let changes = 0;
  await privacy.observe(() => changes++);
  callback(null);
  assert.equal(changes, 1);
  privacy.dispose();
  callback(null);
  assert.equal(changes, 1, 'late events after disposal must not restart the app');
  console.log('Hosted privacy: consent, version changes, errors, links, revocation and event lifecycle passed.');
})().catch(error => { console.error(error); process.exitCode = 1; });
