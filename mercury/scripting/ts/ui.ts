/*
 * ui.ts
 *
 * UI Toolkit elements. A VisualElement holds a handle to an element of the game's UI; it throws
 * "UI element was destroyed" if the UIDocument it belonged to is gone.
 */
import type { Behaviour } from "./behaviour";

/** Something with a handle to a UI element (internal). */
type ElementCtor<T extends VisualElement> = new (handle: number) => T;

/** Any element of a UI document. */
export class VisualElement {
  constructor(/** @internal */ readonly handle: number) {}

  /**
   * Finds a descendant by its `name` (and, optionally, USS class), like UI Toolkit's `Q`.
   * Pass the element type to get its methods: `root.q("play", Button)`.
   */
  q<T extends VisualElement = VisualElement>(
    name: string,
    type?: ElementCtor<T>,
    className = "",
  ): T | null {
    const h = __native.uiQuery(this.handle, name, className);
    return h < 0 ? null : new (type ?? (VisualElement as unknown as ElementCtor<T>))(h);
  }
  /** Finds a descendant by USS class name only. */
  qClass<T extends VisualElement = VisualElement>(className: string, type?: ElementCtor<T>): T | null {
    return this.q("", type, className);
  }
  /** Adds or removes a USS class. */
  enableInClassList(className: string, enable: boolean): void { __native.uiClass(this.handle, className, enable); }
  /** True if the element has the USS class. */
  hasClass(className: string): boolean { return __native.uiHasClass(this.handle, className); }
  /** Shows (display: flex) or hides (display: none) the element. */
  set display(visible: boolean) { __native.uiSetDisplay(this.handle, visible); }
  focus(): void { __native.uiFocus(this.handle); }
  /** Takes focus away from the focused element, so Enter and the arrow keys go back to the game. */
  blurFocused(): void { __native.uiBlurFocused(this.handle); }
  /**
   * Binds the element and its children to a script: its `@field`s and `@bindable` getters become
   * the binding paths used in the UXML.
   */
  setDataSource(script: Behaviour): void { __native.uiSetDataSource(this.handle, script.__id); }
}

/** A text label. */
export class Label extends VisualElement {
  get text(): string { return __native.uiGetText(this.handle); }
  set text(value: string) { __native.uiSetText(this.handle, value); }
}

/** A button. */
export class Button extends VisualElement {
  get text(): string { return __native.uiGetText(this.handle); }
  set text(value: string) { __native.uiSetText(this.handle, value); }
  /** Adds a handler that runs when the button is clicked (Unity's `button.clicked += fn`). */
  onClick(handler: () => void): void { __native.uiOnClick(this.handle, handler); }
}

/** A checkbox. */
export class Toggle extends VisualElement {
  get text(): string { return __native.uiGetText(this.handle); }
  set text(value: string) { __native.uiSetText(this.handle, value); }
  get value(): boolean { return __native.uiToggleValue(this.handle); }
  set value(v: boolean) { __native.uiToggleValue(this.handle, v); }
  /** Adds a handler that runs when the value changes. */
  onValueChanged(handler: (value: boolean) => void): void { __native.uiOnToggleChanged(this.handle, handler); }
}

/** The UI document on an object. */
export class UIDocument {
  constructor(/** @internal */ readonly root: VisualElement) {}
  get rootVisualElement(): VisualElement { return this.root; }
}
