import { http } from '@kit.NetworkKit';
import { util } from '@kit.ArkTS';

export interface TemplatePart { name: string; x: number; y: number; rotation?: number; mirror?: number; columns?: number; rows?: number; dx?: number; dy?: number; }
export interface AgentArgs {
  work_brief?: string;
  summary?: string; evidence?: string[]; question?: string; reason?: string;
  url?: string; document?: string; offset?: number; length?: number; content?: string; code?: string; language?: string;
  rle?: string; parts?: TemplatePart[]; mode?: string;
  action?: string; value?: number; x?: number; y?: number;
  width?: number; height?: number; cells?: number[][]; query?: string; name?: string;
}
interface ToolFunction { name: string; arguments: string; }
interface ToolCall { id: string; type: string; function: ToolFunction; }
interface Message {
  role: string; content: string; response_items?: ResponseItem[];
  tool_calls?: ToolCall[]; tool_call_id?: string;
}
interface ResponseContent { type: string; text?: string; refusal?: string; }
interface ResponseItem {
  type?: string; id?: string; role?: string; content?: string | ResponseContent[];
  call_id?: string; name?: string; arguments?: string; output?: string;
  created_by?: Object;
}
interface ResponsePayload { status: string; output?: ResponseItem[]; incomplete_details?: IncompleteDetails; }
interface IncompleteDetails { reason: string; }
interface ResponseEvent { type: string; delta?: string; item?: ResponseItem; response?: ResponsePayload; }
interface FunctionDefinition { name: string; description?: string; parameters: Object; }
interface ToolDefinition { type: string; function: FunctionDefinition; }
interface ResponseTool { type: string; name: string; description?: string; parameters: Object; strict: boolean; }
interface RequestBody {
  model: string; input: ResponseItem[]; tools: ResponseTool[];
  reasoning: Reasoning; max_output_tokens: number; stream: boolean; store: boolean;
  include: string[]; parallel_tool_calls: boolean;
}
interface Reasoning { effort: string; }
export interface AgentTrace { name: string; arguments: string; result: string; }
export interface AgentLine { role: string; text: string; }

