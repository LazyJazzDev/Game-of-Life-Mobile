import SwiftUI
import Security

struct AgentMessage: Identifiable {
  let id = UUID()
  let role: String
  var text: String
}

@MainActor final class LifeAgent: ObservableObject {
  @Published var messages: [AgentMessage] = []
  @Published var progress = ""
  @Published var busy = false
  @Published var key = ""
  @Published var consent = false
  weak var renderer: DemoRenderer?
  private var task: Task<Void, Never>?
  private var history: [[String: Any]] = []
  private var generation = 0
  private var toolLog: [[String:Any]] = []
  private let service = "net.lazyjazz.gameoflife.deepseek"
  private var chinese: Bool { Locale.preferredLanguages.first?.hasPrefix("zh") == true }
  func text(_ zh: String, _ en: String) -> String { chinese ? zh : en }

  init() {
    let query: [String: Any] = [kSecClass as String:kSecClassGenericPassword,
      kSecAttrService as String:service, kSecReturnData as String:true]
    var item: CFTypeRef?
    if SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
      let data = item as? Data { key = String(data:data,encoding:.utf8) ?? "" }
    #if targetEnvironment(simulator)
      // Local development provisioning only; never included in the application bundle.
      let file = FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("deepseek-api.key")
      if let value = try? String(contentsOf:file,encoding:.utf8) {
        key = value.trimmingCharacters(in:.whitespacesAndNewlines)
        _ = saveKey()
        try? FileManager.default.removeItem(at:file)
      }
    #endif
  }

  @discardableResult func saveKey() -> Bool {
    key = key.trimmingCharacters(in:.whitespacesAndNewlines)
    let query: [String:Any] = [kSecClass as String:kSecClassGenericPassword,kSecAttrService as String:service]
    SecItemDelete(query as CFDictionary)
    guard !key.isEmpty else { return true }
    var entry=query
    entry[kSecValueData as String]=Data(key.utf8)
    entry[kSecAttrAccessible as String]=kSecAttrAccessibleWhenUnlockedThisDeviceOnly
    return SecItemAdd(entry as CFDictionary,nil) == errSecSuccess
  }

  func cancel() {
    generation += 1
    task?.cancel(); task=nil; busy=false
    progress=text("已停止", "Stopped")
    // Do not reuse a partial tool-call chain on the next turn.
    history=[]
  }

  func send(_ input: String) {
    let input=input.trimmingCharacters(in:.whitespacesAndNewlines)
    guard !busy, !input.isEmpty, !key.isEmpty, consent else { return }
    messages.append(AgentMessage(role:"user",text:input));busy=true
    generation += 1
    let current=generation
    task=Task {
      var failed=false
      do { try await run(input) }
      catch is CancellationError { }
      catch {
        failed=true
        if current==generation {
          messages.append(AgentMessage(role:"assistant",text:text("请求未完成：", "Request failed: ")+error.localizedDescription))
          history=[]
        }
      }
      if current==generation {
        busy=false;progress=failed ? text("请求失败", "Request failed") : text("已完成", "Finished");task=nil
        #if targetEnvironment(simulator)
          if ProcessInfo.processInfo.environment["LONGMARCH_AGENT_SMOKE"] != nil {
            let file=FileManager.default.urls(for:.documentDirectory,in:.userDomainMask)[0].appendingPathComponent("AgentSmoke.json")
            let proof:[String:Any] = ["tools":toolLog,"messages":messages.map{["role":$0.role,"text":$0.text]}]
            try? JSONSerialization.data(withJSONObject:proof,options:.prettyPrinted).write(to:file)
          }
        #endif
      }
    }
  }

