import { common } from '@kit.AbilityKit';
import { window, display } from '@kit.ArkUI';
import { fileIo as fs, picker } from '@kit.CoreFileKit';
import { sensor } from '@kit.SensorServiceKit';
import native from 'libgameoflife.so';

interface PatternFile { name: string; modified: number; }
// A named pattern from the built-in Life Lexicon library (demo/gol/patterns/library.json).
interface BuiltinPattern {
  name: string; category: string; width: number; height: number; period?: number; speed?: string; rle: string;
}
interface PatternDocument { patterns: BuiltinPattern[]; }
// One row of the library list: a section header, a saved pattern or a built-in one.
interface LibraryRow {
  key: string; header?: string; folder?: string; title?: string; count?: number;
  saved?: PatternFile; builtin?: BuiltinPattern;
}

const LEXICON_LICENSE: string = 'https://creativecommons.org/licenses/by-sa/3.0/';
const CATEGORY_ORDER: string[] = ['still', 'oscillator', 'spaceship', 'gun', 'puffer', 'methuselah', 'other'];

class LibraryRows implements IDataSource {
  private rows: LibraryRow[] = [];
  private listeners: DataChangeListener[] = [];
  set(rows: LibraryRow[]): void {
    this.rows = rows;
    this.listeners.forEach((listener: DataChangeListener) => listener.onDataReloaded());
  }
  totalCount(): number { return this.rows.length; }
  getData(index: number): LibraryRow { return this.rows[index]; }
  registerDataChangeListener(listener: DataChangeListener): void { this.listeners.push(listener); }
  unregisterDataChangeListener(listener: DataChangeListener): void {
    this.listeners = this.listeners.filter((item: DataChangeListener) => item !== listener);
  }
}

// Category names are the string resources category_<id>; speed directions are direction_<name>.
function patternDetail(pattern: BuiltinPattern, text: (name: string) => string): string {
  const parts: string[] = [];
  if (pattern.speed) {
    parts.push(pattern.speed.replace(/ (diagonal|orthogonal|oblique)$/,
      (match: string, direction: string): string => ` ${text(`direction_${direction}`)}`));
  }
  if (pattern.period && pattern.period > 1) parts.push(`p${pattern.period}`);
  parts.push(`${pattern.width}×${pattern.height}`);
  return parts.join(' · ');
}

// Life .cells text for a run-length encoded pattern.
function patternCells(pattern: BuiltinPattern): string {
  const rows: string[] = [];
  let row = '';
  let count = '';
  for (const c of pattern.rle) {
    if (c >= '0' && c <= '9') { count += c; continue; }
    const n = count.length > 0 ? Number(count) : 1;
    count = '';
    if (c === 'b') row += '.'.repeat(n);
    else if (c === 'o') row += 'O'.repeat(n);
    else if (c === '$' || c === '!') {
      rows.push(row);
      for (let i = 1; i < n && c === '$'; i++) rows.push('');
      row = '';
      if (c === '!') break;
    }
  }
  const lines = rows.map((line: string) => line.padEnd(pattern.width, '.'));
  return `!Name: ${pattern.name}\n!Life Lexicon (Stephen A. Silver et al.), adapted under CC BY-SA 3.0: https://creativecommons.org/licenses/by-sa/3.0/\n${lines.join('\n')}\n`;
}
interface NativeCommand {
  type: string; value?: number | boolean; axis?: number; path?: string; angle?: number; kind?: number;
  x?: number; y?: number; request?: number;
}
class NativeStatus {
  ready: boolean = false;
  error: string = '';
  device: string = '';
  resources: string = '';
  fps: number = 0;
  frames: number = 0;
  fileRequest: number = 0;
  fileRevision: number = 0;
  sizeAxis: number = 0;
  sizeValue: number = 64;
}

// iOS dark-mode system colors, so both apps share one visual language.
const BACKGROUND = '#000000';
// Grouped rows on a sheet, one level above the sheet background.
const SHEET = '#1C1C1E';
const SHEET_ROW = '#2C2C2E';
const SEPARATOR = '#38383A';
const LABEL = '#FFFFFF';
const SECONDARY = '#99EBEBF5';
const HEADER = '#8D8D93';
const TINT = '#0A84FF';
const FILL = '#3D767680';
const DESTRUCTIVE = '#FF453A';
// A hidden gesture indicator reports no avoid area; keep its touch strip clear.
const HIDDEN_INDICATOR_VP = 16;

