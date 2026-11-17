import Foundation
import WebKit

// General research workspace: no patterns, domain recipes, filesystem or credentials.
@MainActor final class AgentResearch: NSObject, WKNavigationDelegate {
  private var documents: [String:String] = [:]
  private var sources: [String:String] = [:]
  private var web: WKWebView?
  private var ready: CheckedContinuation<Void, Error>?

  func fetch(_ text: String) async throws -> [String:Any] {
    guard var parts = URLComponents(string:text), parts.host != nil else { throw AgentFailure.invalidResponse }
    if parts.scheme == "http" { parts.scheme = "https" }
    guard parts.scheme == "https", parts.user == nil, parts.password == nil,
      let host=parts.host, host != "localhost", !host.hasPrefix("127."), !host.hasPrefix("192.168."), !host.hasPrefix("10."),
      let url=parts.url else { throw AgentFailure.invalidResponse }
    var request=URLRequest(url:url); request.timeoutInterval=45
    request.setValue("LifeResearch/1.0",forHTTPHeaderField:"User-Agent")
    let (bytes,response)=try await URLSession.shared.bytes(for:request)
    guard let http=response as? HTTPURLResponse else { throw AgentFailure.invalidResponse }
    guard http.statusCode == 200 else { return ["ok":false,"status":http.statusCode,"url":url.absoluteString] }
    var data=Data()
    for try await byte in bytes {
      try Task.checkCancellation()
      guard data.count < 4*1024*1024 else { throw AgentFailure.truncated }
      data.append(byte)
    }
    guard let content=String(data:data,encoding:.utf8) else { throw AgentFailure.invalidResponse }
    let id="doc\(documents.count+1)"; documents[id]=content; sources[id]=response.url?.absoluteString ?? text
    return read(id,0,12000)
  }

  func read(_ id:String,_ offset:Int,_ length:Int) -> [String:Any] {
    guard let text=documents[id], offset>=0 else { return ["ok":false,"error":"unknown_document_or_offset"] }
    let chars=Array(text), start=min(offset,chars.count), end=min(chars.count,start+max(1,min(length,24000)))
    return ["ok":true,"document":id,"url":sources[id] ?? "generated","length":chars.count,"offset":start,
      "next_offset":end,"content":String(chars[start..<end]),"untrusted_source":true]
  }

  func content(_ id:String) -> String? { documents[id] }

  func write(_ id:String,_ content:String) -> [String:Any] {
    guard id.range(of:"^[A-Za-z][A-Za-z0-9_-]{0,63}$",options:.regularExpression) != nil,
      content.utf8.count <= 1024*1024 else { return ["ok":false,"error":"invalid_artifact"] }
    let name="work_"+id; documents[name]=content; sources[name]="Agent-authored workspace";
    return ["ok":true,"document":name,"length":content.count]
  }


  func webView(_ webView:WKWebView,didFinish navigation:WKNavigation!) { ready?.resume(); ready=nil }
  func webView(_ webView:WKWebView,didFailProvisionalNavigation navigation:WKNavigation!,withError error:Error) {
    ready?.resume(throwing:error); ready=nil
  }

  func script(_ code:String, language:String = "javascript") async throws -> [String:Any] {
    if web == nil {
      let configuration=WKWebViewConfiguration(); configuration.websiteDataStore = .nonPersistent()
      let view=WKWebView(frame:.zero,configuration:configuration); view.navigationDelegate=self; web=view
      try await withCheckedThrowingContinuation { (continuation:CheckedContinuation<Void,Error>) in
        ready=continuation
        view.loadHTMLString("<html><head><meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; script-src 'unsafe-inline' 'unsafe-eval' blob: https://cdn.jsdelivr.net; worker-src blob:; connect-src https://cdn.jsdelivr.net\"></head><body></body></html>",baseURL:nil)
      }
    }
    guard let web else { throw AgentFailure.invalidResponse }
    // A disposable worker supplies a hard execution deadline without blocking UI.
    let base=Bundle.main.bundleURL.appendingPathComponent("hashlife")
    let engine=try String(contentsOf:base.appendingPathComponent("life.js"),encoding:.utf8)+"\n"+String(contentsOf:base.appendingPathComponent("adapter.js"),encoding:.utf8)
    let body="""
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
    """
    let result=try await web.callAsyncJavaScript(body,arguments:["code":code,"documents":documents,"engine":engine,"language":language],in:nil,contentWorld:.page)
    guard let object=result as? [String:Any] else { throw AgentFailure.invalidResponse }
    guard JSONSerialization.isValidJSONObject(object) else {
      return ["ok":false,"error":"invalid_script_result","detail":"Return a JSON-compatible object with finite numbers; NaN, Infinity, functions and cyclic objects are not supported."]
    }
    return object
  }
}