  private func run(_ input: String) async throws {
    if history.isEmpty {
      history=[["role":"system","content":"You are the Life Agent inside Conway's Game of Life (B3/S23). Help design and test structures using tools. Reply in the user's language. Do not claim actions without successful tool results. Use catalog patterns rather than inventing known layouts. For repeated or large structures, save RLE templates, compose named templates with repetition, and place the whole compound once. Template anchors are TOP-LEFT; pattern_place is CENTER. Inspect canvas_summary before writing and compare its population/hash with template_info after placement. Use canvas_fit to show a complete region; never guess a zoom. Enlarged drawings are not functioning metacells. Coordinates: center origin, x right, y down. Read before editing; never reset or randomize unless requested. Destructive actions require user confirmation. Preserve unrelated cells. One tool at a time. Step tests are finite observations, not proofs. The app has 16384x16384 cells; large metacells may not fit. Web links are references only. Keep replies concise. Stop after confirmation is requested and wait for the user."]]
    }
    history.append(["role":"user","content":input])
    let url=Bundle.main.url(forResource:"agent-tools",withExtension:"json")!
    let tools=try JSONSerialization.jsonObject(with:Data(contentsOf:url))
    var toolCount=0
    for round in 0..<10 {
      try Task.checkCancellation()
      progress=text("正在思考", "Thinking")+" · \(round+1)/10"
      let response=try await completion(tools)
      try Task.checkCancellation()
      history.append(response)
      if let content=response["content"] as? String, !content.isEmpty {
        messages.append(AgentMessage(role:"assistant",text:content))
      }
      guard let calls=response["tool_calls"] as? [[String:Any]], !calls.isEmpty else { return }
      for call in calls {
        toolCount += 1
        guard toolCount <= 24 else { throw AgentFailure.truncated }
        try Task.checkCancellation()
        guard let id=call["id"] as? String, let function=call["function"] as? [String:Any],
          let name=function["name"] as? String, let raw=function["arguments"] as? String,
          let data=raw.data(using:.utf8) else { throw AgentFailure.invalidResponse }
        progress=toolLabel(name)
        let result: String
        do {
          guard let args=try JSONSerialization.jsonObject(with:data) as? [String:Any] else { throw AgentFailure.invalidResponse }
          result=try await execute(name,args)
        } catch { result="{\"ok\":false,\"error\":\"invalid_arguments_or_unavailable\"}" }
        try Task.checkCancellation()
        toolLog.append(["name":name,"arguments":raw,"result":result])
        history.append(["role":"tool","tool_call_id":id,"content":result])
      }
    }
    messages.append(AgentMessage(role:"assistant",text:text("已达到本轮操作上限。可以继续描述下一步。", "This turn reached its action limit. Tell me what to do next.")))
    history=[]
  }

  private func toolLabel(_ name:String)->String {
    switch name {
    case "pattern_search":return text("正在查找已知结构…", "Finding known patterns…")
    case "template_save","template_compose":return text("正在保存和组合图样模板…", "Saving and composing templates…")
    case "template_list","template_info":return text("正在查看图样模板…", "Inspecting templates…")
    case "canvas_summary":return text("正在核验画布结构…", "Verifying canvas structure…")
    case "canvas_fit":return text("正在适配结构视野…", "Fitting the structure…")
    case "template_place","pattern_place","canvas_edit":return text("正在绘制细胞结构…", "Editing cells…")
    case "canvas_read","canvas_state":return text("正在读取画布…", "Reading canvas…")
    case "canvas_control":return text("正在执行画布操作…", "Applying canvas operation…")
    default:return text("正在查询研究资料…", "Looking up research sources…")
    }
  }

  private func completion(_ tools:Any) async throws -> [String:Any] {
    var request=URLRequest(url:URL(string:"https://api.deepseek.com/chat/completions")!)
    request.httpMethod="POST";request.timeoutInterval=120
    request.setValue("Bearer \(key)",forHTTPHeaderField:"Authorization")
    request.setValue("application/json",forHTTPHeaderField:"Content-Type")
    let historySize=try JSONSerialization.data(withJSONObject:history).count
    guard historySize <= 512*1024 else { history=[];throw AgentFailure.truncated }
    request.httpBody=try JSONSerialization.data(withJSONObject:["model":"deepseek-flash","messages":history,"tools":tools,
      "thinking":["type":"enabled"],"reasoning_effort":"high","max_tokens":4096,"stream":true])
    let (bytes,response)=try await URLSession.shared.bytes(for:request)
    guard let http=response as? HTTPURLResponse, http.statusCode==200 else {
      throw AgentFailure.http((response as? HTTPURLResponse)?.statusCode ?? 0)
    }
    var content="",reasoning=""
    var calls: [Int:[String:Any]]=[:]
    var finished=false
    for try await line in bytes.lines {
      try Task.checkCancellation()
      guard line.hasPrefix("data: ") else { continue }
      let raw=String(line.dropFirst(6))
      if raw=="[DONE]" { finished=true;break }
      guard let data=raw.data(using:.utf8),let event=try JSONSerialization.jsonObject(with:data) as? [String:Any],
        let choices=event["choices"] as? [[String:Any]],let choice=choices.first,
        let delta=choice["delta"] as? [String:Any] else { continue }
      if let reason=delta["reasoning_content"] as? String { reasoning+=reason;progress=text("正在分析结构与操作步骤…", "Analyzing the structure and next steps…") }
      if let part=delta["content"] as? String { content+=part;progress=text("正在整理回复…", "Preparing reply…") }
      if let changes=delta["tool_calls"] as? [[String:Any]] {
        for change in changes {
          let index=change["index"] as? Int ?? 0
          var call=calls[index] ?? ["type":"function","function":["name":"","arguments":""]]
          if let id=change["id"] { call["id"]=id }
          var fn=call["function"] as! [String:String]
          if let f=change["function"] as? [String:Any] {
            if let n=f["name"] as? String { fn["name",default:""]+=n }
            if let a=f["arguments"] as? String { fn["arguments",default:""]+=a }
          }
          call["function"]=fn;calls[index]=call
        }
      }
      if let reason=choice["finish_reason"] as? String, reason=="length" { throw AgentFailure.truncated }
      if content.utf8.count+reasoning.utf8.count > 256*1024 { throw AgentFailure.truncated }
    }
    guard finished else { throw AgentFailure.invalidResponse }
    var result: [String:Any] = ["role":"assistant","content":content,"reasoning_content":reasoning]
    if !calls.isEmpty { result["tool_calls"]=calls.keys.sorted().compactMap { calls[$0] } }
    return result
  }