function pad(value: number): string {
  return value < 10 ? `0${value}` : `${value}`;
}

function patternDate(seconds: number): string {
  const d = new Date(seconds * 1000);
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
}

// A file name for the in-app pattern library, without directories or reserved characters.
function patternName(name: string): string {
  const base = name.trim().replace(/\.cells$/i, '').replace(/[\\/:*?"<>|]/g, '_').trim();
  return base.length > 0 ? base : '';
}

function rectEnd(start: number, size: number): number {
  return size > 0 ? start + size : 0;
}

@Entry
@Component
struct Index {
  // The game opens once the bundled resources are ready.
  @State private opened: boolean = false;
  @State private stats: NativeStatus = new NativeStatus();
  @State private sizeAxis: number = 0;
  @State private sizeValue: number = 64;
  @State private sizeVisible: boolean = false;
  @State private safeBottom: number = 0;
  @State private rootWidth: number = 360;
  @State private anchorX: number = 0;
  @State private anchorY: number = 0;
  private timer: number = -1;
  private fileBusy: boolean = false;
  // In-app pattern library shown by the GoL open/save buttons.
  @State private libraryVisible: boolean = false;
  @State private librarySave: boolean = false;
  @State private patterns: PatternFile[] = [];
  @State private saveName: string = '';
  @State private librarySearch: string = '';
  // '' is the folder list; 'saved' or a category id is an opened folder.
  @State private libraryFolder: string = '';
  private libraryHandled: boolean = false;
  private libraryRows: LibraryRows = new LibraryRows();
  private builtins?: BuiltinPattern[];
  private request: number = 0;
  private areaWidth: number = 1;
  private areaHeight: number = 1;
  private lastX: number = 0;
  private lastY: number = 0;
  private distance: number = 0;
  private button: number = -1;
  private multi: boolean = false;
  private sensorActive: boolean = false;
  private entryAngle: number = 0;
  private shownError: string = '';
  private win?: window.Window;
  private context(): common.UIAbilityContext { return this.getUIContext().getHostContext() as common.UIAbilityContext; }
  // A localized string resource, with %s/%d arguments.
  private text(name: string, ...args: (string | number)[]): string {
    return this.context().resourceManager.getStringByNameSync(name, ...args);
  }
  private send(c: NativeCommand): void { native.command(JSON.stringify(c)); }

  aboutToAppear(): void {
    const startup = AppStorage.get<string>('gameStartupError') ?? '';
    if (startup.length > 0) this.alert(this.text('game_error'), startup);
    this.trackSafeArea();
    this.timer = setInterval(() => {
      const value = JSON.parse(native.status()) as NativeStatus;
      if (value.ready === undefined) return;
      this.stats = value;
      if (value.ready && !this.opened) this.open();
      if (value.sizeAxis !== this.sizeAxis) {
        this.sizeAxis = value.sizeAxis;
        this.sizeValue = value.sizeValue;
        this.sizeVisible = value.sizeAxis > 0;
      }
      if (this.opened && value.fileRequest > 0 && !this.fileBusy) this.fileAction(value.fileRequest);
      this.showNativeError(value.error);
    }, 250);
  }
  aboutToDisappear(): void {
    if (this.timer >= 0) clearInterval(this.timer);
    if (this.win) {
      this.win.off('avoidAreaChange');
      this.win.off('windowSizeChange');
    }
    this.leave();
  }

  // Layout is fullscreen with hidden system bars, like the iOS app. Content that
  // iOS keeps inside its safe area avoids the camera cutout and gesture strip here.
  private async trackSafeArea(): Promise<void> {
    try {
      this.win = await window.getLastWindow(this.context());
      this.updateSafeArea();
      this.win.on('avoidAreaChange', () => this.updateSafeArea());
      this.win.on('windowSizeChange', () => this.updateSafeArea());
    } catch (e) { /* Without window metrics the layout keeps zero insets. */ }
  }
  private updateSafeArea(): void {
    if (!this.win) return;
    try {
      this.applySafeArea(this.win);
    } catch (e) { /* Keep the previous insets if the window is being torn down. */ }
  }
  private applySafeArea(win: window.Window): void {
    const ui = this.getUIContext();
    const rect = win.getWindowProperties().windowRect;
    const cutout = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_CUTOUT);
    const indicator = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_NAVIGATION_INDICATOR);
    const bottom = Math.max(indicator.bottomRect.height > 0 ? rect.height - indicator.bottomRect.top : 0,
      cutout.bottomRect.height > 0 ? rect.height - cutout.bottomRect.top : 0);
    this.safeBottom = ui.px2vp(bottom);
    this.sendBottomInset();
  }
  // GoL keeps its lower controls above the gesture area plus 12 vp, as on iOS.
  private sendBottomInset(): void {
    if (this.areaHeight <= 1) return;
    const gesture = this.safeBottom > 0 ? this.safeBottom : HIDDEN_INDICATOR_VP;
    this.send({ type: 'bottomInset', value: (gesture + 12) / this.areaHeight });
  }

  private alert(title: string, message: string): void {
    this.getUIContext().showAlertDialog({
      title: title, message: message, autoCancel: false,
      confirm: { value: $r('app.string.ok'), action: () => {} }
    });
  }
  private showNativeError(error: string): void {
    if (error.length === 0 || error === this.shownError) return;
    this.shownError = error;
    this.alert(this.text('game_error'), error);
  }

  // The interface keeps the orientation the game opened in; gravity turns the icons.
  private async lockOrientation(): Promise<void> {
    try {
      const win = await window.getLastWindow(this.context());
      const rotation = display.getDefaultDisplaySync().rotation;
      this.entryAngle = rotation * Math.PI / 2;
      let orientation = window.Orientation.PORTRAIT;
      if (rotation === 1) orientation = window.Orientation.LANDSCAPE;
      else if (rotation === 3) orientation = window.Orientation.LANDSCAPE_INVERTED;
      else if (rotation === 2) orientation = window.Orientation.PORTRAIT_INVERTED;
      await win.setPreferredOrientation(orientation);
    } catch (e) { this.alert(this.text('game_error'), `Orientation: ${e}`); }
  }
  private open(): void {
    this.shownError = '';
    this.opened = true;
    this.button = -1; this.multi = false;
    this.send({ type: 'open' });
    this.lockOrientation();
    this.startGravity();
  }
  private leave(): void {
    this.send({ type: 'stop' });
    this.opened = false; this.sizeAxis = 0; this.sizeVisible = false; this.shownError = '';
    this.stopGravity();
  }
  // The direction icons turn with the device. The sensor is used only while
  // the page is visible: apps must not read sensors in the background.
  private startGravity(): void {
    if (this.sensorActive) return;
    try {
      sensor.on(sensor.SensorId.GRAVITY, (data: sensor.GravityResponse) => {
        if (Math.abs(data.x) + Math.abs(data.y) > 3) {
          const angle = Math.round(Math.atan2(-data.x, data.y) / (Math.PI / 2)) * Math.PI / 2;
          this.send({ type: 'icons', angle: this.entryAngle - angle });
        }
      }, { interval: 200000000 });
      this.sensorActive = true;
    } catch (e) { /* Direction icons remain upright if no gravity sensor is available. */ }
  }
  private stopGravity(): void {
    if (!this.sensorActive) return;
    try { sensor.off(sensor.SensorId.GRAVITY); } catch (e) { /* Already unsubscribed. */ }
    this.sensorActive = false;
  }
  onPageShow(): void {
    if (this.opened) this.startGravity();
  }
  onPageHide(): void {
    this.stopGravity();
  }
  private applySize(value: number): void {
    this.sizeValue = Math.max(2, Math.min(256, Math.round(value)));
    this.send({ type: 'size', axis: this.sizeAxis, value: this.sizeValue });
  }
  private closeSize(): void {
    this.sizeVisible = false;
    this.send({ type: 'dismissSize' });
  }
  private pointer(kind: number, x: number, y: number, value: number = 0): void {
    this.send({ type: 'input', kind: kind, x: x / this.areaWidth, y: y / this.areaHeight, value: value });
  }
  private touch(event: TouchEvent): void {
    const points = event.touches;
    if (event.type === TouchType.Cancel) {
      if (this.button >= 0) { this.pointer(5, 0, 0, 0); this.pointer(5, 0, 0, 1); }
      this.button = -1; this.multi = false; this.distance = 0;
      return;
    }
    const ending = event.type === TouchType.Up;
    // Every change of the finger count keeps the pointer where the game last saw
    // it: a jump while a drag is active would move the grid by the difference.
    if (points.length === 0 || (ending && points.length <= 1)) {
      // A two-finger gesture ends at its last midpoint, a single touch where it lifted.
      if (!this.multi && event.changedTouches.length > 0) {
        this.lastX = event.changedTouches[0].x; this.lastY = event.changedTouches[0].y;
      }
      if (this.button >= 0) this.pointer(2, this.lastX, this.lastY, this.button);
      this.button = -1; this.multi = false; this.distance = 0;
      return;
    }
    let x = points[0].x, y = points[0].y;
    let distance = 0;
    if (points.length >= 2) {
      x = (points[0].x + points[1].x) / 2;
      y = (points[0].y + points[1].y) / 2;
      distance = Math.hypot(points[1].x - points[0].x, points[1].y - points[0].y);
    }
    if (event.type === TouchType.Down && points.length === 1) {
      this.lastX = x; this.lastY = y;
      this.anchorX = x; this.anchorY = y;
      this.button = 0; this.pointer(1, x, y);
    } else if (points.length >= 2 && !this.multi) {
      // Cancel the one-finger press where that finger was, then start the
      // two-finger pan at the midpoint.
      if (this.button >= 0) { this.pointer(5, this.lastX, this.lastY, 0); this.pointer(5, this.lastX, this.lastY, 1); }
      this.multi = true;
      this.button = 1; this.pointer(1, x, y, 1);
    } else if (event.type === TouchType.Move && this.multi && points.length < 2) {
      // One finger of a two-finger gesture lifted: end it at the last midpoint.
      if (this.button >= 0) { this.pointer(2, this.lastX, this.lastY, this.button); this.button = -1; }
      return;
    } else if (event.type === TouchType.Move) {
      if (this.button >= 0) {
        this.pointer(0, x, y);
        if (this.distance > 0 && distance > 0) this.pointer(6, x, y, distance / this.distance);
      }
    }
    if (ending && this.button >= 0) {
      this.pointer(2, this.multi ? this.lastX : x, this.multi ? this.lastY : y, this.button);
      this.button = -1;
    }
    this.lastX = x; this.lastY = y; this.distance = distance;
  }
  private async completeFile(path: string): Promise<void> {
    this.request = Math.max(this.request, this.stats.fileRevision) + 1;
    const id = this.request;
    this.send({ type: 'file', path: path, request: id });
    for (let i = 0; i < 1200; i++) {
      await new Promise<void>((resolve) => setTimeout(resolve, 100));
      const value = JSON.parse(native.status()) as NativeStatus;
      if (value.fileRevision >= id) {
        if (value.error.length > 0) throw new Error(value.error);
        return;
      }
      if (!this.opened) return;
    }
    throw new Error('File operation timed out');
  }
  private patternDirectory(): string {
    const directory = `${this.context().filesDir}/patterns`;
    if (!fs.accessSync(directory)) fs.mkdirSync(directory);
    return directory;
  }
  private refreshPatterns(): void {
    const directory = this.patternDirectory();
    this.patterns = fs.listFileSync(directory)
      .filter((file: string) => file.toLowerCase().endsWith('.cells'))
      .map((file: string): PatternFile => ({ name: file.slice(0, -6), modified: fs.statSync(`${directory}/${file}`).mtime }))
      .sort((a: PatternFile, b: PatternFile) => b.modified - a.modified);
  }
  private loadBuiltins(): BuiltinPattern[] {
    if (this.builtins === undefined) {
      try {
        const text = fs.readTextSync(`${this.stats.resources}/Patterns/library.json`);
        this.builtins = (JSON.parse(text) as PatternDocument).patterns;
      } catch (e) {
        this.builtins = [];
      }
    }
    return this.builtins;
  }
  // Saved patterns first, then the built-in library by category, filtered by the search text.
  private folderTitle(folder: string): string {
    return this.text(folder === 'saved' ? 'my_patterns' : `category_${folder}`);
  }
  // The library opens on a folder list: saved patterns in "My Patterns", then one
  // folder per built-in category. Searching the list spans every folder;
  // searching an opened folder filters only that folder.
  private rebuildRows(): void {
    const query = this.librarySearch.trim().toLowerCase();
    const matches = (name: string): boolean => query.length === 0 || name.toLowerCase().includes(query);
    const rows: LibraryRow[] = [];
    const folder = this.librarySave ? 'saved' : this.libraryFolder;
    const builtins = this.librarySave ? [] : this.loadBuiltins();
    if (folder === '' && query.length === 0) {
      rows.push({ key: 'f:saved', folder: 'saved', title: this.folderTitle('saved'), count: this.patterns.length });
      for (const category of CATEGORY_ORDER) {
        const count = builtins.filter((pattern: BuiltinPattern) => pattern.category === category).length;
        rows.push({ key: `f:${category}`, folder: category, title: this.folderTitle(category), count: count });
      }
      this.libraryRows.set(rows);
      return;
    }
    const grouped = folder === '';
    if (folder === '' || folder === 'saved') {
      const saved = this.patterns.filter((pattern: PatternFile) => matches(pattern.name));
      if (grouped && saved.length > 0) rows.push({ key: 'h:saved', header: `${this.folderTitle('saved')} · ${saved.length}` });
      saved.forEach((pattern: PatternFile) => rows.push({ key: `s:${pattern.name}`, saved: pattern }));
    }
    for (const category of CATEGORY_ORDER) {
      if (folder !== '' && folder !== category) continue;
      const items = builtins.filter((pattern: BuiltinPattern) => pattern.category === category && matches(pattern.name));
      if (items.length === 0) continue;
      if (grouped) rows.push({ key: `h:${category}`, header: `${this.folderTitle(category)} · ${items.length}` });
      items.forEach((pattern: BuiltinPattern) => rows.push({ key: `b:${category}:${pattern.name}`, builtin: pattern }));
    }
    this.libraryRows.set(rows);
  }
  private openFolder(folder: string): void {
    this.libraryFolder = folder;
    this.librarySearch = '';
    this.rebuildRows();
  }
  // Opening or saving starts in the app's own pattern library; the system
  // document picker is one link away.
  private fileAction(action: number): void {
    this.fileBusy = true;
    this.libraryHandled = false;
    this.librarySave = action !== 1;
    this.librarySearch = '';
    this.libraryFolder = '';
    try {
      this.refreshPatterns();
    } catch (e) {
      this.patterns = [];
    }
    this.rebuildRows();
    const now = new Date();
    this.saveName = `Life ${now.getFullYear()}-${pad(now.getMonth() + 1)}-${pad(now.getDate())} ` +
      `${pad(now.getHours())}.${pad(now.getMinutes())}.${pad(now.getSeconds())}`;
    this.libraryVisible = true;
  }
  // Runs one choice from the library after closing it; cancelling reaches the
  // game as an empty path, exactly like a dismissed system picker.
  private async finishFile(work: () => Promise<void>): Promise<void> {
    this.libraryHandled = true;
    this.libraryVisible = false;
    try {
      await work();
    } catch (e) {
      this.alert(this.text('game_error'), `Files: ${e}`);
      try { await this.completeFile(''); } catch (cancelError) { /* Keep the original error visible. */ }
    }
    finally { this.fileBusy = false; }
  }
  // The built-in patterns' license (CC BY-SA 3.0), from the library footer.
  private openLicense(): void {
    this.context().openLink(LEXICON_LICENSE).catch(() => { /* The address stays readable in the footer. */ });
  }
  private libraryDismissed(): void {
    if (this.libraryHandled) return;
    this.libraryHandled = true;
    this.finishFile(() => this.completeFile(''));
  }
  private openPattern(pattern: PatternFile): void {
    this.finishFile(() => this.completeFile(`${this.patternDirectory()}/${pattern.name}.cells`));
  }
  private openBuiltin(pattern: BuiltinPattern): void {
    this.finishFile(async () => {
      const path = `${this.context().cacheDir}/Builtin.cells`;
      const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.TRUNC);
      try { fs.writeSync(file.fd, patternCells(pattern)); } finally { fs.closeSync(file); }
      await this.completeFile(path);
    });
  }
  private savePattern(name: string): void {
    const base = patternName(name);
    if (base.length === 0) { this.alert(this.text('save_pattern'), this.text('enter_file_name')); return; }
    const save = () => this.finishFile(() => this.completeFile(`${this.patternDirectory()}/${base}.cells`));
    if (!this.patterns.some((pattern: PatternFile) => pattern.name === base)) { save(); return; }
    this.getUIContext().showAlertDialog({
      title: $r('app.string.replace_pattern'), message: this.text('replace_exists', base), autoCancel: true,
      primaryButton: { value: $r('app.string.cancel'), action: () => {} },
      secondaryButton: { value: $r('app.string.replace'), fontColor: DESTRUCTIVE, action: () => save() }
    });
  }
  private deletePattern(pattern: PatternFile): void {
    try {
      fs.unlinkSync(`${this.patternDirectory()}/${pattern.name}.cells`);
      this.refreshPatterns();
      this.rebuildRows();
    } catch (e) { this.alert(this.text('game_error'), `Files: ${e}`); }
  }
  private systemFile(): void {
    const save = this.librarySave;
    this.finishFile(async () => {
      const temp = `${this.context().cacheDir}/Life.cells`;
      const dialog = new picker.DocumentViewPicker(this.context());
      if (!save) {
        const options = new picker.DocumentSelectOptions(); options.maxSelectNumber = 1;
        const uris = await dialog.select(options);
        if (uris.length === 0) { await this.completeFile(''); return; }
        const file = fs.openSync(uris[0], fs.OpenMode.READ_ONLY);
        try { await fs.copyFile(file.fd, temp); } finally { fs.closeSync(file); }
        await this.completeFile(temp);
      } else {
        await this.completeFile(temp);
        const options = new picker.DocumentSaveOptions(); options.newFileNames = ['Life.cells'];
        const uris = await dialog.save(options);
        if (uris.length > 0) {
          const file = fs.openSync(uris[0], fs.OpenMode.READ_WRITE);
          try { await fs.copyFile(temp, file.fd); } finally { fs.closeSync(file); }
        }
      }
    });
  }

  // ---- Grid size popover ----

  @Builder
  private stepper() {
    Row() {
      Row() { SymbolGlyph($r('sys.symbol.minus')).fontSize(17).fontColor([LABEL]) }
        .width(47).height(32).justifyContent(FlexAlign.Center).onClick(() => this.adjust(-1))
      Divider().vertical(true).height(18).strokeWidth(1).color('#5A5A5E')
      Row() { SymbolGlyph($r('sys.symbol.plus')).fontSize(17).fontColor([LABEL]) }
        .width(47).height(32).justifyContent(FlexAlign.Center).onClick(() => this.adjust(1))
    }.backgroundColor(FILL).borderRadius(16)
  }
  private adjust(delta: number): void {
    this.applySize(this.sizeValue + delta);
  }

  @Builder
  private surface() {
    XComponent({ id: 'gameSurface', type: XComponentType.SURFACE, libraryname: 'gameoflife' })
      .width('100%').height('100%')
    // Native-library XComponents dispatch touch to C++; a sibling ArkUI
    // input layer keeps all multi-touch handling in the shared UI adapter.
    Column() {}.width('100%').height('100%').backgroundColor('#00000000')
      .hitTestBehavior(HitTestMode.Block)
      .onTouch((event: TouchEvent) => this.touch(event))
    // Anchor for the size popover at the tapped readout, like the iOS popover source.
    Column().width(1).height(1).position({ x: this.anchorX, y: this.anchorY }).hitTestBehavior(HitTestMode.None)
      .bindPopup(this.sizeVisible, {
        builder: this.sizePicker(),
        placement: Placement.Top,
        enableArrow: true,
        autoCancel: true,
        popupColor: '#F22C2C2E',
        backgroundBlurStyle: BlurStyle.NONE,
        radius: 26,
        width: Math.min(340, this.rootWidth - 24),
        onStateChange: (e: PopupStateChangeParam) => { if (!e.isVisible && this.sizeVisible) this.closeSize(); }
      })
  }

  @Builder
  private sizePicker() {
    Column({ space: 16 }) {
      Row() {
        Text(this.sizeAxis === 1 ? $r('app.string.grid_width') : $r('app.string.grid_height')).fontSize(17).fontWeight(600).fontColor(LABEL)
        Blank()
        Text($r('app.string.done')).fontSize(17).fontColor(TINT).onClick(() => { this.applySize(this.sizeValue); this.closeSize(); })
      }.width('100%')
      Row() {
        Text(`${this.sizeValue}`).fontSize(22).fontColor(LABEL).fontFeature('"tnum" on')
        Blank()
        this.stepper()
      }.width('100%')
      // Resize the grid once scrubbing ends instead of rebuilding cells on every move.
      Slider({ value: this.sizeValue, min: 2, max: 256, step: 1 })
        .selectedColor(TINT).trackColor('#48484A').blockColor('#FFFFFF').trackThickness(4)
        .onChange((value: number, mode: SliderChangeMode) => {
          this.sizeValue = Math.round(value);
          if (mode === SliderChangeMode.End || mode === SliderChangeMode.Click) this.applySize(value);
        })
        .accessibilityText(this.text(this.sizeAxis === 1 ? 'grid_width' : 'grid_height'))
      Row() {
        Text('2').fontSize(12).fontColor(SECONDARY)
        Blank()
        Text('256').fontSize(12).fontColor(SECONDARY)
      }.width('100%')
    }.padding(20).width('100%')
  }

  // ---- Pattern library (open/save start here, not in the system picker) ----

  @Builder
  private patternDelete(pattern: PatternFile) {
    Row() { Text($r('app.string.delete')).fontSize(17).fontColor(LABEL) }
    .width(80).height('100%').justifyContent(FlexAlign.Center).backgroundColor(DESTRUCTIVE)
    .onClick(() => this.deletePattern(pattern))
  }

  @Builder
  private libraryRow(row: LibraryRow) {
    if (row.folder !== undefined) {
      Row({ space: 12 }) {
        SymbolGlyph(row.folder === 'saved' ? $r('sys.symbol.folder_fill') : $r('sys.symbol.folder'))
          .fontSize(22).fontColor([TINT])
        Text(row.title).fontSize(17).fontColor(LABEL).layoutWeight(1)
        Text(`${row.count}`).fontSize(17).fontColor(SECONDARY)
        SymbolGlyph($r('sys.symbol.chevron_right')).fontSize(16).fontColor([SECONDARY])
      }
      .width('100%').padding({ left: 16, right: 16, top: 12, bottom: 12 }).backgroundColor(SHEET_ROW)
      .border({ width: { bottom: 0.5 }, color: SEPARATOR })
    } else if (row.header !== undefined) {
      Text(row.header).fontSize(15).fontWeight(600).fontColor(HEADER)
        .width('100%').padding({ left: 16, right: 16, top: 18, bottom: 8 })
    } else {
      Row({ space: 12 }) {
        SymbolGlyph(row.saved ? $r('sys.symbol.doc_text') : $r('sys.symbol.square_grid_2x2'))
          .fontSize(20).fontColor([TINT])
        Column({ space: 2 }) {
          Text(row.saved ? row.saved.name : row.builtin!.name).fontSize(17).fontColor(LABEL)
            .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
          Text(row.saved ? patternDate(row.saved.modified) : patternDetail(row.builtin!, (name: string): string => this.text(name)))
            .fontSize(13).fontColor(SECONDARY)
        }.alignItems(HorizontalAlign.Start).layoutWeight(1)
      }
      .width('100%').padding({ left: 16, right: 16, top: 10, bottom: 10 }).backgroundColor(SHEET_ROW)
      .border({ width: { bottom: 0.5 }, color: SEPARATOR })
    }
  }

  @Builder
  private patternLibrary() {
    Column() {
      if (this.librarySave) {
        Row({ space: 12 }) {
          TextInput({ text: this.saveName, placeholder: $r('app.string.file_name') })
            .layoutWeight(1).height(40).fontSize(17).fontColor(LABEL).placeholderColor(SECONDARY)
            .backgroundColor(FILL).borderRadius(10).caretColor(TINT)
            .onChange((value: string) => { this.saveName = value; })
            .onSubmit(() => this.savePattern(this.saveName))
          Text($r('app.string.save')).fontSize(17).fontWeight(600).fontColor(TINT).onClick(() => this.savePattern(this.saveName))
        }.width('100%').padding({ left: 16, right: 16, bottom: 4 })
      } else {
        if (this.libraryFolder.length > 0) {
          Row({ space: 4 }) {
            SymbolGlyph($r('sys.symbol.chevron_left')).fontSize(18).fontColor([TINT])
            Text($r('app.string.all_folders')).fontSize(17).fontColor(TINT)
            Blank()
            Text(this.folderTitle(this.libraryFolder)).fontSize(17).fontWeight(600).fontColor(LABEL)
          }
          .width('100%').padding({ left: 16, right: 16, bottom: 10 })
          .onClick(() => this.openFolder(''))
        }
        Search({ value: this.librarySearch,
          placeholder: this.libraryFolder.length > 0 ?
            this.text('search_in_folder', this.folderTitle(this.libraryFolder)) : this.text('search_all') })
          .height(40).margin({ left: 16, right: 16 }).backgroundColor(FILL).fontColor(LABEL)
          .placeholderColor(SECONDARY).textFont({ size: 17 })
          .onChange((value: string) => { this.librarySearch = value; this.rebuildRows(); })
      }
      List() {
        LazyForEach(this.libraryRows, (row: LibraryRow) => {
          ListItem() { this.libraryRow(row) }
          .swipeAction(row.saved ? { end: this.patternDelete(row.saved) } : {})
          .onClick(() => {
            if (row.folder !== undefined) this.openFolder(row.folder);
            else if (row.saved) { if (this.librarySave) this.savePattern(row.saved.name); else this.openPattern(row.saved); }
            else if (row.builtin) this.openBuiltin(row.builtin);
          })
        }, (row: LibraryRow) => row.key)
        ListItem() {
          Text(this.librarySave ? '' : this.text('lexicon_credit'))
            .fontSize(12).fontColor(SECONDARY).padding({ left: 16, right: 16, top: 12, bottom: 12 })
            .onClick(() => { if (!this.librarySave) this.openLicense(); })
        }
      }
      .cachedCount(8).scrollBar(BarState.Auto).edgeEffect(EdgeEffect.Spring).layoutWeight(1).width('100%')
      Text(this.librarySave ? $r('app.string.save_to_files') : $r('app.string.open_from_files'))
        .fontSize(17).fontColor(TINT).padding({ top: 14, bottom: 14 + this.safeBottom })
        .onClick(() => this.systemFile())
    }.width('100%').height('100%')
  }

  // ---- Game page (iOS GamesView) ----

  private canvasArea(area: Area): void {
    this.areaWidth = Number(area.width); this.areaHeight = Number(area.height);
    this.sendBottomInset();
  }

  build() {
    Stack({ alignContent: Alignment.TopStart }) {
      // The canvas fills the display; the page stays blank until resources are ready.
      if (this.opened) {
        Stack() { this.surface() }
          .width('100%').height('100%')
          .onAreaChange((oldArea: Area, area: Area) => this.canvasArea(area))
      }
    }
    .width('100%').height('100%').backgroundColor(BACKGROUND)
    .onAreaChange((oldArea: Area, area: Area) => {
      this.rootWidth = Number(area.width);
    })
    .bindSheet($$this.libraryVisible, this.patternLibrary(), {
      detents: [SheetSize.MEDIUM, SheetSize.LARGE], dragBar: true, backgroundColor: SHEET,
      title: { title: this.librarySave ? $r('app.string.save_pattern') : $r('app.string.open_pattern') },
      onDisappear: () => this.libraryDismissed()
    })
  }
}
