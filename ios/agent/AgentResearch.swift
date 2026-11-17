import Foundation
import WebKit

// General research workspace: no patterns, domain recipes, filesystem or credentials.
@MainActor final class AgentResearch: NSObject, WKNavigationDelegate, WKScriptMessageHandler {
  private var documents: [String:String] = [:]
  private var sources: [String:String] = [:]
  let web: WKWebView
  var visualChanged: ([String:Any]) -> Void = { _ in }
  private var setup: Task<Void, Error>?
  private var visualGeneration = 0

  override init() {
    let configuration=WKWebViewConfiguration();configuration.websiteDataStore = .nonPersistent()
    web=WKWebView(frame:.zero,configuration:configuration)
    super.init()
    web.navigationDelegate=self;web.isOpaque=false;web.backgroundColor = .clear
    web.scrollView.isScrollEnabled=false
    web.configuration.userContentController.add(self,name:"preview")
  }
  func userContentController(_ userContentController:WKUserContentController,didReceive message:WKScriptMessage) {
    if message.name == "preview", let body=message.body as? [String:Any], body["token"] as? Int == visualGeneration,
      let frame=body["frame"] as? [String:Any] { visualChanged(frame) }
  }
  func prepare() async throws {
    if let setup { try await setup.value;return }
    let task=Task { @MainActor in
      try await withCheckedThrowingContinuation { (continuation:CheckedContinuation<Void,Error>) in
        ready=continuation
        web.loadHTMLString("<html><head><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='Content-Security-Policy' content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline' 'unsafe-eval' blob: https://cdn.jsdelivr.net; worker-src blob:; connect-src https://cdn.jsdelivr.net\"></head><body></body></html>",baseURL:nil)
      }
      let visual=try String(contentsOf:Bundle.main.bundleURL.appendingPathComponent("agent-preview.js"),encoding:.utf8)
      _ = try await web.evaluateJavaScript(visual)
    }
    setup=task
    try await task.value
  }
  func hide() {
    visualGeneration += 1
    web.evaluateJavaScript("window.cancelResearchRuns?.();",completionHandler:nil)
  }
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

  func search(_ query:String) async throws -> [String:Any] {
    guard !query.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty, query.count<=240 else { return ["ok":false,"error":"invalid_query"] }
    var url=URLComponents(string:"https://api.github.com/search/repositories")!
    url.queryItems=[URLQueryItem(name:"q",value:query),URLQueryItem(name:"per_page",value:"10")]
    let fetched=try await fetch(url.url!.absoluteString)
    guard fetched["ok"] as? Bool == true, let id=fetched["document"] as? String,
      let raw=documents[id], let data=raw.data(using:.utf8),
      let listing=try JSONSerialization.jsonObject(with:data) as? [String:Any],
      let items=listing["items"] as? [[String:Any]] else { return fetched }
    let matches=items.map { item in
      ["name":item["full_name"] as? String ?? "", "description":item["description"] as? String ?? "",
       "url":item["html_url"] as? String ?? "", "api_url":item["url"] as? String ?? "",
       "default_branch":item["default_branch"] as? String ?? ""]
    }
    return ["ok":true,"document":id,"matches":matches,"total_count":listing["total_count"] as? Int ?? 0,"untrusted_source":true]
  }

  func read(_ id:String,_ offset:Int,_ length:Int) -> [String:Any] {
    guard let text=documents[id], offset>=0 else { return ["ok":false,"error":"unknown_document_or_offset"] }
    let chars=Array(text), start=min(offset,chars.count), end=min(chars.count,start+max(1,min(length,24000)))
    return ["ok":true,"document":id,"url":sources[id] ?? "generated","length":chars.count,"offset":start,
      "next_offset":end,"content":String(chars[start..<end]),"untrusted_source":true]
  }

  func find(_ query:String, _ document:String = "") -> [String:Any] {
    guard !query.isEmpty, query.count <= 160 else { return ["ok":false,"error":"invalid_query"] }
    if !document.isEmpty, documents[document] == nil { return ["ok":false,"error":"unknown_document"] }
    let ids=document.isEmpty ? documents.keys.sorted() : [document]
    var matches:[[String:Any]]=[]
    for id in ids {
      guard let text=documents[id] else { continue }
      var start=text.startIndex
      while start<text.endIndex, let hit=text.range(of:query,options:.caseInsensitive,range:start..<text.endIndex) {
        let low=text.index(hit.lowerBound,offsetBy:-100,limitedBy:text.startIndex) ?? text.startIndex
        let high=text.index(hit.upperBound,offsetBy:300,limitedBy:text.endIndex) ?? text.endIndex
        matches.append(["document":id,"offset":text.distance(from:text.startIndex,to:low),"content":String(text[low..<high])])
        if matches.count>=20 { return ["ok":true,"matches":matches,"more_possible":true,"untrusted_source":true] }
        start=hit.upperBound
      }
    }
    return ["ok":true,"matches":matches,"more_possible":false,"untrusted_source":true]
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
    let token=visualGeneration
    try await prepare();try Task.checkCancellation()
    guard token==visualGeneration else { throw CancellationError() }
    // A disposable worker supplies a hard execution deadline without blocking UI.
    let base=Bundle.main.bundleURL.appendingPathComponent("hashlife")
    let engine=try String(contentsOf:base.appendingPathComponent("life.js"),encoding:.utf8)+"\n"+String(contentsOf:base.appendingPathComponent("adapter.js"),encoding:.utf8)
    let body=try String(contentsOf:Bundle.main.bundleURL.appendingPathComponent("agent-research.js"),encoding:.utf8)+"\nreturn await runResearch(code, documents, engine, language, frame=>window.webkit.messageHandlers.preview.postMessage({token,frame}));"
    let result=try await web.callAsyncJavaScript(body,arguments:["code":code,"documents":documents,"engine":engine,"language":language,"token":token],in:nil,contentWorld:.page)
    guard let object=result as? [String:Any] else { throw AgentFailure.invalidResponse }
    guard JSONSerialization.isValidJSONObject(object) else {
      return ["ok":false,"error":"invalid_script_result","detail":"Return a JSON-compatible object with finite numbers; NaN, Infinity, functions and cyclic objects are not supported."]
    }
    return object
  }
}
