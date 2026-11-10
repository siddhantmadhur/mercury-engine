/*
 * input.ts
 */
import { Vector2 } from "./math";

/** Keyboard keys (GLFW key codes). */
export enum Key {
  Unknown = -1, Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
  Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  Semicolon = 59, Equal = 61,
  A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
  Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267, Home = 268, End = 269,
  F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  LeftShift = 340, LeftControl = 341, LeftAlt = 342, RightShift = 344, RightControl = 345, RightAlt = 346,
}

/** Mouse buttons. */
export enum MouseButton { Left = 0, Right = 1, Middle = 2 }

/** Keyboard and mouse input. */
export const Input = {
  /** True while the key is held down. */
  isKeyPressed(key: Key): boolean { return __native.isKeyPressed(__native.sceneCurrent(), key); },
  /** True during the tick or frame the key went down. */
  isKeyJustPressed(key: Key): boolean { return __native.isKeyJustPressed(__native.sceneCurrent(), key); },
  /** True while the mouse button is held down. */
  isMouseButtonPressed(button: MouseButton): boolean {
    return __native.isMouseButtonPressed(__native.sceneCurrent(), button);
  },
  /** True while the pointer is over in-game UI (so clicks are meant for it). */
  get isPointerOverUI(): boolean { return __native.isPointerOverUI(__native.sceneCurrent()); },
  /** How far the cursor moved since last frame, in pixels. */
  get cursorDelta(): Vector2 {
    __native.cursorDelta(__native.sceneCurrent());
    return new Vector2(__native.scratch[0], __native.scratch[1]);
  },
  /** Scroll wheel movement this frame. */
  get scroll(): Vector2 {
    __native.frameScroll(__native.sceneCurrent());
    return new Vector2(__native.scratch[0], __native.scratch[1]);
  },
};

/** One input action from the project's Input Actions asset (Unity's Input System). */
export class InputAction {
  constructor(private readonly scene: number, private readonly index: number) {}

  /** The current value as a number (a button is 0 or 1; a 2D action gives its x). */
  readFloat(): number { return __native.inputActionRead(this.scene, this.index); }
  /** The current value as a 2D vector (a stick or WASD composite). */
  readVector2(): Vector2 {
    __native.inputActionRead(this.scene, this.index);
    return new Vector2(__native.scratch[0], __native.scratch[1]);
  }
  get isPressed(): boolean { return __native.inputActionState(this.scene, this.index, 0); }
  get wasPressedThisFrame(): boolean { return __native.inputActionState(this.scene, this.index, 1); }
  get wasReleasedThisFrame(): boolean { return __native.inputActionState(this.scene, this.index, 2); }
  enable(): void { __native.inputActionEnable(this.scene, this.index, true); }
  disable(): void { __native.inputActionEnable(this.scene, this.index, false); }
}

/** The project's input actions. */
export class InputActionAsset {
  constructor(private readonly scene: number) {}
  /** Finds an action by "Map/Action" or by its bare name; null if there is none. */
  find(name: string): InputAction | null {
    const index = __native.inputActionFind(this.scene, name);
    return index < 0 ? null : new InputAction(this.scene, index);
  }
  /** Like {@link find}, but throws if there is no such action. */
  require(name: string): InputAction {
    const action = this.find(name);
    if (!action) throw new Error(`The project has no input action "${name}"`);
    return action;
  }
  /** Enables every action of a map. */
  enableMap(map: string): void { __native.inputMapEnable(this.scene, map, true); }
  disableMap(map: string): void { __native.inputMapEnable(this.scene, map, false); }
}
