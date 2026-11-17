import { http } from '@kit.NetworkKit';
import { webview } from '@kit.ArkWeb';
import { util } from '@kit.ArkTS';

export interface ResearchTemplate { name: string; rle: string; }
export interface ResearchValue { templates?: ResearchTemplate[]; result?: Object; }
export interface ResearchResult { ok: boolean; value?: ResearchValue; output?: string; error?: string; }

// The Web component exposes only result delivery, never native files or credentials.
export class ResearchBridge {
  receive: (id: number, value: string) => void = () => {};
  finish(id: number, value: string): void { this.receive(id, value); }
}
export class AgentResearch {
  controller: webview.WebviewController = new webview.WebviewController();
  bridge: ResearchBridge = new ResearchBridge();
  ready: boolean = false;
  engine: string = '';
  private documents: Record<string, string> = {};
  private sources: Record<string, string> = {};
  private sequence: number = 0;
  private pending?: (value: string) => void;
  private request?: http.HttpRequest;
  constructor() {
    this.bridge.receive = (id: number, value: string) => {
      if (id === this.sequence) { const done = this.pending; this.pending = undefined; done?.(value); }
    };
  }
  cancel(): void {
    this.sequence++;
    this.request?.destroy(); this.request = undefined;
    const done = this.pending; this.pending = undefined;
    done?.('{"ok":false,"error":"cancelled"}');
    // Reload destroys the document and its workers, including an infinite script.
    if (this.ready) { this.ready = false; this.controller.refresh(); }
  }
  content(id: string): string | undefined { return this.documents[id]; }
  read(id: string, offset: number, length: number): string {
    const value = this.documents[id];
    if (value === undefined || offset < 0) return '{"ok":false,"error":"unknown_document_or_offset"}';
    const start = Math.min(offset, value.length);
    const end = Math.min(value.length, start + Math.max(1, Math.min(length, 24000)));
    return JSON.stringify({ ok: true, document: id, url: this.sources[id], length: value.length,
      offset: start, next_offset: end, content: value.slice(start, end), untrusted_source: true });
  }
  write(name: string, content: string): string {
    if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(name) || new util.TextEncoder().encodeInto(content).length > 1024*1024)
      return '{"ok":false,"error":"invalid_artifact"}';
    const id = 'work_' + name; this.documents[id] = content; this.sources[id] = 'Agent-authored workspace';
    return JSON.stringify({ ok: true, document: id, length: content.length });
  }
  async fetch(input: string): Promise<string> {
    const url = input.replace(/^http:/, 'https:');
    const host = /^https:\/\/([^/]+)/i.exec(url)?.[1] ?? '';
    if (!host || host.includes('@') || /^(localhost|127\.|10\.|192\.168\.|\[)/i.test(host))
      return '{"ok":false,"error":"invalid_public_url"}';
    const request = http.createHttp(); this.request = request;
    try {
      const response = await request.request(url, { method: http.RequestMethod.GET,
        header: { 'User-Agent': 'LifeResearch/1.0' }, expectDataType: http.HttpDataType.ARRAY_BUFFER,
        connectTimeout: 30000, readTimeout: 45000, maxLimit: 4*1024*1024 });
      if (response.responseCode !== 200) return JSON.stringify({ok: false, status: response.responseCode, url: url});
      const content = new util.TextDecoder('utf-8', { fatal: true }).decodeToString(new Uint8Array(response.result as ArrayBuffer));
      const id = 'doc' + (Object.keys(this.documents).length + 1);
      this.documents[id] = content; this.sources[id] = url;
      return this.read(id, 0, 12000);
    } finally { request.destroy(); if (this.request === request) this.request = undefined; }
  }
  async script(code: string, language: string): Promise<string> {
    if (!this.ready || !this.engine.length) return '{"ok":false,"error":"research_runtime_not_ready"}';
    const id = ++this.sequence;
    return new Promise<string>((resolve: (value: string) => void) => {
      const timer = setTimeout(() => {
        if (id === this.sequence && this.pending) { this.cancel(); }
      }, 70000);
      this.pending = (value: string) => { clearTimeout(timer); resolve(value); };
      const js = `window.startResearch(${id},${JSON.stringify(code)},${JSON.stringify(this.documents)},${JSON.stringify(this.engine)},${JSON.stringify(language)});`;
      this.controller.runJavaScript(js).catch(() => this.bridge.finish(id, '{"ok":false,"error":"research_runtime_failed"}'));
    });
  }
}
