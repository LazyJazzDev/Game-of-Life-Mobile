globalThis.agentResearchRuns ??= new Set();
globalThis.cancelResearchRuns = () => { for(const cancel of [...globalThis.agentResearchRuns]) cancel(); };
// Shared disposable research worker. No patterns, credentials or construction recipes.
async function runResearch(code, documents, engine, language, onPreview) {
return await new Promise((resolve,reject)=>{
  const visual = typeof agentPreviewFrame === 'function' && onPreview ? agentPreviewFrame.toString() : '';
  const source = engine + '\n' + visual + `\nconst originalFetch=self.fetch, originalImport=self.importScripts; self.fetch=undefined; self.XMLHttpRequest=undefined; self.WebSocket=undefined; self.importScripts=undefined;
  let previewAt=0;
  if(typeof agentPreviewFrame==='function') {
    const encode=life.encodeRLE.bind(life);
    const emit=(cells,generation,phase)=>{const now=Date.now();if(phase==='structure'&&now-previewAt<180)return;previewAt=now;
      const frame=agentPreviewFrame(cells,generation,phase,encode);if(frame)self.postMessage({preview:frame});};
    life.preview=emit;
    for(const name of ['parseRLE','encodeRLE']) {const original=life[name];life[name]=function(value){const result=original.call(life,value);emit(name==='parseRLE'?result:value,0,'structure');return result;};}
  }
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
  let output='',settled=false,result=null,playbackTimer=null;
  const frames=[];let queuedBytes=0;
  const finish=value=>{
    if(settled)return;settled=true;clearTimeout(timer);clearTimeout(playbackTimer);
    frames.length=0;stop();globalThis.agentResearchRuns.delete(cancel);resolve({...value,output});
  };
  const cancel=()=>finish({ok:false,error:'cancelled'});
  globalThis.agentResearchRuns.add(cancel);
  const timer=setTimeout(()=>finish({ok:false,error:'execution_timeout'}),60000);
  // The worker may finish in milliseconds. Drain exact snapshots at a visible
  // cadence before returning its result, so the next tool cannot erase them.
  const play=()=>{
    playbackTimer=null;
    if(settled)return;
    if(frames.length){
      const frame=frames.shift();queuedBytes-=frame.rle.length;
      try{onPreview?.(frame);}catch(_){}
      playbackTimer=setTimeout(play,300);
    }else if(result)finish(result);
  };
  worker.onmessage=e=>{
    if(settled)return;
    if(e.data.preview){
      const frame=e.data.preview;
      frames.push(frame);queuedBytes+=frame.rle.length;
      // Coalesce older queued work, but retain the newest exact state. Bound
      // retained RLE strings as well as frame count for large research scenes.
      while(frames.length>12 || (queuedBytes>8*1024*1024 && frames.length>1))queuedBytes-=frames.shift().rle.length;
      if(playbackTimer===null)play();
      return;
    }
    if(typeof e.data.log==='string'){output=(output+e.data.log+String.fromCharCode(10)).slice(-16000);return;}
    clearTimeout(timer);stop();result=e.data;
    if(!result.ok){finish(result);return;}
    if(playbackTimer===null)play();
  };
  worker.onerror=e=>finish({ok:false,error:e.message});
  worker.postMessage({code,documents,language});
});
}
