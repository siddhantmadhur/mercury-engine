import { Behaviour, Vector3, field, script } from "mercury";

@script()
export class RotatingHazard extends Behaviour {
  @field({ tooltip: "Euler rotation speed in degrees per second" })
  degreesPerSecond = new Vector3(0, 60, 0);

  onTick(dt: number): void {
    const rot = this.transform.eulerAngles.add(this.degreesPerSecond.scale(dt));
    this.transform.eulerAngles = new Vector3(rot.x % 360, rot.y % 360, rot.z % 360);
  }
}
