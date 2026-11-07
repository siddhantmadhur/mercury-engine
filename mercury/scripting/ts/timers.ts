/*
 * timers.ts
 *
 * setTimeout/setInterval and `wait` run on game time (they stop while Time.timeScale is 0).
 * The engine calls `__pumpTimers` once per fixed tick.
 */

interface Timer {
  id: number;
  due: number;
  interval: number; // 0: once
  fn: () => void;
}

const timers = new Map<number, Timer>();
let nextId = 1;

function add(fn: () => void, ms: number, repeat: boolean): number {
  const id = nextId++;
  const seconds = Math.max(0, (ms ?? 0) / 1000);
  timers.set(id, { id, due: __native.time() + seconds, interval: repeat ? Math.max(seconds, 0.001) : 0, fn });
  return id;
}

export function setTimeoutGame(fn: () => void, ms = 0): number { return add(fn, ms, false); }
export function setIntervalGame(fn: () => void, ms = 0): number { return add(fn, ms, true); }
export function clearTimer(id: number | undefined): void { if (id !== undefined) timers.delete(id); }

/** @internal called by the engine every fixed tick */
export function pumpTimers(): void {
  if (timers.size === 0) return;
  const now = __native.time();
  for (const timer of [...timers.values()]) {
    if (!timers.has(timer.id) || timer.due > now) continue;
    if (timer.interval > 0) timer.due += timer.interval; else timers.delete(timer.id);
    try {
      timer.fn();
    } catch (e) {
      console.error(e instanceof Error ? (e.stack ?? e.message) : String(e));
    }
  }
}

/**
 * Resolves after `seconds` of game time, for use in async coroutines:
 *
 * ```ts
 * async flash() { this.visible = false; await wait(0.2); this.visible = true; }
 * ```
 */
export function wait(seconds: number): Promise<void> {
  return new Promise((resolve) => { add(resolve, seconds * 1000, false); });
}
