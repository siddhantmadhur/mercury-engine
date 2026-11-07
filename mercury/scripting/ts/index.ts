/*
 * index.ts
 *
 * The "mercury" module: everything a game script can import.
 */
// This must run first: decorators.ts installs Symbol.metadata before any user class is defined.
import { finalizeScripts } from "./decorators";
import { GameObject } from "./gameobject";
import { Color, Vector2, Vector3 } from "./math";
import { AssetRef, NodeRef } from "./refs";
import { pumpTimers, setIntervalGame, setTimeoutGame, clearTimer } from "./timers";

export * from "./math";
export * from "./time";
export * from "./refs";
export * from "./event";
export * from "./behaviour";
export * from "./gameobject";
export * from "./components";
export * from "./decorators";
export * from "./physics";
export * from "./input";
export * from "./ui";
export * from "./scene";
export * from "./debug";
export { wait } from "./timers";
export * from "./generated/components";

// Functions the engine calls from C++.
__native.setHelper("construct", (ctor: new () => unknown) => new ctor());
__native.setHelper("makeGameObject", (scene: number, entity: number) => new GameObject(scene, entity));
__native.setHelper("newVector2", (x: number, y: number) => new Vector2(x, y));
__native.setHelper("newVector3", (x: number, y: number, z: number) => new Vector3(x, y, z));
__native.setHelper("newColor", (r: number, g: number, b: number, a: number) => new Color(r, g, b, a));
__native.setHelper("newNodeRef", (path: string) => new NodeRef(path));
__native.setHelper("newAssetRef", (path: string) => new AssetRef(path));
__native.setHelper("finalizeScripts", finalizeScripts);
__native.setHelper("pumpTimers", pumpTimers);

// Timers on game time (setTimeout takes milliseconds, like the web).
const g = globalThis as any;
g.setTimeout = setTimeoutGame;
g.setInterval = setIntervalGame;
g.clearTimeout = clearTimer;
g.clearInterval = clearTimer;