export interface AgentActivity { title: string; detail: string; started: number; finished: boolean; failed: boolean; }
export class LifeAgent {
  key: string = '';
  readonly model: string = 'gpt-6.1-sol';
  systemPrompt: string = '';
  liveResponse: string = '';
  activities: AgentActivity[] = [];
  cancelResearch: () => void = () => {};
  private ticker: number = -1;
  private updated: number = 0;
  private endStream?: () => void;
  busy: boolean = false;
  consent: boolean = false;
  lines: AgentLine[] = [];
  trace: AgentTrace[] = [];
  progress: string = '';
  changed: () => void = () => {};
  execute: (name: string, args: AgentArgs) => Promise<string> = async () => '{"ok":false}';
  tools: Object[] = [];
  private history: Message[] = [];
  private request?: http.HttpRequest;
  private epoch: number = 0;
  zh: boolean = true;
  text(zh: string, en: string): string { return this.zh ? zh : en; }
  private recoverHistory(): void {
    let index = 0;
    while (index < this.history.length) {
      const calls = this.history[index].tool_calls;
      if (!calls?.length) { index++; continue; }
      const pending = new Set<string>(calls.map((call: ToolCall) => call.id));
      let next = index + 1;
      while (next < this.history.length && this.history[next].role === 'tool') {
        const id = this.history[next].tool_call_id;
        if (id) pending.delete(id);
        next++;
      }
      if (pending.size) {
        this.history = this.history.slice(0, index);
        this.history.push({role: 'user', content: 'The previous run was interrupted during a tool batch. The original request and earlier completed work remain in this conversation. Some interrupted actions may already have taken effect; inspect current state before repeating any edits. Retained research documents remain available.'});
        return;
      }
      index = next;
    }
  }
  cancel(): void {
    const wasBusy = this.busy;
    this.epoch++; this.endStream?.(); this.endStream = undefined; this.cancelResearch();
    if (this.liveResponse.trim()) this.lines.push({role: 'assistant', text: this.liveResponse});
    clearInterval(this.ticker); this.liveResponse = '';
    this.request?.destroy(); this.request = undefined;
    this.busy = false; this.recoverHistory();
    if (wasBusy) this.progress = this.text('已停止', 'Stopped'); this.changed();
  }
  private updateBrief(value: string): void {
    const sentence = value.trim().split(/[。！？!?\n]/)[0].trim();
    if (sentence) this.progress = sentence.slice(0, 160);
  }
  private reportBrief(value: string): void {
    const brief = value.trim();
    this.updateBrief(brief);
    const last = this.lines[this.lines.length-1];
    if (!last || last.role !== 'assistant' || last.text.trim() !== brief) this.lines.push({role: 'assistant', text: brief});
    this.changed();
  }
  private toolProgress(name: string, args: AgentArgs): string {
    if (name === 'research_fetch') return this.text('正在读取公开资料…', 'Reading public sources…');
    if (name === 'research_read') return this.text('正在阅读研究文档…', 'Reading research documents…');
    if (name === 'research_write') return this.text('正在保存研究程序…', 'Saving research program…');
    if (name === 'research_script') return (args.code ?? '').includes('life_evolve') || (args.code ?? '').includes('life.evolve') ? this.text('正在演化并验证结构…', 'Evolving and verifying the structure…') : this.text('正在运行研究程序…', 'Running a research program…');
    if (name === 'template_save' || name === 'template_compose') return this.text('正在保存和组合图样模板…', 'Saving and composing templates…');
    if (name === 'template_place') return this.text('正在批量放置完整模板…', 'Placing the complete template…');
    if (name === 'canvas_summary') return this.text('正在核验画布结构…', 'Verifying canvas structure…');
    if (name === 'canvas_fit') return this.text('正在适配结构视野…', 'Fitting the structure…');
    if (name === 'pattern_search') return this.text('正在查找已知结构…', 'Finding known structures…');
    if (name === 'pattern_place') return this.text('正在放置结构：', 'Placing pattern: ') + (args.name ?? '');
    if (name === 'canvas_state') return this.text('正在检查画布状态…', 'Checking canvas state…');
    if (name === 'canvas_read') return this.text('正在读取区域细胞…', 'Reading cells in the region…');
    if (name === 'canvas_edit') return this.text('正在修改细胞布局…', 'Editing cell layout…');
    if (name === 'canvas_control') {
      if (args.action === 'step') return this.text(`正在推进 ${args.value} 代…`, `Advancing ${args.value} generations…`);
      if (args.action === 'view') return this.text('正在调整视野…', 'Adjusting the view…');
      if (args.action === 'speed') return this.text('正在调整演化速度…', 'Changing simulation speed…');
      if (args.action === 'reset' || args.action === 'randomize') return this.text('等待你确认画布操作…', 'Waiting for your confirmation…');
      if (args.action === 'play') return args.value ? this.text('正在开始模拟…', 'Starting simulation…') : this.text('正在暂停模拟…', 'Pausing simulation…');
    }
    return this.text('正在查询研究资料…', 'Looking up research sources…');
  }
  private activity(title: string): void {
    this.activities.push({ title: title, detail: '', started: Date.now(), finished: false, failed: false });
    if (this.activities.length > 12) this.activities.shift();
    this.changed();
  }
  private finishActivity(error: string = ''): void {
    const item = this.activities[this.activities.length-1];
    if (item) { item.finished = true; item.failed = error.length > 0; if (error) item.detail = error.slice(0, 240); }
    this.changed();
  }
  private responseInput(): ResponseItem[] {
    const input: ResponseItem[] = [];
    for (const message of this.history) {
      if (message.response_items) input.push(...message.response_items);
      else if (message.role === 'tool') input.push({type: 'function_call_output', call_id: message.tool_call_id, output: message.content});
      else input.push({role: message.role === 'system' ? 'developer' : message.role, content: message.content});
    }
    return input;
  }
  private async completion(effort: string, epoch: number): Promise<Message> {
    if (JSON.stringify(this.history).length > 384*1024) {
      for (let i = 1; i < this.history.length-8; i++) {
        const m = this.history[i];
        if (m.role === 'tool' && m.content.length > 1800)
          m.content = m.content.slice(0,1400) + '\n[Older output abbreviated; use research_read/script to inspect retained workspace documents.]';
      }
    }
    const input = this.responseInput();
    if (JSON.stringify(input).length > 2*1024*1024) throw new Error('context_limit');
    const functions: ResponseTool[] = this.tools.map((item: Object): ResponseTool => {
      const fn = (item as ToolDefinition).function;
      return {type: 'function', name: fn.name, description: fn.description, parameters: fn.parameters, strict: false};
    });
    const request = http.createHttp(); this.request = request;
    const body: RequestBody = {model: this.model, input, tools: functions,
      reasoning: {effort}, max_output_tokens: 32768, stream: true, store: false,
      include: ['reasoning.encrypted_content'], parallel_tool_calls: false};
    const decoder = new util.TextDecoder('utf-8');
    let buffer = '', content = '', failure = '', pendingName = '', argumentBytes = 0;
    let done = false;
    let output: ResponseItem[] = [];
    const ended = new Promise<void>((resolve: () => void) => {
      this.endStream = resolve;
      request.on('dataEnd', () => resolve());
    });
    request.on('dataReceive', (bytes: ArrayBuffer) => {
      if (epoch !== this.epoch || failure.length > 0) return;
      buffer += decoder.decodeToString(new Uint8Array(bytes), {stream: true});
      let end = buffer.indexOf('\n');
      while (end >= 0) {
        const line = buffer.slice(0, end).trim(); buffer = buffer.slice(end+1);
        end = buffer.indexOf('\n');
        if (!line.startsWith('data:')) continue;
        const raw = line.slice(5).trim();
        if (raw === '[DONE]') continue;
        try {
          const event = JSON.parse(raw) as ResponseEvent;
          if (event.type === 'response.output_text.delta' || event.type === 'response.refusal.delta') {
            content += event.delta ?? ''; this.liveResponse = content; this.updateBrief(content);
          } else if (event.type === 'response.output_item.added' && event.item?.type === 'function_call') {
            pendingName = event.item.name ?? ''; argumentBytes = 0;
          } else if (event.type === 'response.function_call_arguments.delta') {
            argumentBytes += event.delta?.length ?? 0;
            if (pendingName === 'research_script' || pendingName === 'research_write') {
              const item = this.activities[this.activities.length-1];
              if (item) item.detail = `${Math.floor(argumentBytes/1024)} KB`;
            }
          } else if (event.type === 'response.incomplete') {
            failure = event.response?.incomplete_details?.reason === 'max_output_tokens' ? 'response_truncated' : 'incomplete_response';
          } else if (event.type === 'response.failed' || event.type === 'error') {
            failure = 'response_failed';
          } else if (event.type === 'response.completed') {
            if (event.response?.status !== 'completed' || !event.response.output) { failure = 'invalid_response'; continue; }
            output = event.response.output;
            for (const item of output) item.created_by = undefined;
            done = true;
          }
          if (content.length + argumentBytes > 512*1024) failure = 'response_truncated';
          if (Date.now()-this.updated > 100) { this.updated = Date.now(); this.changed(); }
        } catch (_) { failure = 'invalid_stream'; }
      }
    });
    try {
      const status = await request.requestInStream('https://api.openai.com/v1/responses', {
        method: http.RequestMethod.POST, header: {'Content-Type': 'application/json', 'Authorization': `Bearer ${this.key.trim()}`},
        extraData: JSON.stringify(body), connectTimeout: 30000, readTimeout: 120000
      });
      if (status !== 200) throw new Error(`HTTP ${status}`);
      await ended;
      if (failure) throw new Error(failure);
      if (!done) throw new Error('incomplete_stream');
      const calls: ToolCall[] = [], texts: string[] = [];
      for (const item of output) {
        if (item.type === 'message' && Array.isArray(item.content)) {
          for (const part of item.content as ResponseContent[]) {
            if (part.text) texts.push(part.text);
            else if (part.refusal) texts.push(part.refusal);
          }
        } else if (item.type === 'function_call') {
          if (!item.call_id || !item.name || typeof item.arguments !== 'string') throw new Error('invalid_tool_call');
          calls.push({id: item.call_id, type: 'function', function: {name: item.name, arguments: item.arguments}});
        }
      }
      content = texts.join('\n'); this.liveResponse = content;
      const message: Message = {role: 'assistant', content, response_items: output};
      if (calls.length) message.tool_calls = calls;
      return message;
    } finally { this.endStream = undefined; request.destroy(); if (this.request === request) this.request = undefined; }
  }
  async send(input: string): Promise<void> {
    if (this.busy || !this.key.trim() || !this.consent || !input.trim() || !this.systemPrompt) return;
    this.recoverHistory();
    this.lines.push({role: 'user', text: input.trim()});
    this.progress = this.text('等待 Agent 的工作简报…', 'Waiting for the Agent’s work brief…');
    this.busy = true; const epoch = ++this.epoch;
    if (!this.history.length) this.history.push({role: 'system', content: this.systemPrompt});
    this.history.push({role: 'user', content: input.trim()});
    this.ticker = setInterval(() => this.changed(), 1000);
    let failed = false, waiting = false, effort = 'high';
    try {
      while (true) {
        if (epoch !== this.epoch) return;
        this.activity(this.text('模型回复', 'Model response'));
        let message: Message;
        try { message = await this.completion(effort, epoch); effort = 'high'; }
        catch (error) {
          if ((error as Error).message !== 'response_truncated') throw error as Error;
          effort = 'low'; this.liveResponse = '';
          this.finishActivity(this.text('回复过长，拆分后继续', 'Response too long; continuing with smaller steps'));
          this.history.push({role: 'user', content: 'The previous response exceeded the output limit and no incomplete tool call was executed. Continue the original task. Break code into small reusable modules using research_write. Emit one small concrete step now.'});
          continue;
        }
        if (epoch !== this.epoch) return;
        this.finishActivity(); this.history.push(message); this.liveResponse = '';
        if (message.content) { this.lines.push({role: 'assistant', text: message.content}); this.updateBrief(message.content); }
        this.changed();
        if (!message.tool_calls?.length) {
          this.history.push({role: 'user', content: "Audit the original request against your actual successful tool results. If anything remains unimplemented or unverified, continue with the next concrete step now. Choosing an engineering approach or proceeding through smaller milestones is already authorized; do not ask me to approve ordinary research or a reduced scope. A failed search or a small collision scan does not establish impossibility. Search complete retained documents and repository indexes with research_find before claiming resources are absent. Call task_complete only when all requirements are met, or task_wait only for genuinely missing user-specific information or destructive confirmation."});
          continue;
        }
        for (const call of message.tool_calls) {
          if (epoch !== this.epoch) return;
          let value: string;
          let finished = false;
          try {
            const args = JSON.parse(call.function.arguments) as AgentArgs;
            if (args.work_brief?.trim()) this.reportBrief(args.work_brief);
            this.activity(this.toolProgress(call.function.name, args));
            if (call.function.name === 'task_complete') {
              if (!args.summary?.trim() || !Array.isArray(args.evidence) || !args.evidence.length || args.evidence.some(item => typeof item !== 'string' || !item.trim())) throw new Error('invalid_completion');
              this.lines.push({role: 'assistant', text: args.summary}); finished = true;
              value = '{"ok":true,"completed":true}';
            } else if (call.function.name === 'task_wait') {
              if (!args.question?.trim() || !['missing_user_information', 'destructive_confirmation'].includes(args.reason ?? '')) throw new Error('invalid_wait');
              this.lines.push({role: 'assistant', text: args.question}); finished = true; waiting = true;
              value = '{"ok":true,"waiting_for_user":true}';
            } else value = await this.execute(call.function.name, args);
          } catch (error) { value = JSON.stringify({ok: false, error: (error as Error).message}); }
          if (epoch !== this.epoch) return;
          this.trace.push({name: call.function.name, arguments: call.function.arguments, result: value});
          this.history.push({role: 'tool', tool_call_id: call.id, content: value});
          const result = JSON.parse(value) as ToolResult;
          this.finishActivity(result.ok === false ? (result.error ?? 'tool_failed') : '');
          if (finished) return;
        }
      }
    } catch (error) {
      failed = true;
      if (epoch === this.epoch) this.lines.push({role: 'assistant', text: this.text('请求未完成：', 'Request failed: ') + (error as Error).message});
    } finally {
      if (epoch === this.epoch) {
        clearInterval(this.ticker); this.request?.destroy(); this.request = undefined;
        this.busy = false; this.liveResponse = ''; this.progress = failed ? this.text('请求失败', 'Request failed') : waiting ? this.text('等待你的信息', 'Waiting for your input') : this.text('已完成', 'Finished'); this.changed();
      }
    }
  }
}
interface ToolResult { ok?: boolean; error?: string; }
