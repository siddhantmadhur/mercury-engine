import { Behaviour, GameObject, NodeRef, field, script } from "mercury";
import { GameManager } from "./game_manager";
import { PlayerController } from "./player_controller";

/** Collected (and removed) when the player rolls into it. */
@script()
export class Pickup extends Behaviour {
  @field({ tooltip: "Node with the GameManager script" }) game = new NodeRef(".");
  @field() points = 1;

  #collected = false;

  onTriggerEnter(other: GameObject): void {
    if (this.#collected || !other.getBehaviour(PlayerController)) return;
    this.#collected = true;
    this.get(this.game)?.getBehaviour(GameManager)?.collect(this.points);
    this.gameObject.destroy();
  }
}
