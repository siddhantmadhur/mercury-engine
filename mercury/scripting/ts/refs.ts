/*
 * refs.ts
 *
 * References that are saved in a map: to another node (by path) and to a project file.
 */

/**
 * A reference to another node of the same map, by path from the root ("Ball", "Walls/Left"). Set
 * it in the Inspector, then resolve it with {@link Behaviour.get}.
 */
export class NodeRef {
  constructor(readonly path: string = "") {}
  /** True if the reference points somewhere. */
  get isSet(): boolean { return this.path !== ""; }
}

/** A reference to a file in the project, as a `res://` path. */
export class AssetRef {
  constructor(readonly path: string = "") {}
  /** True if the reference points somewhere. */
  get isSet(): boolean { return this.path !== ""; }
}
