/*
 * behaviour.ts
 */
import { GameObject, Transform } from "./gameobject";
import { InputActionAsset } from "./input";
import { Debug } from "./debug";
import type { NodeRef } from "./refs";
import { UIDocument, VisualElement } from "./ui";

/**
 * Base class of game scripts (Unity's MonoBehaviour). Declare a script with `@script()`:
 *
 * ```ts
 * @script()
 * class Ball extends Behaviour {
 *   @field() speed = 60;
 *   onTick(dt: number) { this.transform.translate(Vector3.right.scale(this.speed * dt)); }
 * }
 * ```
 *
 * Override any of the lifecycle methods below. The constructor must not call engine APIs (the
 * object does not exist yet): use field initializers there and `awake`/`onStart` for the rest.
 * Add the script to a node in the editor (or with `[component type="Ball" node="..."]` in the
 * `.mmap`); `@field`s show in the Inspector and are saved in the map.
 */
export abstract class Behaviour {
  /** @internal set by the engine right after construction */
  __id = 0;
  /** @internal */
  __scene = 0;
  /** @internal */
  __entity = 0;

  /** The object this script is on. */
  get gameObject(): GameObject { return new GameObject(this.__scene, this.__entity); }
  /** Shortcut for `gameObject.transform`. */
  get transform(): Transform { return this.gameObject.transform; }
  /** Disabled scripts get no `onStart`, `onFrame` or `onTick`. */
  get enabled(): boolean { return __native.behaviourIsEnabled(this.__id); }
  set enabled(value: boolean) { __native.behaviourSetEnabled(this.__id, value); }

  /** The UIDocument on this object, or null. */
  get document(): UIDocument | null {
    const h = __native.uiRoot(this.__scene, this.__entity);
    return h < 0 ? null : new UIDocument(new VisualElement(h));
  }
  /** The project's input actions (Unity's Input System). */
  get inputActions(): InputActionAsset { return new InputActionAsset(this.__scene); }

  /** Resolves a {@link NodeRef} field; null (with a warning) if nothing is at that path. */
  get(ref: NodeRef): GameObject | null {
    if (!ref.isSet) return null;
    const e = __native.sceneFind(this.__scene, ref.path);
    if (e < 0) {
      Debug.warn(`${this.gameObject.path}: no node at path "${ref.path}"`);
      return null;
    }
    return new GameObject(this.__scene, e);
  }

  // --- lifecycle ------------------------------------------------------------------------
  /** As soon as the object exists, even if this script is disabled. */
  awake?(): void;
  /** Whenever the script becomes enabled on an active object. */
  onEnable?(): void;
  onDisable?(): void;
  /** Before the first tick or frame the script is enabled for. */
  onStart?(): void;
  /** Every rendered frame (Unity's Update). */
  onFrame?(dt: number): void;
  /** Every frame after every script's `onFrame` (Unity's LateUpdate). */
  onLateFrame?(dt: number): void;
  /** Every fixed tick, before physics runs (Unity's FixedUpdate). */
  onTick?(dt: number): void;
  /** Collisions and triggers are reported at the start of the next tick. */
  onCollisionEnter?(other: GameObject): void;
  onCollisionStay?(other: GameObject): void;
  onCollisionLeave?(other: GameObject): void;
  onTriggerEnter?(other: GameObject): void;
  onTriggerStay?(other: GameObject): void;
  onTriggerExit?(other: GameObject): void;
  /** When the object is removed. */
  onDestroy?(): void;
}
