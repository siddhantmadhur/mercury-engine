import { Behaviour, Debug, Mathf, Random, Vector3, field, script } from "mercury";

/**
 * Serves the ball and keeps it moving at a constant speed.
 *
 * Moving paddles add energy on contact, and a near-vertical ball would never reach either side,
 * so onTick corrects both.
 */
@script()
export class Ball extends Behaviour {
  @field({ tooltip: "World units per second" }) speed = 60;
  @field({ tooltip: "Smallest share of the direction that is horizontal" }) minHorizontal = 0.45;
  @field({ tooltip: "Serves go within this many degrees of horizontal" }) serveAngle = 35;

  #servedThisTick = false;

  onStart(): void {
    this.serve();
  }

  /** Back to the center, heading towards a random side. Only the first call in a tick counts. */
  serve(): void {
    if (this.#servedThisTick) return;
    this.#servedThisTick = true;
    Debug.log("Starting new round...");

    const degree = this.serveAngle > 0 ? Random.range(-this.serveAngle, this.serveAngle) : 0;
    const side = Random.value() < 0.5 ? 1 : -1;
    const radians = degree * Mathf.deg2Rad;
    const direction = new Vector3(side * Math.cos(radians), Math.sin(radians), 0);

    this.transform.position = Vector3.zero;
    this.transform.velocity = direction.scale(this.speed);
  }

  onTick(): void {
    // A velocity set this tick overrides that tick's bounces, so leave a fresh serve alone.
    if (this.#servedThisTick) {
      this.#servedThisTick = false;
      return;
    }

    const velocity = this.transform.velocity;
    const current = velocity.length;
    if (current <= 0) return;

    let dir = velocity.scale(1 / current);
    let correct = Math.abs(current - this.speed) > 1;
    if (Math.abs(dir.x) < this.minHorizontal) {
      const x = Math.sign(dir.x === 0 ? 1 : dir.x) * this.minHorizontal;
      const y = Math.sign(dir.y) * Math.sqrt(1 - this.minHorizontal * this.minHorizontal);
      dir = dir.with({ x, y });
      correct = true;
    }
    if (correct) this.transform.velocity = dir.scale(this.speed);
  }
}
