/*
 * decorators.ts
 *
 * The decorators that declare scripts and their fields. They are standard (TC39) decorators, so
 * the project's tsconfig must NOT set `experimentalDecorators`.
 *
 * A script is registered when its class is defined. Field types are worked out afterwards from
 * the default values (see finalizeScripts), once every module has been evaluated, so a field
 * initializer may refer to anything declared later in its file.
 */
import { Behaviour } from "./behaviour";
import { Color, Vector2, Vector3 } from "./math";
import { AssetRef, NodeRef } from "./refs";
import { Event } from "./event";

// Standard decorators store their data in `context.metadata`, which needs Symbol.metadata.
// esbuild's and tsc's decorator lowering both look it up when the class is defined.
(Symbol as any).metadata ??= Symbol.for("Symbol.metadata");

const FIELDS = Symbol.for("mercury.fields");
const BINDABLES = Symbol.for("mercury.bindables");
const CALLABLES = Symbol.for("mercury.callables");

type Metadata = Record<PropertyKey, unknown>;

interface FieldMeta {
  name: string;
  kind: "field" | "int" | "enum" | "asset";
  tooltip?: string;
  enumValues?: string[];
  assetType?: string;
  element?: "int" | "float" | "string" | "nodeRef" | "asset";
}

/** Options for {@link field}. */
export interface FieldOptions {
  /** Hover text in the Inspector. */
  tooltip?: string;
  /** The item type of an array that is empty by default. */
  element?: "int" | "float" | "string" | "nodeRef" | "asset";
}

// Gives this class its own list (copied from its parent's), so subclasses add to it.
function own<T>(metadata: Metadata, key: symbol): T[] {
  if (!Object.hasOwn(metadata, key)) metadata[key] = [...((metadata[key] as T[] | undefined) ?? [])];
  return metadata[key] as T[];
}

function checkMember(context: { name: string | symbol; static: boolean; private: boolean }, what: string): string {
  if (context.static) throw new Error(`@${what} cannot be used on a static member ("${String(context.name)}")`);
  if (context.private) throw new Error(`@${what} cannot be used on a #private member ("${String(context.name)}")`);
  if (typeof context.name !== "string") throw new Error(`@${what} needs a string member name`);
  return context.name;
}

function addField(context: ClassFieldDecoratorContext, what: string, meta: Omit<FieldMeta, "name">): void {
  const name = checkMember(context, what);
  const list = own<FieldMeta>(context.metadata as Metadata, FIELDS);
  const existing = list.findIndex((f) => f.name === name);
  const entry: FieldMeta = { name, ...meta };
  if (existing >= 0) list[existing] = entry; else list.push(entry);
}

/**
 * Shows a member in the Inspector and saves it in the map. The type comes from the default value:
 * `number` is a float, and `boolean`, `string`, `Vector2`, `Vector3`, `Color`, `NodeRef`,
 * `AssetRef`, `Event` and arrays of those work too. The map key is the property name.
 */
export function field(options: FieldOptions = {}) {
  return (_value: undefined, context: ClassFieldDecoratorContext): void =>
    addField(context, "field", { kind: "field", tooltip: options.tooltip, element: options.element });
}

/** A whole-number field (the default type of a `number` is float). */
export function int(options: FieldOptions = {}) {
  return (_value: undefined, context: ClassFieldDecoratorContext): void =>
    addField(context, "int", { kind: "int", tooltip: options.tooltip });
}

/** A string field restricted to `values`, shown as a drop-down. */
export function enumField(values: string[], options: FieldOptions = {}) {
  return (_value: undefined, context: ClassFieldDecoratorContext): void =>
    addField(context, "enumField", { kind: "enum", enumValues: [...values], tooltip: options.tooltip });
}

/** An {@link AssetRef} field that only accepts assets of one type ("Material", "Texture", ...). */
export function asset(assetType: string, options: FieldOptions = {}) {
  return (_value: undefined, context: ClassFieldDecoratorContext): void =>
    addField(context, "asset", { kind: "asset", assetType, tooltip: options.tooltip });
}

/**
 * A value UI bindings may read, without saving it or showing it in the Inspector (Unity's
 * `[CreateProperty]`). Put it on a getter or a field.
 */
export function bindable() {
  return (_value: unknown, context: ClassGetterDecoratorContext | ClassFieldDecoratorContext | ClassAccessorDecoratorContext): void => {
    const name = checkMember(context as any, "bindable");
    const list = own<string>(context.metadata as Metadata, BINDABLES);
    if (!list.includes(name)) list.push(name);
  };
}

/**
 * A method an Event saved in the map may call. `arg` is the type of its one argument: "bool",
 * "int", "float" or "string"; leave it out for a method without arguments.
 */
export function callable(arg: "" | "bool" | "int" | "float" | "string" = "") {
  return (_value: Function, context: ClassMethodDecoratorContext): void => {
    const name = checkMember(context, "callable");
    const list = own<{ name: string; arg: string }>(context.metadata as Metadata, CALLABLES);
    if (!list.some((c) => c.name === name)) list.push({ name, arg });
  };
}

// --- registration ---------------------------------------------------------------------------

interface Pending {
  name: string;
  ctor: new () => unknown;
  metadata: Metadata;
  location: string;
  dataAsset: boolean;
}

