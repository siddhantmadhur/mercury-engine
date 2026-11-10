/*
 * native.d.ts
 *
 * The functions the engine provides to the runtime library. Internal: scripts use the classes in
 * "mercury", never `__native`. Objects are always passed as a (scene serial, entity id) pair, and
 * vectors come back through `scratch`.
 */

interface ScriptFieldSpec {
  name: string;
  type: string;
  elementType: string;
  enumValues: string[];
  assetType: string;
  tooltip: string;
}

interface ScriptSpec {
  name: string;
  location: string;
  ctor: new () => unknown;
  fields: ScriptFieldSpec[];
  bindables: string[];
  callables: { name: string; arg: string }[];
  callbackMask: number;
  dataAsset: boolean;
}

declare const __native: {
  /** Float64Array(16) used to return vectors without allocating. */
  readonly scratch: Float64Array;

  registerScript(spec: ScriptSpec): void;
  setHelper(name: string, fn: (...args: any[]) => unknown): void;
  mapStack(stack: string): string;
  eventInvoke(behaviourId: number, field: string): void;

  randomSeed(seed: number): void;
  random(): number;
  time(): number;
  deltaTime(): number;
  fixedDeltaTime(): number;
  realtime(): number;
  frameCount(): number;
  timeScale(): number;
  setTimeScale(scale: number): void;
  /** level: 0 debug, 1 info, 2 warn, 3 error */
  log(level: number, text: string): void;

  objectIsValid(scene: number, entity: number): boolean;
  objectName(scene: number, entity: number): string;
  objectPath(scene: number, entity: number): string;
  objectTag(scene: number, entity: number): string;
  objectSetTag(scene: number, entity: number, tag: string): void;
  objectLayer(scene: number, entity: number): number;
  objectSetLayer(scene: number, entity: number, layer: number): void;
  objectActiveSelf(scene: number, entity: number): boolean;
  objectActiveInHierarchy(scene: number, entity: number): boolean;
  objectSetActive(scene: number, entity: number, active: boolean): void;
  objectParent(scene: number, entity: number): number;
  objectChildren(scene: number, entity: number): number[];
  objectFindChild(scene: number, entity: number, path: string): number;
  objectSetParent(scene: number, entity: number, parent: number, keepWorld: boolean): void;
  objectDestroy(scene: number, entity: number): void;
  /** True if the object has a mesh; its bounds (entity space) are in scratch[0..5]. */
  objectBounds(scene: number, entity: number): boolean;
  objectBehaviours(scene: number, entity: number): object[];

  getPosition(scene: number, entity: number): void;
  setPosition(scene: number, entity: number, x: number, y: number, z: number): void;
  getRotation(scene: number, entity: number): void;
  setRotation(scene: number, entity: number, x: number, y: number, z: number, w: number): void;
  getEuler(scene: number, entity: number): void;
  setEuler(scene: number, entity: number, x: number, y: number, z: number): void;
  getScale(scene: number, entity: number): void;
  setScale(scene: number, entity: number, x: number, y: number, z: number): void;
  getVelocity(scene: number, entity: number): void;
  setVelocity(scene: number, entity: number, x: number, y: number, z: number): void;

  /** The serial of the scene whose script is running now. */
  sceneCurrent(): number;
  sceneFind(scene: number, path: string): number;
  sceneFindWithTag(scene: number, tag: string): number;
  sceneFindAllWithTag(scene: number, tag: string): number[];
  sceneInstantiate(
    scene: number, path: string, px: number, py: number, pz: number,
    qx: number, qy: number, qz: number, qw: number, parent: number,
  ): number;
  sceneCreate(scene: number, name: string, parent: number): number;
  sceneLoadMap(scene: number, path: string): void;
  sceneQuit(scene: number): void;
  sceneAllBehaviours(scene: number): object[];
  behaviourSetEnabled(id: number, enabled: boolean): void;
  behaviourIsEnabled(id: number): boolean;

  addForce(scene: number, entity: number, x: number, y: number, z: number, mode: number): void;
  addImpulse(scene: number, entity: number, x: number, y: number, z: number): void;
  addTorque(scene: number, entity: number, x: number, y: number, z: number, mode: number): void;
  setAngularVelocity(scene: number, entity: number, x: number, y: number, z: number): void;
  /** Returns the hit entity or -1; point, normal and distance are in scratch[0..6]. */
  raycast(
    scene: number, ox: number, oy: number, oz: number, dx: number, dy: number, dz: number,
    maxDistance: number, layerMask: number, queryTriggers: boolean,
  ): number;

  isKeyPressed(scene: number, key: number): boolean;
  isKeyJustPressed(scene: number, key: number): boolean;
  isMouseButtonPressed(scene: number, button: number): boolean;
  isPointerOverUI(scene: number): boolean;
  cursorDelta(scene: number): void;
  frameScroll(scene: number): void;
  inputActionFind(scene: number, name: string): number;
  inputActionRead(scene: number, index: number): number;
  inputActionState(scene: number, index: number, which: number): boolean;
  inputActionEnable(scene: number, index: number, enable: boolean): void;
  inputMapEnable(scene: number, map: string, enable: boolean): void;

  loadDataAsset(scene: number, className: string, path: string): object;

  debugBegin(title: string, x: number, y: number): void;
  debugText(text: string): void;
  debugEnd(): void;

  uiRoot(scene: number, entity: number): number;
  uiQuery(handle: number, name: string, className: string): number;
  uiSetText(handle: number, text: string): void;
  uiGetText(handle: number): string;
  uiOnClick(handle: number, fn: () => void): void;
  uiToggleValue(handle: number, value?: boolean): boolean;
  uiOnToggleChanged(handle: number, fn: (value: boolean) => void): void;
  uiClass(handle: number, name: string, enable: boolean): void;
  uiFocus(handle: number): void;
  uiHasClass(handle: number, name: string): boolean;
  uiBlurFocused(handle: number): void;
  uiSetDisplay(handle: number, visible: boolean): void;
  uiSetDataSource(handle: number, behaviourId: number): void;

  animatorSet(scene: number, entity: number, kind: number, name: string, value?: number | boolean): void;
  animatorGet(scene: number, entity: number, kind: number, name: string): number | boolean;
  animatorPlay(scene: number, entity: number, state: string, layer: number, time: number): void;
  animatorState(scene: number, entity: number, layer: number): string;
  audioCall(scene: number, entity: number, op: number): boolean | undefined;
  audioPlayOneShot(scene: number, entity: number, clip: string, volume: number): void;
  audioSet(scene: number, entity: number, property: string, value: number | boolean | string): void;
  characterMove(scene: number, entity: number, x: number, y: number, z: number): number;
  characterGrounded(scene: number, entity: number): boolean;
  characterVelocity(scene: number, entity: number): void;

  // Generic component access (generated component classes use these).
  componentHas(scene: number, entity: number, type: string): boolean;
  componentGet(scene: number, entity: number, type: string, field: string): unknown;
  componentSet(scene: number, entity: number, type: string, field: string, value: unknown): void;
  componentAdd(scene: number, entity: number, type: string, props: Record<string, unknown>): void;
};
