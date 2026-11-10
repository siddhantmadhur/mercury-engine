import { Behaviour, GameObject, NodeRef, Vector3, field, script } from "mercury";

/** Keeps the camera at the offset it starts with from the target. */
@script()
export class CameraFollow extends Behaviour {
  @field({ tooltip: "Node to follow" }) target = new NodeRef();

  #target: GameObject | null = null;
  #offset = Vector3.zero;

  onStart(): void {
    this.#target = this.get(this.target);
    if (this.#target) {
      this.#offset = this.transform.position.sub(this.#target.transform.position);
    }
  }

  onTick(): void {
    if (!this.#target) return;
    this.transform.position = this.#target.transform.position.add(this.#offset);
  }
}
