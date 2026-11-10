/*
 * components.ts
 *
 * Engine components attached to objects (Rigidbody, Animator, ...). The classes for the built-in
 * components are generated for each project from the engine's TypeRegistry into
 * `generated/components.ts`, with a typed property for every field.
 */
import type { GameObject } from "./gameobject";

/** The class of a component, as passed to `getComponent`. */
export interface ComponentType<T extends Component> {
  /** The name the component has in maps and the Inspector. */
  readonly typeName: string;
  new (gameObject: GameObject): T;
}

/** Base class of the engine's components. */
export abstract class Component {
  constructor(readonly gameObject: GameObject) {}

  /** @internal reads one field of this component */
  protected getField(type: string, field: string): unknown {
    return __native.componentGet(this.gameObject.sceneSerial, this.gameObject.entity, type, field);
  }
  /** @internal writes one field of this component */
  protected setField(type: string, field: string, value: unknown): void {
    __native.componentSet(this.gameObject.sceneSerial, this.gameObject.entity, type, field, value);
  }
}
