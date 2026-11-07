import { Behaviour, Debug, Input, Key, NodeRef, Scene, bindable, field, script } from "mercury";
import { Ball } from "./ball";

export type Side = "player" | "enemy";

/** Keeps the score (shown by ui/scoreboard.uxml) and quits on Escape. */
@script()
export class PongGame extends Behaviour {
  @field() ball = new NodeRef("Ball");
  @field({ tooltip: "Show the ball's position and velocity" }) showDebug = true;

  // The scoreboard binds to these.
  @bindable() playerWins = 0;
  @bindable() enemyWins = 0;

  #ball: Ball | null = null;
  #scoredThisTick = false;

  onStart(): void {
    this.#ball = this.get(this.ball)?.getBehaviour(Ball) ?? null;
    if (!this.#ball) Debug.error(`PongGame: "${this.ball.path}" has no Ball script`);
    this.document?.rootVisualElement.setDataSource(this);
  }

  /** Awards a point and serves again. Only one point counts per tick. */
  score(scorer: Side): void {
    if (this.#scoredThisTick) return;
    this.#scoredThisTick = true;
    if (scorer === "player") {
      Debug.log("Player scored");
      this.playerWins += 1;
    } else {
      Debug.log("Enemy scored");
      this.enemyWins += 1;
    }
    this.#ball?.serve();
  }

  onTick(): void {
    // Collisions are handled before onTick, so this clears last tick's point.
    this.#scoredThisTick = false;
    if (Input.isKeyPressed(Key.Escape)) Scene.quit();
  }

  onFrame(): void {
    // A developer window, so it stays out of the game UI; the scoreboard is UI Toolkit.
    if (this.showDebug && this.#ball) {
      const ball = this.#ball;
      Debug.window("Ball", (w) => {
        const pos = ball.transform.position;
        const vel = ball.transform.velocity;
        w.text(`POSITION X: ${pos.x.toFixed(6)}, Y: ${pos.y.toFixed(6)}`);
        w.text(`VELOCITY X: ${vel.x.toFixed(6)}, Y: ${vel.y.toFixed(6)}`);
      }, 10, 70);
    }
  }
}
