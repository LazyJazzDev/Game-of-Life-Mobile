import { http } from '@kit.NetworkKit';
import { util } from '@kit.ArkTS';

export interface TemplatePart { name: string; x: number; y: number; rotation?: number; mirror?: number; columns?: number; rows?: number; dx?: number; dy?: number; }
export interface AgentArgs {
  url?: string; document?: string; offset?: number; length?: number; content?: string; code?: string; language?: string;
  rle?: string; parts?: TemplatePart[]; mode?: string;
  action?: string; value?: number; x?: number; y?: number;
  width?: number; height?: number; cells?: number[][]; query?: string; name?: string;
}
interface ToolFunction { name: string; arguments: string; }
interface ToolCall { id: string; type: string; function: ToolFunction; }
interface Message {
  role: string; content: string; reasoning_content?: string;
  tool_calls?: ToolCall[]; tool_call_id?: string;
}
interface DeltaFunction { name?: string; arguments?: string; }
interface DeltaCall { index: number; id?: string; function?: DeltaFunction; }
interface Delta { content?: string; reasoning_content?: string; tool_calls?: DeltaCall[]; }
interface Choice { delta?: Delta; finish_reason?: string; }
interface Completion { choices: Choice[]; }
interface RequestBody {
  model: string; messages: Message[]; tools: Object[];
  thinking: Thinking; reasoning_effort: string; max_tokens: number; stream: boolean;
}
interface Thinking { type: string; }
export interface AgentTrace { name: string; arguments: string; result: string; }
export interface AgentLine { role: string; text: string; }

