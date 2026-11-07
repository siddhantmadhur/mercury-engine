import { Behaviour, Input, Key, MouseButton, NodeRef, Quaternion, Vector2, Vector3, field, script } from "mercury";

/**
 * Turns the camera around a point with the mouse, like a model viewer: left-drag orbits,
 * right/middle-drag pans, the wheel zooms, R resets. With `distance` 0 the camera frames
 * `target`'s mesh by itself.
 */
@script()
export class OrbitCamera extends Behaviour {
  @field({ tooltip: "Node whose mesh to frame (when distance is 0)" }) target = new NodeRef();
  @field({ tooltip: "Point to orbit around" }) focus = Vector3.zero;
  @field({ tooltip: "Distance from the focus; 0 frames the target" }) distance = 0;
  @field({ tooltip: "Starting angle around the vertical, degrees" }) yaw = 0;
  @field({ tooltip: "Starting angle above the horizon, degrees" }) pitch = 15;
  @field({ tooltip: "Degrees per second while idle" }) autoRotate = 6;

  #startFocus = Vector3.zero;
  #startDistance = 1;
  #startYaw = 0;
  #startPitch = 0;
  #idle = 0; // seconds since the last mouse input
  #scroll = 0; // wheel turned since the last tick

  onStart(): void {
    if (this.distance <= 0) {
      // Frame the target's mesh: far enough for its bounding sphere to fit.
      this.distance = 5;
      const object = this.get(this.target);
      const bounds = object?.bounds;
      if (object && bounds) {
        const scale = object.transform.localScale;
        const lo = new Vector3(bounds.min.x * scale.x, bounds.min.y * scale.y, bounds.min.z * scale.z);
        const hi = new Vector3(bounds.max.x * scale.x, bounds.max.y * scale.y, bounds.max.z * scale.z);
        this.focus = object.transform.position.add(lo.add(hi).scale(0.5));
        this.distance = Math.max(hi.sub(lo).length * 1.1, 0.1);
      }
    }
    this.#startFocus = this.focus;
    this.#startDistance = this.distance;
    this.#startYaw = this.yaw;
    this.#startPitch = this.pitch;
    this.#apply();
  }

  onFrame(): void {
    // The wheel is reported per frame; ticks may run less often.
    if (!Input.isPointerOverUI) this.#scroll += Input.scroll.y;
  }

  onTick(dt: number): void {
    const delta = Input.cursorDelta;
    const scroll = this.#scroll;
    this.#scroll = 0;
    const overUi = Input.isPointerOverUI;
    let moved = false;
    const moving = !delta.equals(Vector2.zero);

    if (!overUi && Input.isMouseButtonPressed(MouseButton.Left)) {
      this.yaw -= delta.x * 0.3;
      this.pitch = Math.min(89, Math.max(-89, this.pitch + delta.y * 0.3));
      moved = moving;
    }
    if (!overUi && (Input.isMouseButtonPressed(MouseButton.Right) || Input.isMouseButtonPressed(MouseButton.Middle))) {
      // Pan in the view plane, at a speed that keeps the focus under the mouse.
      const rotation = Quaternion.euler(-this.pitch, this.yaw, 0);
      const speed = this.distance * 0.0015;
      this.focus = this.focus.add(rotation.rotate(new Vector3(-delta.x * speed, delta.y * speed, 0)));
      moved = moving;
    }
    if (scroll !== 0) {
      this.distance = Math.max(this.distance * Math.pow(0.9, scroll), 0.01);
      moved = true;
    }
    if (Input.isKeyJustPressed(Key.R)) {
      this.focus = this.#startFocus;
      this.distance = this.#startDistance;
      this.yaw = this.#startYaw;
      this.pitch = this.#startPitch;
      moved = true;
    }

    this.#idle = moved ? 0 : this.#idle + dt;
    // Turn slowly on its own after a few seconds without input.
    if (this.#idle > 3) this.yaw += this.autoRotate * dt;
    this.#apply();
  }

  #apply(): void {
    // Looking down at the focus from `pitch` degrees above the horizon.
    const rotation = Quaternion.euler(-this.pitch, this.yaw, 0);
    this.transform.position = this.focus.add(rotation.rotate(new Vector3(0, 0, this.distance)));
    this.transform.eulerAngles = new Vector3(-this.pitch, this.yaw, 0);
  }
}
