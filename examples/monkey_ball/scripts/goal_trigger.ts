import { Behaviour, GameObject, NodeRef, Scene, field, script } from "mercury";
import { GameManager } from "./game_manager";
import { PlayerBall } from "./player_ball";

@script()
export class GoalTrigger extends Behaviour {
  @field({ tooltip: "Node with GameManager script" }) gameManager = new NodeRef("GameManager");

  #triggered = false;

  onTriggerEnter(other: GameObject): void {
    if (this.#triggered) return;
    if (!other.getBehaviour(PlayerBall)) return;
    this.#triggered = true;
    const manager = this.get(this.gameManager)?.getBehaviour(GameManager);
    if (manager) {
      manager.onGoalReached();
      return;
    }
    // Fallback: search the scene for a GameManager.
    Scene.findObjectsOfType(GameManager)[0]?.onGoalReached();
  }
}