  private func native(_ args:[String:Any]) async throws -> String {
    try Task.checkCancellation()
    guard let renderer else { throw AgentFailure.canvas }
    let data=try JSONSerialization.data(withJSONObject:args)
    return await withCheckedContinuation { continuation in
      renderer.agentCommand(String(decoding:data,as:UTF8.self)) { continuation.resume(returning:$0) }
    }
  }

  private func execute(_ name:String,_ args:[String:Any]) async throws -> String {
    if name=="knowledge_sources" {
      return "Life Lexicon: https://github.com/dvgrn/life-lexicon ; LifeWiki: https://conwaylife.com/wiki/Main_Page ; metapixels: https://conwaylife.com/wiki/OTCA_metapixel ; Golly: https://golly.sourceforge.io/ . These links have not been fetched."
    }
    if name=="pattern_search" {
      let query=(args["query"] as? String ?? "").lowercased()
      let matches=BuiltinPattern.all.filter { $0.name.lowercased().contains(query) || $0.category.lowercased().contains(query) }.prefix(12)
      let rows=matches.map { ["name":$0.name,"width":$0.width,"height":$0.height,"category":$0.category] as [String:Any] }
      return String(decoding:try JSONSerialization.data(withJSONObject:rows),as:UTF8.self)
    }
    if name=="pattern_place" {
      guard let name=args["name"] as? String, let p=BuiltinPattern.all.first(where:{$0.name==name}),
        let x=args["x"] as? Int, let y=args["y"] as? Int, x >= -8192,x <= 8191,y >= -8192,y <= 8191,p.width*p.height<=4096 else { throw AgentFailure.invalidResponse }
      let lines=p.cells.components(separatedBy:"\n").filter{!$0.hasPrefix("!")}.prefix(p.height)
      var cells:[[Int]]=[]
      for (dy,row) in lines.enumerated() { for (dx,c) in row.enumerated() {
        cells.append([x-p.width/2+dx,y-p.height/2+dy,c=="O" ? 1 : 0])
      } }
      return try await native(["action":"edit","cells":cells])
    }
    var payload=args
    switch name {
    case "canvas_state":payload=["action":"state"]
    case "canvas_control":break
    case "canvas_read":payload["action"]="read"
    case "canvas_edit":payload["action"]="edit"
    case "canvas_summary":payload["action"]="summary"
    case "canvas_fit":payload["action"]="fit"
    case "metacell_guide","template_save","template_list","template_info","template_compose","template_place":payload["action"]=name
    default:throw AgentFailure.invalidResponse
    }
    return try await native(payload)
  }
}

enum AgentFailure: LocalizedError {
  case http(Int),invalidResponse,truncated,canvas
  var errorDescription:String? {
    let zh=Locale.preferredLanguages.first?.hasPrefix("zh") == true
    switch self {
    case .http(let code):return zh ? "服务请求失败（\(code)），请检查密钥、余额或网络。" : "API request failed (\(code)). Check your key, balance and connection."
    case .invalidResponse:return zh ? "回复中断或格式无效，请重试。" : "Incomplete or invalid response. Please retry."
    case .truncated:return zh ? "回复超过本轮限制，未执行不完整的操作。" : "Response exceeded this turn's limit. Incomplete actions were not executed."
    case .canvas:return zh ? "画布尚未就绪。" : "Canvas is not ready."
    }
  }
}

