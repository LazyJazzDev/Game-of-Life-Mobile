import { UIAbility, ConfigurationConstant, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import native from 'libgameoflife.so';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want): void {
    // Explicit capture arguments only; ordinary launches never seed a board.
    const pattern = want.parameters?.['capturePattern'];
    const zoom = want.parameters?.['captureZoom'];
    const panel = want.parameters?.['capturePanel'];
    AppStorage.setOrCreate('capturePattern', typeof pattern === 'string' ? pattern : '');
    AppStorage.setOrCreate('captureZoom', typeof zoom === 'string' ? zoom : '1');
    AppStorage.setOrCreate('capturePanel', typeof panel === 'string' ? panel : '');
    const language = want.parameters?.['captureLanguage'];
    if (typeof language === 'string' && (language === 'en-US' || language === 'zh-CN')) {
      this.context.getApplicationContext().setLanguage(language);
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
    } catch (e) {
      AppStorage.setOrCreate('gameStartupError', `Window setup: ${e}`);
    }
  }
  onForeground(): void {
    AppStorage.setOrCreate('privacyForeground', Date.now());
    if (AppStorage.get<boolean>('privacyNativeReady')) native.command('{"type":"active","value":true}'); }
  onBackground(): void { if (AppStorage.get<boolean>('privacyNativeReady')) native.command('{"type":"active","value":false}'); }
  onWindowStageDestroy(): void { if (AppStorage.get<boolean>('privacyNativeReady')) native.command('{"type":"stop"}'); }
}
