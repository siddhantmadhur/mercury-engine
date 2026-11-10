import { Behaviour, Button, Input, Key, NodeRef, Scene, bindable, field, script } from "mercury";
import { FlythroughCamera } from "./flythrough_camera";

/**
 * The overlay of every scene map (ui/hud.uxml): the scene's name, how to move the camera, the frame
 * rate and, with a FlythroughCamera, its lap results. Esc or the Back button returns to the menu.
 */
@script()
export class SceneHud extends Behaviour {
  @field() title = "Scene";
  @field({ tooltip: "Controls shown under the title" }) hint = "";
  @field({ tooltip: "Camera node (its FlythroughCamera reports laps)" }) camera = new NodeRef("Camera");

  // Values hud.uxml binds to (title and hint are fields above).
  @bindable() fpsText = "-- fps";
  @bindable()
  get lapText(): string {
    const flythrough = this.get(this.camera)?.getBehaviour(FlythroughCamera);
    if (!flythrough) return "";
    let text = `Lap ${flythrough.laps + 1}  ${(flythrough.lapProgress * 100).toFixed(0)}%`;
    if (flythrough.lastLapSummary) text += `\nLast lap: ${flythrough.lastLapSummary}`;
    return text;
  }

  // Frame rate over the last half second.
  #frames = 0;
  #elapsed = 0;

  onStart(): void {
    const root = this.document?.rootVisualElement;
    if (!root) return;
    root.setDataSource(this);
    root.q("back", Button)?.onClick(() => this.#backToMenu());
  }

  onFrame(dt: number): void {
    this.#frames++;
    this.#elapsed += dt;
    if (this.#elapsed >= 0.5) {
      this.fpsText = `${(this.#frames / this.#elapsed).toFixed(0)} fps  (${((1000 * this.#elapsed) / this.#frames).toFixed(2)} ms)`;
      this.#frames = 0;
      this.#elapsed = 0;
    }
  }

  onTick(): void {
    if (Input.isKeyJustPressed(Key.Escape)) this.#backToMenu();
  }

  #backToMenu(): void {
    Scene.loadMap("res://maps/menu.mmap");
  }
}
