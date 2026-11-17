#!/usr/bin/env python3
"""Stage the same generic research runtime and tool contract as iOS. Never stage templates."""
from pathlib import Path
import json
import re
import shutil
root = Path(__file__).resolve().parent.parent
out = root / 'harmonyos/entry/src/main/resources/rawfile'
for name in ('agent-tools.json', 'knowledge-sources.json'):
    shutil.copy2(root / 'ios/agent' / name, out / name)
shutil.copytree(root / 'ios/agent/hashlife', out / 'hashlife', dirs_exist_ok=True)
shutil.copy2(root / 'mobile/agent-research.js', out / 'agent-research.js')
shutil.copy2(root / 'mobile/agent-preview.js', out / 'agent-preview.js')
source = (root / 'ios/agent/LifeAgent.swift').read_text()
prompt = re.search(r'history=\[\["role":"system","content":(".*?")\]\]', source).group(1)
(out / 'agent-system.txt').write_text(json.loads(prompt))
key = root / 'tools/openai-api.key'
if not key.is_file():
    raise SystemExit('Missing local Agent credential file')
shutil.copy2(key, out / 'AgentCredential.key')
(out / 'research.html').write_text('''<!doctype html><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'self' 'unsafe-inline' 'unsafe-eval' blob: https://cdn.jsdelivr.net; worker-src blob:; connect-src https://cdn.jsdelivr.net">
<script>''' + (out / 'agent-preview.js').read_text() + '\n' + (out / 'agent-research.js').read_text() + '''
window.startResearch = function(id, code, documents, engine, language) {
  runResearch(code, documents, engine, language, frame => researchBridge.preview(id, JSON.stringify(frame))).then(value => {
    let text;
    try { text = JSON.stringify(value, (k,v) => { if(typeof v === 'number' && !Number.isFinite(v)) throw Error('nonfinite'); return v; }); }
    catch(e) { text = JSON.stringify({ok:false,error:'invalid_script_result'}); }
    researchBridge.finish(id, text);
  }).catch(e => researchBridge.finish(id, JSON.stringify({ok:false,error:String(e)})));
};
</script>''')
# Existing staged bundles may contain legacy OTCA templates. Remove those only.
bundle = out / 'Resources'
shutil.rmtree(bundle / 'AgentTemplates', ignore_errors=True)
manifest = bundle / 'manifest.json'
if manifest.exists():
    doc = json.loads(manifest.read_text())
    doc['files'] = [f for f in doc['files'] if not f['path'].startswith('AgentTemplates/')]
    manifest.write_text(json.dumps(doc, indent=2) + '\n')
