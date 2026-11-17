import { http } from '@kit.NetworkKit';

export interface TemplatePart { name: string; x: number; y: number; rotation?: number; mirror?: number; columns?: number; rows?: number; dx?: number; dy?: number; }
export interface AgentArgs {
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
interface Choice { message: Message; finish_reason: string; }
interface Completion { choices: Choice[]; }
interface RequestBody {
  model: string; messages: Message[]; tools: Object[];
  thinking: Thinking; reasoning_effort: string; max_tokens: number; stream: boolean;
}
interface Thinking { type: string; }
export interface AgentTrace { name: string; arguments: string; result: string; }
export interface AgentLine { role: string; text: string; }

export class LifeAgent {
  key: string = '';
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
    this.epoch++;
    this.request?.destroy(); this.request = undefined;
    this.busy = false; this.history = [];
    this.progress = this.text('已停止', 'Stopped'); this.changed();
  }
  private toolProgress(name: string, args: AgentArgs): string {
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
  async send(input: string): Promise<void> {
    if (this.busy || !this.key.trim() || !this.consent || !input.trim()) return;
    this.lines.push({ role: 'user', text: input.trim() });
    this.busy = true;
    const epoch = ++this.epoch;
    if (this.history.length === 0) this.history.push({ role: 'system', content:
      'You are Life Agent inside Conway Life (B3/S23). Reply in the user language. Use tools to inspect and edit the canvas. Coordinates are center origin, x right, y down. Use catalog patterns. For large or repeated designs, save small RLE templates, compose named templates with grid repetition, and place the whole compound template once. Template anchors are TOP-LEFT; pattern_place anchors are CENTER. Inspect canvas_summary before writing and compare template population/hash afterwards. A scaled drawing of cells is not a functioning metacell; never claim it simulates macro Life without a real metacell construction. Read before editing, preserve unrelated cells. Never reset/randomize unless requested. These actions need user confirmation, so stop and wait after requesting them. Do not claim success on failed tools. Tests are finite observations, not proofs. Links are references, not fetched text. Keep responses concise.' });
    this.history.push({ role: 'user', content: input.trim() });
    let failed = false;
    let toolCount = 0;
    try {
      for (let round = 0; round < 10; round++) {
        if (epoch !== this.epoch) return;
        this.progress = this.text('正在思考与分析结构', 'Thinking about the structure') + ` · ${round + 1}/10`;
        this.changed();
        const request = http.createHttp(); this.request = request;
        const body: RequestBody = { model: 'deepseek-flash', messages: this.history, tools: this.tools,
          thinking: { type: 'enabled' }, reasoning_effort: 'high', max_tokens: 4096, stream: false };
        if (JSON.stringify(body).length > 512*1024) throw new Error(this.text('对话过长，请重新开始。', 'Conversation limit reached; start again.'));
        const response = await request.request('https://api.deepseek.com/chat/completions', {
          method: http.RequestMethod.POST, header: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${this.key.trim()}` },
          extraData: JSON.stringify(body), expectDataType: http.HttpDataType.STRING, connectTimeout: 30000, readTimeout: 120000
        });
        request.destroy(); this.request = undefined;
        if (epoch !== this.epoch) return;
        if (response.responseCode !== 200) throw new Error(this.text('服务请求失败，请检查密钥、余额或网络', 'API request failed; check key, balance and connection') + ` (${response.responseCode})`);
        const result = JSON.parse(response.result as string) as Completion;
        if (!result.choices?.length || result.choices[0].finish_reason === 'length') throw new Error(this.text('回复不完整，请重试', 'Incomplete response; retry'));
        const message = result.choices[0].message;
        this.history.push(message);
        if (message.content) this.lines.push({ role: 'assistant', text: message.content });
        this.changed();
        if (!message.tool_calls?.length) break;
        for (const call of message.tool_calls) {
          if (++toolCount > 24) throw new Error(this.text('本轮操作次数达到上限。', 'Action limit reached.'));
          if (epoch !== this.epoch) return;
          const args = JSON.parse(call.function.arguments) as AgentArgs;
          this.progress = this.toolProgress(call.function.name, args);
          this.changed();
          let value = '{"ok":false,"error":"invalid_arguments_or_unavailable"}';
          try { value = await this.execute(call.function.name, args); }
          catch (_) { }
          if (epoch !== this.epoch) return;
          this.trace.push({ name: call.function.name, arguments: call.function.arguments, result: value });
          this.history.push({ role: 'tool', tool_call_id: call.id, content: value });
        }
        if (round === 9) {
          this.lines.push({ role: 'assistant', text: this.text('本轮已达到操作上限，请描述下一步。', 'Action limit reached. Tell me the next step.') });
          this.history = [];
        }
      }
    } catch (error) {
      failed = true;
      if (epoch === this.epoch) {
        this.lines.push({ role: 'assistant', text: this.text('请求未完成：', 'Request failed: ') + (error as Error).message });
        this.history = [];
      }
    } finally {
      if (epoch === this.epoch) { this.request?.destroy(); this.request = undefined; this.busy = false; this.progress = failed ? this.text('请求失败', 'Request failed') : this.text('已完成', 'Finished'); this.changed(); }
    }
  }
}
