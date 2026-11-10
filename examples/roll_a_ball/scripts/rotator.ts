import { Behaviour, Vector3, field, script } from "mercury";

/** Spins the node at a constant rate. */
@script()
export class Rotator extends Behaviour {
  @field() degreesPerSecond = new Vector3(15, 30, 45);

  onTick(dt: number): void {
    const next = this.transform.eulerAngles.add(this.degreesPerSecond.scale(dt));
    this.transform.eulerAngles = new Vector3(next.x % 360, next.y % 360, next.z % 360);
  }
}
