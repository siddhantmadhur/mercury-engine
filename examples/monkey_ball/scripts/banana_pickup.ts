import { Behaviour, GameObject, NodeRef, Scene, Vector3, field, int, script } from "mercury";
import { GameManager } from "./game_manager";
import { PlayerBall } from "./player_ball";

@script()
export class BananaPickup extends Behaviour {
  @int({ tooltip: "Score awarded upon collection" }) points = 100;
  @field({ tooltip: "Rotation speed in degrees/sec" }) rotateSpeed = 90;
  @field({ tooltip: "Node with GameManager script" }) gameManager = new NodeRef("GameManager");

  #collected = false;

  onTick(dt: number): void {
    this.transform.eulerAngles = this.transform.eulerAngles.add(new Vector3(0, this.rotateSpeed * dt, 0));
  }

  onTriggerEnter(other: GameObject): void {
    if (this.#collected) return;
    if (!other.getBehaviour(PlayerBall)) return;
    this.#collected = true;
    const manager =
      this.get(this.gameManager)?.getBehaviour(GameManager) ?? Scene.findObjectsOfType(GameManager)[0];
    manager?.collectBanana(this.points);
    this.gameObject.destroy();
  }
}
