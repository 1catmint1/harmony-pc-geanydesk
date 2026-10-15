import { AbilityStage } from '@kit.AbilityKit';
import Want from '@ohos.app.ability.Want';
import qpa from 'libplugins_platforms_qopenharmony.so';
import hilog from '@ohos.hilog';

export default class MyAbilityStage extends AbilityStage {
  private static isFirstLaunch: boolean = true;
  tag = 'GeanyQt';
  domain = 0x474541;

  onCreate(): void {
    qpa.attachAbilityStage(this);
  }

  onAcceptWant(want: Want): string {
    hilog.info(this.domain, this.tag, 'onAcceptWant want: %{public}s', JSON.stringify(want.parameters));
    if (want.parameters?.instanceKey === undefined) {
      if (MyAbilityStage.isFirstLaunch) {
        MyAbilityStage.isFirstLaunch = false;
        return 'geany_main_window';
      }

      let instanceKey: string | undefined = qpa.getJsAbilityInstanceKey();
      if (instanceKey !== undefined) {
        return instanceKey;
      }
      return 'geany_main_window';
    }

    return `${want.parameters?.instanceKey}`;
  }
}

