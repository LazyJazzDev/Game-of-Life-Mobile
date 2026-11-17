import { UIAbility, ConfigurationConstant, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import native from 'libgameoflife.so';

export default class EntryAbility extends UIAbility {
  private captureLanguage: string = '';
  onCreate(want: Want): void {
    // Explicit capture arguments only; ordinary launches never seed a board.
    const agentPrompt = want.parameters?.['agentSmoke'];
    AppStorage.setOrCreate('agentSmoke', this.context.applicationInfo.debug && typeof agentPrompt === 'string' ? agentPrompt : '');
    const researchTest = want.parameters?.['researchSelftest'];
    AppStorage.setOrCreate('researchSelftest', this.context.applicationInfo.debug && typeof researchTest === 'string' ? researchTest : '');
    AppStorage.setOrCreate('agentForeground', true);
    const pattern = want.parameters?.['capturePattern'];
    const zoom = want.parameters?.['captureZoom'];
    const panel = want.parameters?.['capturePanel'];
    const saveName = want.parameters?.['captureSaveName'];
    AppStorage.setOrCreate('capturePattern', typeof pattern === 'string' ? pattern : '');
    AppStorage.setOrCreate('captureZoom', typeof zoom === 'string' ? zoom : '1');
    AppStorage.setOrCreate('capturePanel', typeof panel === 'string' ? panel : '');
    AppStorage.setOrCreate('captureSaveName', typeof saveName === 'string' ? saveName.trim() : '');
    const language = want.parameters?.['captureLanguage'];
    if (typeof language === 'string' && (language === 'en-US' || language === 'zh-CN')) {
      this.captureLanguage = language;
    }
    this.context.getApplicationContext().setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_DARK);
  }
  onWindowStageCreate(stage: window.WindowStage): void {
    this.configureWindow(stage);
  }
  private async configureWindow(stage: window.WindowStage): Promise<void> {
    try {
      const win = await stage.getMainWindow();
      await win.setWindowLayoutFullScreen(true);
      await win.setWindowSystemBarEnable([]);
      // The system handles rotation and its lock; the app reads no motion sensor.
      await win.setPreferredOrientation(window.Orientation.AUTO_ROTATION_RESTRICTED);
      await stage.loadContent('pages/Index');
      // setLanguage requires an existing window and a loaded page (API 11+).
      if (this.captureLanguage.length > 0) {
        this.context.getApplicationContext().setLanguage(this.captureLanguage);
      }
    } catch (e) {
      AppStorage.setOrCreate('gameStartupError', `Window setup: ${e}`);
    }
  }
  onForeground(): void {
    AppStorage.setOrCreate('agentForeground', true);
    AppStorage.setOrCreate('privacyForeground', Date.now());
    if (AppStorage.get<boolean>('privacyNativeReady')) native.command('{"type":"active","value":true}'); }
  onBackground(): void { AppStorage.setOrCreate('agentForeground', false); if (AppStorage.get<boolean>('privacyNativeReady')) native.background(); }
  onWindowStageDestroy(): void { if (AppStorage.get<boolean>('privacyNativeReady')) native.command('{"type":"stop"}'); }
}
