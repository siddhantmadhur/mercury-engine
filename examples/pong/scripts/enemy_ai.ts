import { Behaviour, Debug, GameObject, Mathf, NodeRef, field, script } from "mercury";
import { Paddle } from "./paddle";

/** Chases the ball with the Paddle on the same node, capped at `speed` so it can lose. */
@script()
export class EnemyAI extends Behaviour {
  @field() ball = new NodeRef("Ball");
  @field({ tooltip: "Keep it below the ball's speed" }) speed = 45;
  @field({ tooltip: "How hard it steers towards the ball" }) gain = 8;

  #paddle: Paddle | null = null;
  #ball: GameObject | null = null;

  onStart(): void {
    this.#paddle = this.gameObject.getBehaviour(Paddle);
    if (!this.#paddle) Debug.error("EnemyAI needs a Paddle on the same node");
    this.#ball = this.get(this.ball);
  }

  onTick(): void {
    if (!this.#paddle || !this.#ball) return;
    const diff = this.#ball.transform.position.y - this.transform.position.y;
    this.#paddle.move(Mathf.clamp(diff * this.gain, -this.speed, this.speed));
  }
}
