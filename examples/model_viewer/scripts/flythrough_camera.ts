import { Behaviour, Debug, Input, Key, Mathf, NodeRef, Quaternion, Vector3, field, script } from "mercury";

interface Waypoint {
  position: Vector3;
  rotation: Quaternion;
}

function catmullRom(p0: Vector3, p1: Vector3, p2: Vector3, p3: Vector3, t: number): Vector3 {
  const t2 = t * t;
  const t3 = t2 * t;
  // 0.5 * ((2 p1) + (p2 - p0) t + (2 p0 - 5 p1 + 4 p2 - p3) t² + (3 p1 - p0 - 3 p2 + p3) t³)
  const a = p1.scale(2);
  const b = p2.sub(p0).scale(t);
  const c = p0.scale(2).sub(p1.scale(5)).add(p2.scale(4)).sub(p3).scale(t2);
  const d = p1.scale(3).sub(p0).sub(p2.scale(3)).add(p3).scale(t3);
  return a.add(b).add(c).add(d).scale(0.5);
}

// The number after the name's letters (P12 -> 12), so P10 follows P9.
function waypointNumber(name: string): number {
  const match = /(\d+)$/.exec(name);
  return match ? parseInt(match[1], 10) : 0;
}

/**
 * Flies the camera slowly through a scene along a smooth path, for benchmarking: the same views
 * every lap, with frame times recorded per lap.
 *
 * The path is the children of `path` (P0, P1, ...): the camera passes through each one's position
 * and turns to face the way it faces. Space pauses.
 */
@script()
export class FlythroughCamera extends Behaviour {
  @field({ tooltip: "Node whose children are the waypoints, in order" }) path = new NodeRef("Path");
  @field({ tooltip: "Time for one lap of the path" }) lapSeconds = 60;
  @field({ tooltip: "Return to the first waypoint at the end" }) loop = true;

  #points: Waypoint[] = [];
  #time = 0; // seconds along the path
  #paused = false;
  #warmup = 1; // ignore the first frames (uploads, first-use stalls)
  #frameTimes: number[] = [];
  #lastLap = "";
  #laps = 0;

  /** "avg 143.2 fps · 1% low 98.1 · min 80.4" for the last full lap. */
  get lastLapSummary(): string { return this.#lastLap; }
  get laps(): number { return this.#laps; }
  get lapProgress(): number {
    return this.lapSeconds > 0 ? (this.#time % this.lapSeconds) / this.lapSeconds : 0;
  }

  onStart(): void {
    this.#points = [];
    const root = this.get(this.path);
    if (root) {
      const children = root.children.sort((a, b) => waypointNumber(a.name) - waypointNumber(b.name));
      for (const child of children) {
        this.#points.push({ position: child.transform.position, rotation: child.transform.rotation });
      }
    }
    if (this.#points.length === 0) {
      Debug.warn(`FlythroughCamera: ${this.path.path} has no waypoints`);
      return;
    }
    this.#place(0);
  }

  onFrame(dt: number): void {
    // Real frame times, not ticks: this is what the benchmark measures.
    if (this.#paused || this.#points.length < 2) return;
    if (this.#warmup > 0) {
      this.#warmup -= dt;
      return;
    }
    this.#frameTimes.push(dt);
  }

  onTick(dt: number): void {
    if (Input.isKeyJustPressed(Key.Space)) {
      this.#paused = !this.#paused;
      // A paused lap measures nothing: start it again.
      this.#frameTimes = [];
    }
    if (this.#paused || this.#points.length < 2 || this.#warmup > 0) return;
    const before = this.#time;
    this.#time += dt;
    if (Math.floor(this.#time / this.lapSeconds) > Math.floor(before / this.lapSeconds)) {
      this.#finishLap();
      if (!this.loop) {
        this.#time = this.lapSeconds;
        this.#paused = true;
      }
    }
    this.#place((this.#time % this.lapSeconds) / this.lapSeconds);
  }

  #place(t: number): void {
    const count = this.#points.length;
    const segments = this.loop ? count : count - 1;
    const f = Mathf.clamp01(t) * segments;
    const i = Math.min(Math.trunc(f), segments - 1);
    const local = f - i;
    const at = (index: number): Waypoint =>
      this.loop ? this.#points[((index % count) + count) % count] : this.#points[Mathf.clamp(index, 0, count - 1)];
    const position = catmullRom(at(i - 1).position, at(i).position, at(i + 1).position, at(i + 2).position, local);
    // Ease the turn so the camera settles into each waypoint's view.
    const ease = local * local * (3 - 2 * local);
    this.transform.position = position;
    this.transform.rotation = at(i).rotation.slerp(at(i + 1).rotation, ease);
  }

  #finishLap(): void {
    this.#laps++;
    if (this.#frameTimes.length === 0) return;
    const times = this.#frameTimes;
    this.#frameTimes = [];
    const total = times.reduce((sum, t) => sum + t, 0);
    times.sort((a, b) => a - b);
    // 1% low: the frame rate of the slowest 1% of frames.
    const worst = Math.max(1, Math.trunc(times.length / 100));
    const worstTotal = times.slice(times.length - worst).reduce((sum, t) => sum + t, 0);
    const avgFps = times.length / total;
    const lowFps = worst / worstTotal;
    const minFps = 1 / times[times.length - 1];
    this.#lastLap =
      `avg ${avgFps.toFixed(1)} fps · 1% low ${lowFps.toFixed(1)} · min ${minFps.toFixed(1)} ` +
      `(${((1000 * total) / times.length).toFixed(2)} ms avg)`;
    Debug.log(`Lap ${this.#laps}: ${times.length} frames, ${this.#lastLap}`);
  }
}
