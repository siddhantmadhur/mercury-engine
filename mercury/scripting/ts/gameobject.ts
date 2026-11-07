/*
 * gameobject.ts
 *
 * Handles to objects of the running scene. A handle holds two numbers (a scene serial and an
 * entity id), never a pointer, so a destroyed object is an error you can catch ("GameObject was
 * destroyed") instead of a crash. Check {@link GameObject.isValid} if an object may be gone.
 */
import type { Component, ComponentType } from "./components";
import { Quaternion, Vector3 } from "./math";
import type { Behaviour } from "./behaviour";

/** How a force is applied. */
export enum ForceMode {
  /** Continuous force, using mass (F = m * a). */
  Force = 0,
  /** Instantaneous impulse, using mass. */
  Impulse = 1,
  /** Instantaneous velocity change, ignoring mass. */
  VelocityChange = 2,
  /** Continuous acceleration, ignoring mass. */
  Acceleration = 3,
}

const scratch = (): Float64Array => __native.scratch;

/** Position, rotation, scale and velocity of an object. */
export class Transform {
  constructor(readonly gameObject: GameObject) {}

  /** World position. */
  get position(): Vector3 {
    __native.getPosition(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = scratch();
    return new Vector3(s[0], s[1], s[2]);
  }
  set position(value: Vector3) {
    __native.setPosition(this.gameObject.sceneSerial, this.gameObject.entity, value.x, value.y, value.z);
  }
  /** World rotation. */
  get rotation(): Quaternion {
    __native.getRotation(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = scratch();
    return new Quaternion(s[0], s[1], s[2], s[3]);
  }
  set rotation(value: Quaternion) {
    __native.setRotation(this.gameObject.sceneSerial, this.gameObject.entity, value.x, value.y, value.z, value.w);
  }
  /** Rotation as Euler angles in degrees (pitch, yaw, roll). */
  get eulerAngles(): Vector3 {
    __native.getEuler(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = scratch();
    return new Vector3(s[0], s[1], s[2]);
  }
  set eulerAngles(value: Vector3) {
    __native.setEuler(this.gameObject.sceneSerial, this.gameObject.entity, value.x, value.y, value.z);
  }
  /** Scale relative to the parent. */
  get localScale(): Vector3 {
    __native.getScale(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = scratch();
    return new Vector3(s[0], s[1], s[2]);
  }
  set localScale(value: Vector3) {
    __native.setScale(this.gameObject.sceneSerial, this.gameObject.entity, value.x, value.y, value.z);
  }
  /** Linear velocity of the object's rigidbody, in world units per second. */
  get velocity(): Vector3 {
    __native.getVelocity(this.gameObject.sceneSerial, this.gameObject.entity);
    const s = scratch();
    return new Vector3(s[0], s[1], s[2]);
  }
  set velocity(value: Vector3) {
    __native.setVelocity(this.gameObject.sceneSerial, this.gameObject.entity, value.x, value.y, value.z);
  }

  /** Moves the object by `delta` in world space. */
  translate(delta: Vector3): void { this.position = this.position.add(delta); }
  /** Rotates by Euler angles in degrees, applied after the current rotation. */
  rotate(eulerDegrees: Vector3): void {
    this.rotation = Quaternion.euler(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z).mul(this.rotation);
  }
  /** Turns the object so its forward axis (-Z) points at `target`. */
  lookAt(target: Vector3, up: Vector3 = Vector3.up): void {
    const forward = target.sub(this.position).normalized;
    if (forward.sqrLength === 0) return;
    const right = forward.cross(up).normalized;
    const realUp = right.cross(forward);
    // Rotation matrix with columns right, up, -forward (RH, -Z forward) to a quaternion.
    const m00 = right.x, m01 = realUp.x, m02 = -forward.x;
    const m10 = right.y, m11 = realUp.y, m12 = -forward.y;
    const m20 = right.z, m21 = realUp.z, m22 = -forward.z;
    const trace = m00 + m11 + m22;
    let q: Quaternion;
    if (trace > 0) {
      const s = Math.sqrt(trace + 1) * 2;
      q = new Quaternion((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s);
    } else if (m00 > m11 && m00 > m22) {
      const s = Math.sqrt(1 + m00 - m11 - m22) * 2;
      q = new Quaternion(0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
    } else if (m11 > m22) {
      const s = Math.sqrt(1 + m11 - m00 - m22) * 2;
      q = new Quaternion((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s);
    } else {
      const s = Math.sqrt(1 + m22 - m00 - m11) * 2;
      q = new Quaternion((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s);
    }
    this.rotation = q.normalized;
  }
}

/** An object of the scene. */
export class GameObject {
  #transform?: Transform;

  constructor(
    /** @internal */ readonly sceneSerial: number,
    /** @internal */ readonly entity: number,
  ) {}

  /** False once the object (or its scene) is gone. */
  get isValid(): boolean { return __native.objectIsValid(this.sceneSerial, this.entity); }
  get name(): string { return __native.objectName(this.sceneSerial, this.entity); }
  /** Path from the map root, e.g. "Walls/Left". */
  get path(): string { return __native.objectPath(this.sceneSerial, this.entity); }
  get tag(): string { return __native.objectTag(this.sceneSerial, this.entity); }
  set tag(value: string) { __native.objectSetTag(this.sceneSerial, this.entity, value); }
  get layer(): number { return __native.objectLayer(this.sceneSerial, this.entity); }
  set layer(value: number) { __native.objectSetLayer(this.sceneSerial, this.entity, value); }
  /** The object's own active flag. */
  get activeSelf(): boolean { return __native.objectActiveSelf(this.sceneSerial, this.entity); }
  /** True if the object and all its parents are active. */
  get activeInHierarchy(): boolean { return __native.objectActiveInHierarchy(this.sceneSerial, this.entity); }
  setActive(active: boolean): void { __native.objectSetActive(this.sceneSerial, this.entity, active); }
  compareTag(tag: string): boolean { return this.tag === tag; }

  get transform(): Transform { return (this.#transform ??= new Transform(this)); }

  get parent(): GameObject | null {
    const p = __native.objectParent(this.sceneSerial, this.entity);
    return p < 0 ? null : new GameObject(this.sceneSerial, p);
  }
  get children(): GameObject[] {
    return __native.objectChildren(this.sceneSerial, this.entity).map((e) => new GameObject(this.sceneSerial, e));
  }
  /** A direct child by name, or a descendant by "A/B" path. */
  findChild(path: string): GameObject | null {
    const e = __native.objectFindChild(this.sceneSerial, this.entity, path);
    return e < 0 ? null : new GameObject(this.sceneSerial, e);
  }
  setParent(parent: GameObject | null, keepWorldTransform = true): void {
    __native.objectSetParent(this.sceneSerial, this.entity, parent ? parent.entity : -1, keepWorldTransform);
  }
  /** The bounds of the object's mesh in its own space (before scale), or null if it has none. */
  get bounds(): { min: Vector3; max: Vector3 } | null {
    if (!__native.objectBounds(this.sceneSerial, this.entity)) return null;
    const s = scratch();
    return { min: new Vector3(s[0], s[1], s[2]), max: new Vector3(s[3], s[4], s[5]) };
  }
  /** Removes the object and its children at the end of the current tick or frame. */
  destroy(): void { __native.objectDestroy(this.sceneSerial, this.entity); }

  /** An engine component on this object, e.g. `getComponent(Rigidbody)`, or null. */
  getComponent<T extends Component>(type: ComponentType<T>): T | null {
    return __native.componentHas(this.sceneSerial, this.entity, type.typeName) ? new type(this) : null;
  }
  /** Adds an engine component, optionally with initial field values. */
  addComponent<T extends Component>(type: ComponentType<T>, properties: Record<string, unknown> = {}): T {
    __native.componentAdd(this.sceneSerial, this.entity, type.typeName, properties);
    return new type(this);
  }
  /** A script on this object, e.g. `getBehaviour(Ball)`, or null. */
  getBehaviour<T extends Behaviour>(type: abstract new (...args: any[]) => T): T | null {
    for (const b of __native.objectBehaviours(this.sceneSerial, this.entity)) {
      if (b instanceof type) return b as T;
    }
    return null;
  }
  /** Every script of this type on this object. */
  getBehaviours<T extends Behaviour>(type: abstract new (...args: any[]) => T): T[] {
    return __native.objectBehaviours(this.sceneSerial, this.entity).filter((b): b is T => b instanceof type);
  }
  /** Adds a force to the object's rigidbody. */
  addForce(force: Vector3, mode: ForceMode = ForceMode.Force): void {
    __native.addForce(this.sceneSerial, this.entity, force.x, force.y, force.z, mode);
  }
  addTorque(torque: Vector3, mode: ForceMode = ForceMode.Force): void {
    __native.addTorque(this.sceneSerial, this.entity, torque.x, torque.y, torque.z, mode);
  }

  equals(other: GameObject | null | undefined): boolean {
    return !!other && other.sceneSerial === this.sceneSerial && other.entity === this.entity;
  }
}
