import { UIAbility, ConfigurationConstant } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import native from 'libgameoflife.so';

export default class EntryAbility extends UIAbility {
  onCreate(): void {
    this.context.getApplicationContext().setColorMode(ConfigurationConstant.ColorMode.COLOR_MODE_DARK);
  }
  onWindowStageCreate(stage: window.WindowStage): void {
    try {
      native.initialize(this.context.resourceManager, this.context.filesDir);
    } catch (e) {
      AppStorage.setOrCreate('gameStartupError', `Initialization: ${e}`);
    }
    this.configureWindow(stage);
  }
  private async configureWindow(stage: window.WindowStage): Promise<void> {
    try {
      const win = await stage.getMainWindow();
      await win.setWindowLayoutFullScreen(true);
      await win.setWindowSystemBarEnable([]);
      await stage.loadContent('pages/Index');
    } catch (e) {
      AppStorage.setOrCreate('gameStartupError', `Window setup: ${e}`);
    }
  }
  onForeground(): void { native.command('{"type":"active","value":true}'); }
  onBackground(): void { native.command('{"type":"active","value":false}'); }
  onWindowStageDestroy(): void { native.command('{"type":"stop"}'); }
}
