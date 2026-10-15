declare const qpa: {
  attachAbilityStage(stage: object): void;
  getJsAbilityInstanceKey(): string | undefined;
  handleJsTopWindowCreated(name: string, ability: object): void;
  startQtApplication(ability: object, session?: object): void;
  handleOnNewWant(want: object, launchParam: object): void;
};

export default qpa;

