import {
  Behaviour, GameObject, Input, Key, NodeRef, Physics, Scene, Vector2, Vector3, field, script,
} from "mercury";
import { BananaPickup } from "./banana_pickup";
import { GameManager } from "./game_manager";
import { GoalTrigger } from "./goal_trigger";

@script()
export class PlayerBall extends Behaviour {
  @field({ tooltip: "Continuous push force applied when rolling" }) rollForce = 24;
  @field({ tooltip: "Upward impulse applied when Space is pressed" }) jumpImpulse = 7.5;
  @field({ tooltip: "Maximum horizontal speed before damping is applied" }) maxSpeed = 20;
  @field({ tooltip: "Camera node for orienting movement direction" }) cameraNode = new NodeRef("Camera");

  #contactCount = 0;
  #grounded = false;
  #jumpCooldown = 0;

  get isGrounded(): boolean { return this.#grounded; }

  onStart(): void {
    this.#contactCount = 0;
    this.#grounded = true;
    this.#jumpCooldown = 0;
  }

  resetPhysics(): void {
    this.#contactCount = 0;
    this.#grounded = true;
    this.#jumpCooldown = 0;
    this.transform.velocity = Vector3.zero;
    Physics.setAngularVelocity(this.gameObject, Vector3.zero);
  }

  onTick(dt: number): void {
    // No rolling while the pause menu is open.
    if (Scene.find("GameManager")?.getBehaviour(GameManager)?.isPaused) return;

    this.#jumpCooldown = Math.max(0, this.#jumpCooldown - dt);
    const vel = this.transform.velocity;
    const pos = this.transform.position;

    // Ground detection: either resting contact or negligible vertical movement.
    this.#grounded = this.#contactCount > 0 || Math.abs(vel.y) < 0.2;

    // Determine movement axes relative to the camera look direction.
    let forward = new Vector3(0, 0, -1);
    const cam = this.get(this.cameraNode);
    if (cam) {
      const toBall = pos.sub(cam.transform.position).with({ y: 0 });
      if (toBall.length > 1e-3) forward = toBall.normalized;
    }
    const right = new Vector3(-forward.z, 0, forward.x);

    // Gather directional input.
    let inputDir = Vector3.zero;
    if (Input.isKeyPressed(Key.W) || Input.isKeyPressed(Key.Up)) inputDir = inputDir.add(forward);
    if (Input.isKeyPressed(Key.S) || Input.isKeyPressed(Key.Down)) inputDir = inputDir.sub(forward);
    if (Input.isKeyPressed(Key.A) || Input.isKeyPressed(Key.Left)) inputDir = inputDir.sub(right);
    if (Input.isKeyPressed(Key.D) || Input.isKeyPressed(Key.Right)) inputDir = inputDir.add(right);

    if (inputDir.length > 1e-3) {
      inputDir = inputDir.normalized;
      Physics.addForce(this.gameObject, inputDir.scale(this.rollForce));
      // Active steering & braking: if horizontal velocity opposes input, apply a snappy counter-force.
      const horizVel = new Vector2(vel.x, vel.z);
      const horizInput = new Vector2(inputDir.x, inputDir.z);
      if (horizVel.dot(horizInput) < 0) {
        Physics.addForce(this.gameObject, inputDir.scale(this.rollForce * 0.7));
      }
    }

    // Soft speed damping if the player rolls faster than maxSpeed.
    const horizSpeed = Math.hypot(vel.x, vel.z);
    if (horizSpeed > this.maxSpeed) {
      const excess = horizSpeed - this.maxSpeed;
      const brake = new Vector3((-vel.x / horizSpeed) * excess * 5, 0, (-vel.z / horizSpeed) * excess * 5);
      Physics.addForce(this.gameObject, brake);
    }

    // Jump execution.
    if (Input.isKeyJustPressed(Key.Space) && this.#grounded && this.#jumpCooldown <= 0) {
      Physics.addImpulse(this.gameObject, new Vector3(0, this.jumpImpulse, 0));
      this.#grounded = false;
      this.#jumpCooldown = 0.25;
    }
  }

  #isGhost(other: GameObject): boolean {
    return !!other.getBehaviour(BananaPickup) || !!other.getBehaviour(GoalTrigger);
  }

  onCollisionEnter(other: GameObject): void {
    if (this.#isGhost(other)) return;
    this.#contactCount++;
  }

  onCollisionLeave(other: GameObject): void {
    if (this.#isGhost(other)) return;
    this.#contactCount = Math.max(0, this.#contactCount - 1);
  }
}
