/*
 * time.ts
 */

/** Game time (Unity's Time class). */
export const Time = {
  /** Seconds since the scene started; during a tick, the time of that tick. */
  get time(): number { return __native.time(); },
  /** Seconds since the last frame, or the fixed tick length inside `onTick`. */
  get deltaTime(): number { return __native.deltaTime(); },
  /** Length of one fixed tick in seconds. */
  get fixedDeltaTime(): number { return __native.fixedDeltaTime(); },
  /** Real seconds since startup, not affected by {@link timeScale}. */
  get realtimeSinceStartup(): number { return __native.realtime(); },
  /** Frames rendered since the scene started. */
  get frameCount(): number { return __native.frameCount(); },
  /** How fast game time passes; 0 pauses ticks. */
  get timeScale(): number { return __native.timeScale(); },
  set timeScale(value: number) { __native.setTimeScale(value); },
};
