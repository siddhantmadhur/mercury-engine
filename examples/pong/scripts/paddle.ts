import { Behaviour, Mathf, Vector3, field, script } from "mercury";

/**
 * Drives a kinematic paddle vertically, stopping it at the edges.
 *
 * Kinematic bodies do not collide with the static walls, so the limit is enforced here.
 * PlayerController and EnemyAI call move.
 */
@script()
export class Paddle extends Behaviour {
  @field({ tooltip: "Furthest the paddle's center may go from y = 0" }) limit = 40;

  move(velocityY: number): void {
    const transform = this.transform;
    let pos = transform.position;
    if (pos.y > this.limit || pos.y < -this.limit) {
      pos = pos.with({ y: Mathf.clamp(pos.y, -this.limit, this.limit) });
      transform.position = pos;
    }
    if ((pos.y >= this.limit && velocityY > 0) || (pos.y <= -this.limit && velocityY < 0)) {
      velocityY = 0;
    }
    transform.velocity = new Vector3(0, velocityY, 0);
  }
}
