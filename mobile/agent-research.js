// Shared disposable research worker. No patterns, credentials or construction recipes.
async function runResearch(code, documents, engine, language) {
return await new Promise((resolve,reject)=>{
  const source = engine + `\nconst originalFetch=self.fetch, originalImport=self.importScripts; self.fetch=undefined; self.XMLHttpRequest=undefined; self.WebSocket=undefined; self.importScripts=undefined;
  const emitLog=text=>self.postMessage({log:String(text)});
  console.log=(...parts)=>emitLog(parts.map(String).join(' '));
  self.onmessage=async e=>{try {
    let result;
    if(e.data.language==='python') {
      self.fetch=originalFetch; self.importScripts=originalImport;
      importScripts('https://cdn.jsdelivr.net/pyodide/v0.27.7/full/pyodide.js');
      const py=await loadPyodide({indexURL:'https://cdn.jsdelivr.net/pyodide/v0.27.7/full/'});
      self.fetch=undefined; self.importScripts=undefined;
      py.setStdout({batched:emitLog});py.setStderr({batched:emitLog});
      py.globals.set('documents',py.toPy(e.data.documents));
      py.globals.set('_evolve', (cells,n)=>JSON.stringify(life.evolve(cells.toJs ? cells.toJs() : cells,n)));
      py.globals.set('_parse_rle', text=>JSON.stringify(life.parseRLE(text)));
      py.globals.set('_encode_rle', cells=>JSON.stringify(life.encodeRLE(cells.toJs ? cells.toJs() : cells)));
      py.runPython([
        'def _make_life_helpers(_e, _p, _c):',
        '    import json as _json',
        '    from builtins import list as _list',
        '    def evolve(cells, generations):',
        '        return _json.loads(_e([_list(p) for p in cells], generations))',
        '    def parse(text):',
        '        return _json.loads(_p(text))',
        '    def encode(cells):',
        '        return _json.loads(_c([_list(p) for p in cells]))',
        '    return evolve, parse, encode',
        'life_evolve, life_parse_rle, life_encode_rle = _make_life_helpers(_evolve, _parse_rle, _encode_rle)',
        'del _make_life_helpers'
      ].join(String.fromCharCode(10)));
      await py.runPythonAsync(e.data.code);
      const value=py.globals.get('result');
      result=value && value.toJs ? value.toJs({dict_converter:Object.fromEntries}) : value;
      if(value && value.destroy)value.destroy();
    } else { result=new Function('documents','life',e.data.code)(e.data.documents,life); }
    self.postMessage({ok:true,value:result});
  } catch(err){self.postMessage({ok:false,error:String(err),stack:err.stack});}}`;
  const url=URL.createObjectURL(new Blob([source],{type:'text/javascript'}));
  const worker=new Worker(url);
  const stop=()=>{worker.terminate();URL.revokeObjectURL(url);};
  let output='';
  const finish=value=>{clearTimeout(timer);stop();resolve({...value,output});};
  const timer=setTimeout(()=>finish({ok:false,error:'execution_timeout'}),60000);
  worker.onmessage=e=>{
    if(typeof e.data.log==='string'){output=(output+e.data.log+String.fromCharCode(10)).slice(-16000);return;}
    finish(e.data);
  };
  worker.onerror=e=>finish({ok:false,error:e.message});
  worker.postMessage({code,documents,language});
});
}