const pending: Pending[] = [];
// The registered name of each data asset class, for Assets.load.
const dataAssetNames = new Map<Function, string>();

const CALLBACKS = [
  "awake", "onEnable", "onDisable", "onStart", "onFrame", "onLateFrame", "onTick",
  "onCollisionEnter", "onCollisionStay", "onCollisionLeave",
  "onTriggerEnter", "onTriggerStay", "onTriggerExit", "onDestroy",
];

function whereDefined(): string {
  const stack = __native.mapStack(new Error().stack ?? "");
  const frame = stack.split("\n").find((l) => /\.ts:\d+:\d+/.test(l) && !l.includes("decorators.ts"));
  return frame ? frame.replace(/^\s*at\s+/, "").replace(/^.*\((.*)\)$/, "$1").trim() : "<unknown>";
}

function register(dataAsset: boolean, name: string | undefined) {
  return (value: Function, context: ClassDecoratorContext): void => {
    if (!dataAsset && !(value.prototype instanceof Behaviour)) {
      throw new Error(`@script classes must extend Behaviour (${String(context.name)})`);
    }
    // The class name is a string literal after lowering, so minification cannot change it.
    const scriptName = name ?? String(context.name);
    if (dataAsset) dataAssetNames.set(value, scriptName);
    pending.push({
      name: scriptName,
      ctor: value as new () => unknown,
      metadata: context.metadata as Metadata,
      location: whereDefined(),
      dataAsset,
    });
  };
}

/**
 * Declares a script class (it must extend {@link Behaviour}). Maps refer to it by the class name,
 * or by `name` if given: `[component type="Ball" node="..."]`.
 */
export function script(name?: string) { return register(false, name); }

/** Declares a data asset type (a ScriptableObject), saved as a `.masset` file. */
export function dataAsset(name?: string) { return register(true, name); }

function inferField(meta: FieldMeta, value: unknown): ScriptFieldSpec {
  const spec: ScriptFieldSpec = {
    name: meta.name, type: "string", elementType: "string",
    enumValues: meta.enumValues ?? [], assetType: meta.assetType ?? "", tooltip: meta.tooltip ?? "",
  };
  const kindOf = (v: unknown): string => {
    if (typeof v === "number") return "float";
    if (typeof v === "boolean") return "bool";
    if (typeof v === "string") return "string";
    if (v instanceof Vector2) return "vector2";
    if (v instanceof Vector3) return "vector3";
    if (v instanceof Color) return "color";
    if (v instanceof NodeRef) return "nodeRef";
    if (v instanceof AssetRef) return "asset";
    if (v instanceof Event) return "event";
    if (Array.isArray(v)) return "list";
    throw new Error(`@field "${meta.name}" has a value of an unsupported type`);
  };
  if (meta.kind === "enum") {
    spec.type = "enum";
  } else if (meta.kind === "asset") {
    spec.type = "asset";
  } else if (meta.kind === "int") {
    spec.type = "int";
  } else {
    spec.type = kindOf(value);
    if (spec.type === "list") {
      const first = (value as unknown[])[0];
      spec.elementType = first !== undefined ? kindOf(first).replace("float", "float") : (meta.element ?? "string");
      if (typeof first === "number" && meta.element === "int") spec.elementType = "int";
      if (first === undefined && meta.element === undefined) {
        throw new Error(`@field "${meta.name}" is an empty array: say its item type with @field({ element: "int" })`);
      }
    }
  }
  return spec;
}

/**
 * Describes every registered script: builds a throwaway instance to read the default of each field
 * and registers the result with the engine. Called by the engine after the bundle has run.
 * @internal
 */
export function finalizeScripts(): void {
  const list = pending.splice(0);
  for (const p of list) {
    const metadata = p.metadata;
    const fieldMetas = ((metadata[FIELDS] as FieldMeta[] | undefined) ?? []);
    const instance = new p.ctor() as Record<string, unknown>;
    const fields = fieldMetas.map((m) => inferField(m, instance[m.name]));
    let mask = 0;
    for (let i = 0; i < CALLBACKS.length; i++) {
      if (typeof (p.ctor as any).prototype[CALLBACKS[i]] === "function") mask |= 1 << i;
    }
    try {
      __native.registerScript({
        name: p.name,
        location: p.location,
        ctor: p.ctor,
        fields,
        bindables: [...((metadata[BINDABLES] as string[] | undefined) ?? [])],
        callables: [...((metadata[CALLABLES] as { name: string; arg: string }[] | undefined) ?? [])],
        callbackMask: mask,
        dataAsset: p.dataAsset,
      });
    } catch (e) {
      console.error(e instanceof Error ? e.message : String(e));
    }
  }
}

/** Loading project assets. */
export const Assets = {
  /**
   * Loads a `.masset` file as an instance of a `@dataAsset` class (Unity's ScriptableObject):
   * `Assets.load(LevelData, "res://assets/level1.masset")`.
   */
  load<T extends object>(type: new () => T, path: string): T {
    const name = dataAssetNames.get(type);
    if (!name) throw new Error(`${type.name} is not a @dataAsset class`);
    return __native.loadDataAsset(__native.sceneCurrent(), name, path) as T;
  },
};
