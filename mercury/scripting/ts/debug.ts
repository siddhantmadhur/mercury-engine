/*
 * debug.ts
 */

function format(args: unknown[]): string {
  return args.map((a) => (typeof a === "string" ? a : a instanceof Error ? `${a.stack ?? a.message}` : String(a))).join(" ");
}

/** Logging and developer windows. */
export const Debug = {
  /** Writes to the Console. */
  log(...args: unknown[]): void { __native.log(0, format(args)); },
  info(...args: unknown[]): void { __native.log(1, format(args)); },
  warn(...args: unknown[]): void { __native.log(2, format(args)); },
  error(...args: unknown[]): void { __native.log(3, format(args)); },

  /**
   * A small developer window (not for player-facing UI; use a UIDocument for that). Call it every
   * frame, e.g. from `onFrame`; `draw` adds lines of text.
   *
   * ```ts
   * Debug.window("Ball", (w) => w.text(`speed ${this.speed}`), 10, 70);
   * ```
   */
  window(title: string, draw: (window: { text(line: string): void }) => void, x = 10, y = 10): void {
    __native.debugBegin(title, x, y);
    try {
      draw({ text: (line) => __native.debugText(line) });
    } finally {
      __native.debugEnd();
    }
  },
};
