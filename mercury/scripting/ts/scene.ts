/*
 * scene.ts
 */
import type { Behaviour } from "./behaviour";
import { GameObject } from "./gameobject";
import { Quaternion, Vector3 } from "./math";
import type { AssetRef } from "./refs";

function wrap(scene: number, entity: number): GameObject | null {
  return entity < 0 ? null : new GameObject(scene, entity);
}

/** The running scene: finding, creating and destroying objects. */
export const Scene = {
  /** Finds an object by path from the map root ("Walls/Left"), or by name anywhere. */
  find(path: string): GameObject | null {
    const scene = __native.sceneCurrent();
    return wrap(scene, __native.sceneFind(scene, path));
  },
  /** The first active object with this tag. */
  findWithTag(tag: string): GameObject | null {
    const scene = __native.sceneCurrent();
    return wrap(scene, __native.sceneFindWithTag(scene, tag));
  },
  findGameObjectsWithTag(tag: string): GameObject[] {
    const scene = __native.sceneCurrent();
    return __native.sceneFindAllWithTag(scene, tag).map((e) => new GameObject(scene, e));
  },
  /** Every enabled script of this type on an active object. */
  findObjectsOfType<T extends Behaviour>(type: abstract new (...args: any[]) => T): T[] {
    return __native.sceneAllBehaviours(__native.sceneCurrent()).filter((b): b is T => b instanceof type);
  },
  /** Copies a `.mprefab` into the scene (Unity's Object.Instantiate). */
  instantiate(
    prefab: AssetRef | string,
    position: Vector3 = Vector3.zero,
    rotation: Quaternion = Quaternion.identity,
    parent: GameObject | null = null,
  ): GameObject {
    const scene = __native.sceneCurrent();
    const path = typeof prefab === "string" ? prefab : prefab.path;
    const entity = __native.sceneInstantiate(
      scene, path, position.x, position.y, position.z,
      rotation.x, rotation.y, rotation.z, rotation.w, parent ? parent.entity : -1,
    );
    return new GameObject(scene, entity);
  },
  /** A new empty object (Unity's `new GameObject(name)`). */
  createGameObject(name: string, parent: GameObject | null = null): GameObject {
    const scene = __native.sceneCurrent();
    return new GameObject(scene, __native.sceneCreate(scene, name, parent ? parent.entity : -1));
  },
  /** Removes an object and its children at the end of the current tick or frame. */
  destroy(object: GameObject): void { object.destroy(); },
  /** Replaces the scene with another map at the start of the next frame. */
  loadMap(resPath: string): void { __native.sceneLoadMap(__native.sceneCurrent(), resPath); },
  /** Ends the game. */
  quit(): void { __native.sceneQuit(__native.sceneCurrent()); },
};