export interface AgentActivity { title: string; detail: string; started: number; finished: boolean; failed: boolean; }
export class LifeAgent {
  key: string = '';
  readonly model: string = 'deepseek-flash';
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
  cancel(): void {
    this.epoch++; this.endStream?.(); this.endStream = undefined; this.cancelResearch();
    clearInterval(this.ticker); this.liveResponse = '';
    this.request?.destroy(); this.request = undefined;
    this.busy = false; this.history = [];
    this.progress = this.text('已停止', 'Stopped'); this.changed();
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
    this.progress = title; this.changed();
  }
  private finishActivity(error: string = ''): void {
    const item = this.activities[this.activities.length-1];
    if (item) { item.finished = true; item.failed = error.length > 0; if (error) item.detail = error.slice(0, 240); }
    this.changed();
  }
  private async completion(effort: string, epoch: number): Promise<Message> {
    if (JSON.stringify(this.history).length > 384*1024) {
      for (let i = 1; i < this.history.length-8; i++) {
        const m = this.history[i];
        if (m.role === 'tool' && m.content.length > 1800)
          m.content = m.content.slice(0,1400) + '\n[Older output abbreviated; use research_read/script to inspect retained workspace documents.]';
      }
    }
    if (JSON.stringify(this.history).length > 2*1024*1024) throw new Error('context_limit');
    const request = http.createHttp(); this.request = request;
    const body: RequestBody = { model: this.model, messages: this.history, tools: this.tools,
      thinking: { type: 'enabled' }, reasoning_effort: effort, max_tokens: 32768, stream: true };
    const decoder = new util.TextDecoder('utf-8');
    let buffer = '', content = '', reasoning = '', failure = '';
    let done = false;
    const calls: ToolCall[] = [];
    const ended = new Promise<void>((resolve: () => void) => {
      this.endStream = resolve;
      request.on('dataEnd', () => resolve());
    });
    request.on('dataReceive', (bytes: ArrayBuffer) => {
      if (epoch !== this.epoch || failure.length > 0) return;
      buffer += decoder.decodeToString(new Uint8Array(bytes), { stream: true });
      let end = buffer.indexOf('\n');
      while (end >= 0) {
        const line = buffer.slice(0, end).trim(); buffer = buffer.slice(end+1);
        end = buffer.indexOf('\n');
        if (!line.startsWith('data:')) continue;
        const raw = line.slice(5).trim();
        if (raw === '[DONE]') { done = true; continue; }
        try {
          const choice = (JSON.parse(raw) as Completion).choices?.[0];
          const delta = choice?.delta;
          if (choice?.finish_reason === 'length') { failure = 'response_truncated'; continue; }
          if (!delta) continue;
          if (delta.reasoning_content) { reasoning += delta.reasoning_content; this.progress = this.text('正在分析结构与操作步骤…', 'Analyzing the structure and next steps…'); }
          if (delta.content) { content += delta.content; this.liveResponse = content; this.progress = this.text('正在整理回复…', 'Preparing reply…'); }
          for (const change of delta.tool_calls ?? []) {
            if (!calls[change.index]) calls[change.index] = { id: '', type: 'function', function: {name: '', arguments: ''} };
            const call = calls[change.index];
            if (change.id) call.id = change.id;
            call.function.name += change.function?.name ?? '';
            call.function.arguments += change.function?.arguments ?? '';
            if (call.function.name === 'research_script' || call.function.name === 'research_write') {
              this.progress = this.text('正在编写研究程序…', 'Writing a research program…');
              const item = this.activities[this.activities.length-1];
              if (item) { item.title = this.progress; item.detail = `${Math.floor(call.function.arguments.length/1024)} KB`; }
            }
          }
          if (content.length + reasoning.length > 256*1024) failure = 'response_truncated';
          if (Date.now()-this.updated > 100) { this.updated = Date.now(); this.changed(); }
        } catch (_) { failure = 'invalid_stream'; }
      }
    });
    try {
      const status = await request.requestInStream('https://api.deepseek.com/chat/completions', {
        method: http.RequestMethod.POST, header: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${this.key.trim()}` },
        extraData: JSON.stringify(body), connectTimeout: 30000, readTimeout: 120000
      });
      if (status !== 200) throw new Error(`HTTP ${status}`);
      await ended;
      if (failure) throw new Error(failure);
      if (!done) throw new Error('incomplete_stream');
      const message: Message = { role: 'assistant', content: content, reasoning_content: reasoning };
      if (calls.length) message.tool_calls = calls;
      return message;
    } finally { this.endStream = undefined; request.destroy(); if (this.request === request) this.request = undefined; }
  }
  async send(input: string): Promise<void> {
    if (this.busy || !this.key.trim() || !this.consent || !input.trim() || !this.systemPrompt) return;
    this.lines.push({role: 'user', text: input.trim()});
    this.busy = true; const epoch = ++this.epoch;
    if (!this.history.length) this.history.push({role: 'system', content: this.systemPrompt});
    this.history.push({role: 'user', content: input.trim()});
    this.ticker = setInterval(() => this.changed(), 1000);
    let failed = false, toolCount = 0, effort = 'high';
    try {
      for (let round = 0; round < 192; round++) {
        if (epoch !== this.epoch) return;
        this.activity(this.text('正在思考', 'Thinking') + ` · ${round+1}/192`);
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
        if (message.content) this.lines.push({role: 'assistant', text: message.content});
        this.changed();
        if (!message.tool_calls?.length) return;
        for (const call of message.tool_calls) {
          if (++toolCount > 768) throw new Error('tool_limit');
          if (epoch !== this.epoch) return;
          let value: string;
          try {
            const args = JSON.parse(call.function.arguments) as AgentArgs;
            this.activity(this.toolProgress(call.function.name, args));
            value = await this.execute(call.function.name, args);
          } catch (error) { value = JSON.stringify({ok: false, error: (error as Error).message}); }
          if (epoch !== this.epoch) return;
          this.trace.push({name: call.function.name, arguments: call.function.arguments, result: value});
          this.history.push({role: 'tool', tool_call_id: call.id, content: value});
          const result = JSON.parse(value) as ToolResult;
          this.finishActivity(result.ok === false ? (result.error ?? 'tool_failed') : '');
        }
      }
      throw new Error('round_limit');
    } catch (error) {
      failed = true;
      if (epoch === this.epoch) this.lines.push({role: 'assistant', text: this.text('请求未完成：', 'Request failed: ') + (error as Error).message});
    } finally {
      if (epoch === this.epoch) {
        clearInterval(this.ticker); this.request?.destroy(); this.request = undefined;
        this.busy = false; this.liveResponse = ''; this.progress = failed ? this.text('请求失败', 'Request failed') : this.text('已完成', 'Finished'); this.changed();
      }
    }
  }
}
interface ToolResult { ok?: boolean; error?: string; }
