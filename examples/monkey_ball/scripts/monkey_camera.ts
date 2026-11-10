import { Behaviour, GameObject, Input, Key, Mathf, NodeRef, Vector3, field, script } from "mercury";

/** Smooth third-person chase camera for Monkey Ball with manual orbit control. */
@script()
export class MonkeyCamera extends Behaviour {
  @field({ tooltip: "Node to follow (typically Player)" }) target = new NodeRef("Player");
  @field({ tooltip: "Distance behind the ball" }) distance = 7;
  @field({ tooltip: "Height above the ball" }) height = 3.5;
  @field({ tooltip: "Downwards look angle in degrees" }) pitch = -22;
  @field({ tooltip: "Horizontal orbit angle in degrees" }) yaw = 0;
  @field({ tooltip: "Rotation speed in degrees/sec when Q/E are held" }) turnSpeed = 90;
  @field({ tooltip: "Follow interpolation rate" }) smoothSpeed = 10;

  #target: GameObject | null = null;

  #offset(): Vector3 {
    const yawRad = this.yaw * Mathf.deg2Rad;
    return new Vector3(Math.sin(yawRad) * this.distance, this.height, Math.cos(yawRad) * this.distance);
  }

  onStart(): void {
    this.#target = this.get(this.target);
    if (this.#target) {
      this.transform.position = this.#target.transform.position.add(this.#offset());
      this.transform.eulerAngles = new Vector3(this.pitch, this.yaw, 0);
    }
  }

  onTick(dt: number): void {
    if (!this.#target) {
      this.#target = this.get(this.target);
      if (!this.#target) return;
    }
    // Camera orbit controls with Q / E.
    if (Input.isKeyPressed(Key.Q)) this.yaw -= this.turnSpeed * dt;
    if (Input.isKeyPressed(Key.E)) this.yaw += this.turnSpeed * dt;
    this.yaw %= 360;

    const targetPos = this.#target.transform.position.add(this.#offset());
    // Smooth lerp to the target position.
    const factor = Math.min(1, this.smoothSpeed * dt);
    this.transform.position = this.transform.position.lerp(targetPos, factor);
    this.transform.eulerAngles = new Vector3(this.pitch, this.yaw, 0);
  }
}
