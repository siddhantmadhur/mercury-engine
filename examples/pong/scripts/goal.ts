import { Behaviour, GameObject, NodeRef, enumField, field, script } from "mercury";
import { Ball } from "./ball";
import { PongGame, type Side } from "./pong_game";

/** A wall that scores a point for `scorer` when the ball reaches it. */
@script()
export class Goal extends Behaviour {
  @enumField(["player", "enemy"], { tooltip: "Who gets the point when the ball hits this wall" })
  scorer: Side = "player";
  @field({ tooltip: "Node with the PongGame script" }) game = new NodeRef(".");

  onCollisionEnter(other: GameObject): void {
    if (!other.getBehaviour(Ball)) return;
    this.get(this.game)?.getBehaviour(PongGame)?.score(this.scorer);
  }
}
