import { privacyManager } from '@kit.AppGalleryKit';
import { commonEventManager } from '@kit.BasicServicesKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { common } from '@kit.AbilityKit';

// Huawei owns the consent UI and the versioned record. Never substitute a local flag.
export class HostedPrivacy {
  private subscriber?: commonEventManager.CommonEventSubscriber;
  private disposed: boolean = false;

  isAgreed(): boolean {
    const info = privacyManager.getAppPrivacyMgmtInfo();
    if (info.type !== privacyManager.AppPrivacyMgmtType.FULL_MODE) return false;
    const results = privacyManager.getAppPrivacyResult();
    const policies = info.privacyInfo.filter(link =>
      link.type === privacyManager.AppPrivacyLinkType.PRIVACY_STATEMENT_LINK);
    return policies.length > 0 && policies.every(link => results.some(result =>
      result.type === privacyManager.AppPrivacyType.PRIVACY_AGREEMENT &&
      result.id === link.id && result.versionCode === link.versionCode &&
      result.result === privacyManager.AppPrivacyResultType.FULL_MODE_AGREED));
  }

  async observe(changed: () => void): Promise<void> {
    const subscriber = await commonEventManager.createSubscriber({
      events: ['usual.event.PRIVACY_STATE_CHANGED']
    });
    if (this.disposed) return;
    this.subscriber = subscriber;
    commonEventManager.subscribe(subscriber, (error) => {
      // The broadcast is a wakeup only; query this app's authoritative record again.
      if (!error && !this.disposed) changed();
    });
  }

  async request(context: common.UIAbilityContext): Promise<void> {
    await privacyManager.requestAppPrivacyConsent(context);
  }

  async showPolicy(context: common.UIAbilityContext): Promise<void> {
    const policy = privacyManager.getAppPrivacyMgmtInfo().privacyInfo.find(link =>
      link.type === privacyManager.AppPrivacyLinkType.PRIVACY_STATEMENT_LINK);
    if (!policy || !policy.url.startsWith('https://')) throw new Error('Hosted privacy policy unavailable');
    await context.openLink(policy.url);
  }

  revoke(): void { privacyManager.disableService(); }

  dispose(): void {
    this.disposed = true;
    if (this.subscriber) {
      try {
        commonEventManager.unsubscribe(this.subscriber, (error) => {
          if (error) hilog.error(0, 'HostedPrivacy', 'Unable to unsubscribe privacy event');
        });
      } catch (e) { hilog.error(0, 'HostedPrivacy', 'Unable to unsubscribe privacy event'); }
    }
    this.subscriber = undefined;
  }
}