struct AgentPanel: View {
  @ObservedObject var agent:LifeAgent
  @State private var input=""
  @State private var settings=false
  @FocusState private var focused:Bool
  var body:some View {
    VStack(spacing:12) {
      if agent.busy {
        HStack(spacing:8) { ProgressView(); Text(agent.progress).font(.footnote).contentTransition(.numericText()) }
          .padding(.horizontal,16).padding(.vertical,10).background(.ultraThinMaterial,in:Capsule())
          .accessibilityLabel(agent.progress)
      }
      VStack(spacing:0) {
        HStack {
          Text("Life Agent").font(.headline)
          Spacer()
          Button { settings.toggle() } label:{Image(systemName:"key")}.accessibilityLabel(agent.text("API 设置","API settings"))
        }.padding()
        ScrollViewReader { reader in
          ScrollView {
            LazyVStack(alignment:.leading,spacing:16) {
              if agent.messages.isEmpty { Text(agent.text("试试：放置一艘滑翔机，运行四代，看看它移动到了哪里。","Try placing a glider, advancing four generations, and checking where it moved.")).foregroundStyle(.secondary) }
              ForEach(agent.messages) { message in
                VStack(alignment:.leading,spacing:4) {
                  Text(message.role=="user" ? agent.text("你","You") : "Life Agent").font(.caption).foregroundStyle(.secondary)
                  Text(message.text).textSelection(.enabled)
                }.id(message.id)
              }
            }.padding()
          }.onChange(of:agent.messages.count) { _,_ in if let id=agent.messages.last?.id { reader.scrollTo(id,anchor:.bottom) } }
        }
        Divider()
        HStack(alignment:.bottom) {
          TextField(agent.text("描述你想探索的结构…","Describe what you want to explore…"),text:$input,axis:.vertical)
            .lineLimit(1...4).focused($focused).submitLabel(.send).onSubmit { send() }
          if agent.busy {
            Button { agent.cancel() } label:{Image(systemName:"stop.circle.fill").font(.title2)}
              .accessibilityLabel(agent.text("停止","Stop"))
          } else {
            Button(action:send) { Image(systemName:"arrow.up.circle.fill").font(.title2) }
              .disabled(input.trimmingCharacters(in:.whitespacesAndNewlines).isEmpty || agent.key.isEmpty || !agent.consent)
              .accessibilityLabel(agent.text("发送","Send"))
          }
        }.padding()
      }.background(.regularMaterial,in:RoundedRectangle(cornerRadius:24))
    }.padding(12)
      .onAppear { if agent.key.isEmpty || !agent.consent { settings=true } else { focused=true } }
      .sheet(isPresented:$settings) {
        NavigationStack {
          Form {
            SecureField("DeepSeek API Key",text:$agent.key).textInputAutocapitalization(.never).autocorrectionDisabled()
            Toggle(agent.text("允许将消息及必要的画布数据发送给 DeepSeek","Allow sending messages and necessary canvas data to DeepSeek"),isOn:$agent.consent)
            Text(agent.text("仅在使用助手时联网。密钥保存在本机钥匙串；不会上传完整画布。","Only the assistant uses the network. Your key stays in this device's Keychain; the full canvas is not uploaded.")).font(.footnote)
          }.navigationTitle(agent.text("助手设置","Agent settings"))
            .toolbar { Button(agent.text("完成","Done")) { if agent.saveKey() { settings=false } } }
        }.presentationDetents([.medium])
      }
  }
  private func send() { let value=input;input="";focused=false;agent.send(value) }
}

struct AgentLauncher:View {
  let title:String
  let action:()->Void
  var body:some View {
    Button(action:action) {
      HStack {Image(systemName:"sparkles");Text(title).foregroundStyle(.secondary);Spacer();Image(systemName:"bubble.left.and.text.bubble.right")}
        .padding(.horizontal,18).frame(height:48)
    }.buttonStyle(.plain).modifier(AgentGlass())
  }
}
struct AgentGlass:ViewModifier {
  func body(content:Content)->some View {
    if #available(iOS 26.0,*) { content.glassEffect(.regular.interactive(),in:.capsule) }
    else { content.background(.ultraThinMaterial,in:Capsule()) }
  }
}
