/*
 * globals.d.ts
 *
 * The only globals a script can use: console and the timer functions. There is no DOM and no
 * Node, so `window`, `document` and `require` are compile errors.
 */

interface Console {
  log(...args: unknown[]): void;
  info(...args: unknown[]): void;
  warn(...args: unknown[]): void;
  error(...args: unknown[]): void;
}
declare const console: Console;

/** Runs `fn` once after `ms` milliseconds of game time (it stops while Time.timeScale is 0). */
declare function setTimeout(fn: () => void, ms?: number): number;
/** Runs `fn` every `ms` milliseconds of game time. */
declare function setInterval(fn: () => void, ms?: number): number;
declare function clearTimeout(id: number | undefined): void;
declare function clearInterval(id: number | undefined): void;
