/*
 * math.ts
 *
 * Immutable vectors, quaternions and colors, plus Mathf and Random. Every operation returns a
 * new value, so `transform.position.x = 5` is a compile error instead of a write that silently
 * does nothing. Use `transform.position = transform.position.with({ x: 5 })` instead.
 */

/** A 2D vector. Immutable: every method returns a new vector. */
export class Vector2 {
  constructor(readonly x: number = 0, readonly y: number = 0) {}

  static readonly zero = new Vector2(0, 0);
  static readonly one = new Vector2(1, 1);
  static readonly up = new Vector2(0, 1);
  static readonly down = new Vector2(0, -1);
  static readonly left = new Vector2(-1, 0);
  static readonly right = new Vector2(1, 0);

  /** A copy with some components replaced. */
  with(c: { x?: number; y?: number }): Vector2 {
    return new Vector2(c.x ?? this.x, c.y ?? this.y);
  }
  add(o: Vector2): Vector2 { return new Vector2(this.x + o.x, this.y + o.y); }
  sub(o: Vector2): Vector2 { return new Vector2(this.x - o.x, this.y - o.y); }
  /** Multiplies every component by `s`. */
  scale(s: number): Vector2 { return new Vector2(this.x * s, this.y * s); }
  dot(o: Vector2): number { return this.x * o.x + this.y * o.y; }
  get length(): number { return Math.hypot(this.x, this.y); }
  get sqrLength(): number { return this.x * this.x + this.y * this.y; }
  /** The vector scaled to length 1, or zero if its length is zero. */
  get normalized(): Vector2 {
    const l = this.length;
    return l > 1e-9 ? this.scale(1 / l) : Vector2.zero;
  }
  distanceTo(o: Vector2): number { return this.sub(o).length; }
  lerp(o: Vector2, t: number): Vector2 {
    return new Vector2(this.x + (o.x - this.x) * t, this.y + (o.y - this.y) * t);
  }
  equals(o: Vector2): boolean { return this.x === o.x && this.y === o.y; }
  toString(): string { return `(${this.x}, ${this.y})`; }
}

/** A 3D vector. Immutable: every method returns a new vector. */
export class Vector3 {
  constructor(readonly x: number = 0, readonly y: number = 0, readonly z: number = 0) {}

  static readonly zero = new Vector3(0, 0, 0);
  static readonly one = new Vector3(1, 1, 1);
  static readonly up = new Vector3(0, 1, 0);
  static readonly down = new Vector3(0, -1, 0);
  static readonly left = new Vector3(-1, 0, 0);
  static readonly right = new Vector3(1, 0, 0);
  static readonly forward = new Vector3(0, 0, -1);
  static readonly back = new Vector3(0, 0, 1);

  /** A copy with some components replaced. */
  with(c: { x?: number; y?: number; z?: number }): Vector3 {
    return new Vector3(c.x ?? this.x, c.y ?? this.y, c.z ?? this.z);
  }
  add(o: Vector3): Vector3 { return new Vector3(this.x + o.x, this.y + o.y, this.z + o.z); }
  sub(o: Vector3): Vector3 { return new Vector3(this.x - o.x, this.y - o.y, this.z - o.z); }
  /** Multiplies every component by `s`. */
  scale(s: number): Vector3 { return new Vector3(this.x * s, this.y * s, this.z * s); }
  dot(o: Vector3): number { return this.x * o.x + this.y * o.y + this.z * o.z; }
  cross(o: Vector3): Vector3 {
    return new Vector3(
      this.y * o.z - this.z * o.y,
      this.z * o.x - this.x * o.z,
      this.x * o.y - this.y * o.x,
    );
  }
  get length(): number { return Math.hypot(this.x, this.y, this.z); }
  get sqrLength(): number { return this.x * this.x + this.y * this.y + this.z * this.z; }
  /** The vector scaled to length 1, or zero if its length is zero. */
  get normalized(): Vector3 {
    const l = this.length;
    return l > 1e-9 ? this.scale(1 / l) : Vector3.zero;
  }
  distanceTo(o: Vector3): number { return this.sub(o).length; }
  lerp(o: Vector3, t: number): Vector3 {
    return new Vector3(
      this.x + (o.x - this.x) * t,
      this.y + (o.y - this.y) * t,
      this.z + (o.z - this.z) * t,
    );
  }
  equals(o: Vector3): boolean { return this.x === o.x && this.y === o.y && this.z === o.z; }
  toString(): string { return `(${this.x}, ${this.y}, ${this.z})`; }
}

/** A rotation. Immutable. */
export class Quaternion {
  constructor(
    readonly x: number = 0,
    readonly y: number = 0,
    readonly z: number = 0,
    readonly w: number = 1,
  ) {}

  static readonly identity = new Quaternion(0, 0, 0, 1);

  /** A rotation of `degrees` around `axis`. */
  static angleAxis(degrees: number, axis: Vector3): Quaternion {
    const a = axis.normalized;
    const h = (degrees * Math.PI) / 360;
    const s = Math.sin(h);
    return new Quaternion(a.x * s, a.y * s, a.z * s, Math.cos(h));
  }
  /**
   * A rotation from Euler angles in degrees. X is applied first, then Y, then Z (the engine's
   * order, the same as Transform.eulerAngles).
   */
  static euler(x: number, y: number, z: number): Quaternion {
    return Quaternion.angleAxis(z, Vector3.back)
      .mul(Quaternion.angleAxis(y, Vector3.up))
      .mul(Quaternion.angleAxis(x, Vector3.right));
  }

