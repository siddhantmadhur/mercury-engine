/*
 * event.ts
 */


/**
 * A list of calls saved with the map (set up in the Inspector) plus listeners added from code,
 * like Unity's UnityEvent.
 *
 * ```ts
 * @field() entered = new Event();
 * onTriggerEnter() { this.entered.invoke(); }
 * ```
 *
 * A saved call names a node, a script on it and one of that script's `@callable` methods.
 */
export class Event {
  /** @internal set by the engine when the owning script is created */
  __id = 0;
  /** @internal */
  __field = "";
  #listeners = new Map<number, () => void>();
  #nextId = 1;

  /** Runs every saved call, then every listener added from code. */
  invoke(): void {
    if (this.__id !== 0) __native.eventInvoke(this.__id, this.__field);
    for (const listener of [...this.#listeners.values()]) listener();
  }
  /** Adds a listener; returns an id for {@link removeListener}. */
  addListener(listener: () => void): number {
    const id = this.#nextId++;
    this.#listeners.set(id, listener);
    return id;
  }
  removeListener(id: number): void { this.#listeners.delete(id); }
  /** Removes the listeners added from code; saved calls stay. */
  removeAllListeners(): void { this.#listeners.clear(); }
}
