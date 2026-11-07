import { Behaviour, Debug, Input, Key, NodeRef, Scene, Vector3, bindable, field, script } from "mercury";
import { Pickup } from "./pickup";

/** Keeps the score and shows it (ui/score.uxml). Escape quits. */
@script()
export class GameManager extends Behaviour {
  @field({ tooltip: "Node with the PlayerController" }) player = new NodeRef("Player");
  @field({ tooltip: "Below this height the player is put back at the start" }) fallLimit = -10;

  #score = 0;
  #total = 0;
  #startPosition = Vector3.zero;

  // The score label in score.uxml binds to scoreText.
  @bindable()
  get scoreText(): string {
    return `Score: ${this.#score} / ${this.#total}`;
  }

  onStart(): void {
    // Every pickup in the map counts towards the total.
    for (const pickup of Scene.findObjectsOfType(Pickup)) this.#total += pickup.points;
    const object = this.get(this.player);
    if (object) this.#startPosition = object.transform.position;
    this.document?.rootVisualElement.setDataSource(this);
  }

  collect(points: number): void {
    this.#score += points;
    Debug.log(`Picked up ${this.#score} of ${this.#total}`);
  }

  onTick(): void {
    if (Input.isKeyPressed(Key.Escape)) Scene.quit();
    const object = this.get(this.player);
    if (!object) return;
    const transform = object.transform;
    if (transform.position.y < this.fallLimit) {
      transform.position = this.#startPosition;
      transform.velocity = Vector3.zero;
    }
  }

  onFrame(): void {
    const root = this.document?.rootVisualElement;
    root?.q("win")?.enableInClassList("win--hidden", this.#total === 0 || this.#score < this.#total);
  }
}
