/*
 * physics.ts
 */
import { GameObject } from "./gameobject";
import { Vector3 } from "./math";

/** What a ray hit. */
export interface RaycastHit {
  readonly gameObject: GameObject;
  readonly point: Vector3;
  readonly normal: Vector3;
  readonly distance: number;
}

/** Physics queries. */
export const Physics = {
  /**
   * The closest hit along a ray, or null.
   * @param layerMask bit N set: hit objects on layer N (default: everything but layer 2)
   */
  raycast(
    origin: Vector3,
    direction: Vector3,
    maxDistance = 1e30,
    layerMask = ~(1 << 2),
    queryTriggers = true,
  ): RaycastHit | null {
    const scene = __native.sceneCurrent();
    const entity = __native.raycast(
      scene, origin.x, origin.y, origin.z, direction.x, direction.y, direction.z,
      maxDistance, layerMask >>> 0, queryTriggers,
    );
    if (entity < 0) return null;
    const s = __native.scratch;
    return {
      gameObject: new GameObject(scene, entity),
      point: new Vector3(s[0], s[1], s[2]),
      normal: new Vector3(s[3], s[4], s[5]),
      distance: s[6],
    };
  },
  /** Sets the angular velocity of an object's rigidbody, in radians per second. */
  setAngularVelocity(object: GameObject, velocity: Vector3): void {
    __native.setAngularVelocity(object.sceneSerial, object.entity, velocity.x, velocity.y, velocity.z);
  },
  /** Adds an instantaneous impulse (using mass) to an object's rigidbody. */
  addImpulse(object: GameObject, impulse: Vector3): void {
    __native.addImpulse(object.sceneSerial, object.entity, impulse.x, impulse.y, impulse.z);
  },
  /** Adds a force to an object's rigidbody. */
  addForce(object: GameObject, force: Vector3): void { object.addForce(force); },
  /** Adds a torque to an object's rigidbody. */
  addTorque(object: GameObject, torque: Vector3): void { object.addTorque(torque); },
};