  /** `this` followed by `o`: the rotation that applies `o` first, then `this`. */
  mul(o: Quaternion): Quaternion {
    return new Quaternion(
      this.w * o.x + this.x * o.w + this.y * o.z - this.z * o.y,
      this.w * o.y - this.x * o.z + this.y * o.w + this.z * o.x,
      this.w * o.z + this.x * o.y - this.y * o.x + this.z * o.w,
      this.w * o.w - this.x * o.x - this.y * o.y - this.z * o.z,
    );
  }
  /** Rotates a vector. */
  rotate(v: Vector3): Vector3 {
    const u = new Vector3(this.x, this.y, this.z);
    const t = u.cross(v).scale(2);
    return v.add(t.scale(this.w)).add(u.cross(t));
  }
  get inverse(): Quaternion { return new Quaternion(-this.x, -this.y, -this.z, this.w); }
  get normalized(): Quaternion {
    const l = Math.hypot(this.x, this.y, this.z, this.w);
    return l > 1e-9 ? new Quaternion(this.x / l, this.y / l, this.z / l, this.w / l) : Quaternion.identity;
  }
  /** Euler angles in degrees, in the order described at {@link Quaternion.euler}. */
  get eulerAngles(): Vector3 {
    const { x, y, z, w } = this;
    // R = Rz * Ry * Rx
    const sinY = Math.min(1, Math.max(-1, 2 * (w * y - x * z)));
    const pitchY = Math.asin(sinY);
    const roll = Math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z));
    const pitch = Math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y));
    const d = 180 / Math.PI;
    return new Vector3(pitch * d, pitchY * d, roll * d);
  }
  slerp(o: Quaternion, t: number): Quaternion {
    let cos = this.x * o.x + this.y * o.y + this.z * o.z + this.w * o.w;
    let b = o;
    if (cos < 0) {
      cos = -cos;
      b = new Quaternion(-o.x, -o.y, -o.z, -o.w);
    }
    if (cos > 0.9995) {
      return new Quaternion(
        this.x + (b.x - this.x) * t,
        this.y + (b.y - this.y) * t,
        this.z + (b.z - this.z) * t,
        this.w + (b.w - this.w) * t,
      ).normalized;
    }
    const theta = Math.acos(cos);
    const s = Math.sin(theta);
    const a0 = Math.sin((1 - t) * theta) / s;
    const a1 = Math.sin(t * theta) / s;
    return new Quaternion(
      this.x * a0 + b.x * a1,
      this.y * a0 + b.y * a1,
      this.z * a0 + b.z * a1,
      this.w * a0 + b.w * a1,
    );
  }
  equals(o: Quaternion): boolean {
    return this.x === o.x && this.y === o.y && this.z === o.z && this.w === o.w;
  }
}

/** An RGBA color with components from 0 to 1. Immutable. */
export class Color {
  constructor(
    readonly r: number = 1,
    readonly g: number = 1,
    readonly b: number = 1,
    readonly a: number = 1,
  ) {}
  static readonly white = new Color(1, 1, 1, 1);
  static readonly black = new Color(0, 0, 0, 1);
  static readonly red = new Color(1, 0, 0, 1);
  static readonly green = new Color(0, 1, 0, 1);
  static readonly blue = new Color(0, 0, 1, 1);
  static readonly yellow = new Color(1, 0.92, 0.016, 1);
  static readonly clear = new Color(0, 0, 0, 0);

  with(c: { r?: number; g?: number; b?: number; a?: number }): Color {
    return new Color(c.r ?? this.r, c.g ?? this.g, c.b ?? this.b, c.a ?? this.a);
  }
  lerp(o: Color, t: number): Color {
    return new Color(
      this.r + (o.r - this.r) * t,
      this.g + (o.g - this.g) * t,
      this.b + (o.b - this.b) * t,
      this.a + (o.a - this.a) * t,
    );
  }
  equals(o: Color): boolean {
    return this.r === o.r && this.g === o.g && this.b === o.b && this.a === o.a;
  }
}

/** Common math helpers (Unity's Mathf). */
export const Mathf = {
  PI: Math.PI,
  deg2Rad: Math.PI / 180,
  rad2Deg: 180 / Math.PI,
  clamp(v: number, min: number, max: number): number { return Math.min(Math.max(v, min), max); },
  clamp01(v: number): number { return Math.min(Math.max(v, 0), 1); },
  lerp(a: number, b: number, t: number): number { return a + (b - a) * Mathf.clamp01(t); },
  /** Moves `current` toward `target` by at most `maxDelta`. */
  moveTowards(current: number, target: number, maxDelta: number): number {
    return Math.abs(target - current) <= maxDelta ? target : current + Math.sign(target - current) * maxDelta;
  },
  /** The shortest signed angle in degrees from `a` to `b`. */
  deltaAngle(a: number, b: number): number {
    const d = (((b - a) % 360) + 540) % 360 - 180;
    return d;
  },
  sign(v: number): number { return v >= 0 ? 1 : -1; },
  approximately(a: number, b: number): boolean { return Math.abs(a - b) < 1e-6; },
} as const;

/** Random numbers (Unity's Random). */
export const Random = {
  /** Seeds the generator, so a run can be repeated. */
  seed(seed: number): void { __native.randomSeed(seed); },
  /** A float in [0, 1). */
  value(): number { return __native.random(); },
  /** A float in [min, max). */
  range(min: number, max: number): number { return min + (max - min) * __native.random(); },
  /** An integer in [min, max). */
  rangeInt(min: number, max: number): number { return Math.floor(min + (max - min) * __native.random()); },
} as const;
