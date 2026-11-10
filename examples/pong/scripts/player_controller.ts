import { Behaviour, Debug, Input, Key, field, script } from "mercury";
import { Paddle } from "./paddle";

/** Moves the Paddle on the same node with the keyboard. */
@script()
export class PlayerController extends Behaviour {
  @field({ tooltip: "World units per second" }) speed = 70;
  @field({ tooltip: "Key name, e.g. W or Up" }) upKey = "W";
  @field() downKey = "S";

  #paddle: Paddle | null = null;
  #up = Key.Unknown;
  #down = Key.Unknown;

  onStart(): void {
    this.#paddle = this.gameObject.getBehaviour(Paddle);
    if (!this.#paddle) Debug.error("PlayerController needs a Paddle on the same node");
    this.#up = Key[this.upKey as keyof typeof Key] ?? Key.Unknown;
    this.#down = Key[this.downKey as keyof typeof Key] ?? Key.Unknown;
  }

  onTick(): void {
    if (!this.#paddle) return;
    let direction = 0;
    if (Input.isKeyPressed(this.#up)) direction += 1;
    if (Input.isKeyPressed(this.#down)) direction -= 1;
    this.#paddle.move(direction * this.speed);
  }
}
