import AbilityConstant from '@ohos.app.ability.AbilityConstant';
import UIAbility from '@ohos.app.ability.UIAbility';
import Window from '@ohos.window';
import Want from '@ohos.app.ability.Want';
import qpa from 'libplugins_platforms_qopenharmony.so';
import hilog from '@ohos.hilog';
import fs from '@ohos.file.fs';
import fileUri from '@ohos.file.fileuri';

export default class EntryAbility extends UIAbility {
  tag = 'GeanyQt';
  domain = 0x474541;
  private launchApplication = 'libentry.so';
  private launchParams = '';
  private name: string = 'geany_main_window';
  private loadContentUrl: string = 'pages/Index';
  private isForeground: boolean = false;
  private pendingWant?: Want;

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam) {
    this.pendingWant = want;
    hilog.info(this.domain, this.tag, 'onCreate want uri=%{private}s parameters=%{private}s',
      want.uri ?? '', JSON.stringify(want.parameters ?? {}));

    let name = want.parameters?.windowName as string;
    if (name !== undefined && name !== '') {
      this.name = name;
    }

    let applicationContext = this.context.getApplicationContext();
    applicationContext.on('applicationStateChange', {
      onApplicationForeground() {
        console.log('Geany applicationStateChangeCallback onApplicationForeground');
      },
      onApplicationBackground() {
        console.log('Geany applicationStateChangeCallback onApplicationBackground');
      }
    });
  }

  newLocalStorage() {
    return new LocalStorage();
  }

  onDestroy() {
    hilog.info(this.domain, this.tag, '%{private}s', 'Ability onDestroy');
  }

  async onWindowStageCreate(windowStage: Window.WindowStage) {
    hilog.info(this.domain, this.tag, '%{private}s', 'Ability onWindowStageCreate');
    let localStore = new LocalStorage();
    localStore.setOrCreate('idName', this.name);
    let newAbility = this.launchWant.parameters?.newAbility as boolean;

    await this.configurePcWindow(windowStage);
    await this.loadMainContent(windowStage, localStore, newAbility);

    if (newAbility !== undefined) {
      return;
    }
    this.startQtApplication();
  }

  private async loadMainContent(windowStage: Window.WindowStage, localStore: LocalStorage,
    newAbility: boolean | undefined) {
    try {
      await windowStage.loadContent(this.loadContentUrl, localStore);
      qpa.handleJsTopWindowCreated(this.name, this);
      if (newAbility !== undefined) {
        this.context.setMissionLabel(this.launchWant.parameters?.title as string);
      }
    } catch (error) {
      hilog.error(this.domain, this.tag, 'loadContent failed: %{private}s', JSON.stringify(error));
    }
  }

  private async configurePcWindow(windowStage: Window.WindowStage) {
    try {
      let mainWindow = windowStage.getMainWindowSync();
      await mainWindow.resize(2560, 1600);
      await mainWindow.moveWindowToGlobal(280, 160);
      hilog.info(this.domain, this.tag, '%{private}s', 'Configured Harmony PC editor window size');
    } catch (error) {
      hilog.warn(this.domain, this.tag, 'configurePcWindow failed: %{private}s', JSON.stringify(error));
    }
  }

  private async startQtApplication() {
    await this.launchParamSet();
    qpa.startQtApplication(this);
  }

  private queuePathsForNativeOpen(): string[] {
    return [
      `${this.context.cacheDir}/geany-pending-open.txt`,
      `${this.context.filesDir}/geany-pending-open.txt`
    ];
  }

  private queueNativeOpenPath(path: string) {
    if (path === '' || path.startsWith('file://') || path.startsWith('content://')) {
      return;
    }
    for (const queuePath of this.queuePathsForNativeOpen()) {
      try {
        let file = fs.openSync(queuePath, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.APPEND);
        fs.writeSync(file.fd, `${path}\n`);
        fs.closeSync(file);
        hilog.info(this.domain, this.tag, 'Queued native open path %{private}s via %{private}s', path, queuePath);
      } catch (error) {
        hilog.warn(this.domain, this.tag, 'Queue native open path failed: %{private}s', JSON.stringify(error));
      }
    }
  }

  private quoteLaunchParam(value: string): string {
    return '"' + value.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
  }

  private addLaunchParam(params: string[], value: string) {
    if (value === undefined || value === '') {
      return;
    }
    if (value.indexOf(' ') >= 0 || value.indexOf('\t') >= 0) {
      params.push(this.quoteLaunchParam(value));
    } else {
      params.push(value);
    }
  }

  private addUniqueCandidate(values: string[], value: string | undefined) {
    if (value === undefined || value === '' || values.indexOf(value) >= 0) {
      return;
    }
    values.push(value);
  }

  private wantUriCandidates(want: Want | undefined): string[] {
    let values: string[] = [];
    if (want === undefined) {
      return values;
    }

    this.addUniqueCandidate(values, want.uri);
    const parameters = want.parameters;
    this.addUniqueCandidate(values, parameters?.url as string);
    this.addUniqueCandidate(values, parameters?.uri as string);
    this.addUniqueCandidate(values, parameters?.path as string);
    this.addUniqueCandidate(values, parameters?.file as string);
    this.addUniqueCandidate(values, parameters?.['ohos.want.params.uri'] as string);
    this.addUniqueCandidate(values, parameters?.['ohos.aafwk.param.uri'] as string);
    this.addUniqueCandidate(values, parameters?.['ability.params.stream'] as string);
    return values;
  }

  private cachedOpenPath(uri: string): string {
    let sourceName = 'geany-open-file';
    try {
      sourceName = new fileUri.FileUri(uri).name;
    } catch (error) {
      let queryIndex = uri.indexOf('?');
      let cleanUri = queryIndex >= 0 ? uri.substring(0, queryIndex) : uri;
      let slashIndex = cleanUri.lastIndexOf('/');
      if (slashIndex >= 0 && slashIndex < cleanUri.length - 1) {
        sourceName = decodeURIComponent(cleanUri.substring(slashIndex + 1));
      }
    }

    sourceName = sourceName.replace(/[\\/:*?"<>|]/g, '_');
    return `${this.context.cacheDir}/geany-open-${Date.now()}-${sourceName}`;
  }

  private pathFromFileUri(uri: string): string {
    if (!uri.startsWith('file://')) {
      return uri;
    }

    let queryIndex = uri.indexOf('?');
    let cleanUri = queryIndex >= 0 ? uri.substring(0, queryIndex) : uri;
    return decodeURIComponent(cleanUri.substring('file://'.length));
  }

  private async cacheExternalUri(uri: string): Promise<string> {
    if (uri === '' || (!uri.startsWith('file://') && !uri.startsWith('content://'))) {
      return uri;
    }

    let destinationPath = this.cachedOpenPath(uri);
    let destinationUri = fileUri.getUriFromPath(destinationPath);

    try {
      await fs.copy(uri, destinationUri);
      hilog.info(this.domain, this.tag, 'Cached external URI to %{private}s', destinationPath);
      return destinationPath;
    } catch (copyError) {
      hilog.warn(this.domain, this.tag, 'fs.copy failed for URI %{private}s: %{private}s',
        uri, JSON.stringify(copyError));
    }

    try {
      await fs.copyFile(uri, destinationPath, 0);
      hilog.info(this.domain, this.tag, 'Copied external URI to %{private}s', destinationPath);
      return destinationPath;
    } catch (copyFileError) {
      hilog.warn(this.domain, this.tag, 'fs.copyFile failed for URI %{private}s: %{private}s',
        uri, JSON.stringify(copyFileError));
    }

    if (uri.startsWith('file://')) {
      try {
        await fs.copyFile(this.pathFromFileUri(uri), destinationPath, 0);
        hilog.info(this.domain, this.tag, 'Copied external file URI path to %{private}s', destinationPath);
        return destinationPath;
      } catch (localPathCopyError) {
        hilog.warn(this.domain, this.tag, 'fs.copyFile path fallback failed for URI %{private}s: %{private}s',
          uri, JSON.stringify(localPathCopyError));
      }
      let localPath = this.pathFromFileUri(uri);
      if (localPath !== '') {
        hilog.warn(this.domain, this.tag, 'Using external file URI path without cache: %{private}s', localPath);
        return localPath;
      }
    }

    return uri;
  }

  async launchParamSet() {
    let params: string[] = [];
    let want = this.launchWant ?? this.pendingWant;

    hilog.info(this.domain, this.tag, 'launchParamSet want uri=%{private}s parameters=%{private}s',
      want?.uri ?? '', JSON.stringify(want?.parameters ?? {}));

    for (const candidate of this.wantUriCandidates(want)) {
      const cachedPath = await this.cacheExternalUri(candidate);
      this.addLaunchParam(params, cachedPath);
      this.queueNativeOpenPath(cachedPath);
    }


    this.launchParams = params.join(' ');
    hilog.info(this.domain, this.tag, 'launchParams: %{private}s', this.launchParams);
  }

  onWindowStageDestroy() {
    hilog.info(this.domain, this.tag, '%{private}s', 'Ability onWindowStageDestroy');
  }

  onForeground() {
    this.isForeground = true;
    hilog.info(this.domain, this.tag, '%{private}s', 'Ability onForeground');
  }

  onBackground() {
    this.isForeground = false;
    hilog.info(this.domain, this.tag, '%{private}s', 'Ability onBackground');
  }

  onNewWant(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(this.domain, this.tag, 'onNewWant uri=%{private}s parameters=%{private}s',
      want.uri ?? '', JSON.stringify(want.parameters ?? {}));
    this.prepareNewWant(want, launchParam);
  }

  private async prepareNewWant(want: Want, launchParam: AbilityConstant.LaunchParam) {
    const candidates = this.wantUriCandidates(want);
    for (const candidate of candidates) {
      const cachedPath = await this.cacheExternalUri(candidate);
      this.queueNativeOpenPath(cachedPath);
      if (want.uri === candidate) {
        want.uri = cachedPath;
      }
    }
    qpa.handleOnNewWant(want, launchParam);
  }
}
