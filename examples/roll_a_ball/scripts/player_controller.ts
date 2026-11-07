import { Behaviour, Physics, Vector3, field, script } from "mercury";
import { GameInput } from "./generated/game_input";

/**
 * Rolls the ball with the Player/Move action (W/A/S/D, the arrow keys or the left stick) and jumps
 * with Player/Jump; see input/actions.minput.
 */
@script()
export class PlayerController extends Behaviour {
  @field({ tooltip: "Push in newtons at full Move input" }) force = 12;
  @field({ tooltip: "Upward push when Jump is pressed on the ground" }) jumpImpulse = 5;

  #input: GameInput | null = null;
  // Actions update once per frame, and a frame can run zero or several ticks, so a press is
  // remembered until the next tick uses it.
  #jumpRequested = false;

  onStart(): void {
    this.#input = new GameInput(this.inputActions);
  }

  onFrame(): void {
    if (this.#input?.Player.Jump.wasPressedThisFrame) this.#jumpRequested = true;
  }

  onTick(): void {
    if (!this.#input) return;
    // The camera looks down -Z, so "up" on screen (Move's +Y) is -Z.
    const move = this.#input.Player.Move.readVector2();
    const direction = new Vector3(move.x, 0, -move.y);
    if (direction.sqrLength !== 0) {
      Physics.addForce(this.gameObject, direction.scale(this.force));
    }

    // Only jump when the ball is resting on something.
    const grounded = Math.abs(this.transform.velocity.y) < 0.05;
    if (this.#jumpRequested && grounded) {
      Physics.addImpulse(this.gameObject, new Vector3(0, this.jumpImpulse, 0));
    }
    this.#jumpRequested = false;
  }
}
