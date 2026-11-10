import { Behaviour, Vector3, field, script } from "mercury";

@script()
export class OscillatingObstacle extends Behaviour {
  @field({ tooltip: "Direction of translation" }) moveAxis = new Vector3(1, 0, 0);
  @field({ tooltip: "Half-distance of movement" }) distance = 2.5;
  @field({ tooltip: "Oscillation frequency in radians/sec" }) speed = 2;

  #origin = Vector3.zero;
  #time = 0;

  onStart(): void {
    this.#origin = this.transform.position;
  }

  onTick(dt: number): void {
    this.#time += dt;
    const offset = Math.sin(this.#time * this.speed) * this.distance;
    this.transform.position = this.#origin.add(this.moveAxis.scale(offset));
  }
}
