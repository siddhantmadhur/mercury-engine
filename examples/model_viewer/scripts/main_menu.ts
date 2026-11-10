import { Behaviour, Button, Input, Key, Label, Scene, VisualElement, script } from "mercury";

// Buttons in ui/menu.uxml; each opens maps/<name>.mmap.
const SCENES = [
  "sponza", "san_miguel", "sibenik", "conference", "breakfast_room",
  "living_room", "gallery", "bunny", "mori_knob", "cornell_original",
  "cornell_sphere", "cornell_water", "cornell_glossy", "cornell_mirror",
];

/**
 * The first map: a button per scene (ui/menu.uxml). Each button is named after its map,
 * maps/<name>.mmap. Esc quits.
 */
@script()
export class MainMenu extends Behaviour {
  #pending = ""; // map to load once "Loading..." has been drawn
  #waitFrames = 0;
  #root: VisualElement | null = null;

  onStart(): void {
    const root = this.document?.rootVisualElement;
    if (!root) return;
    this.#root = root;
    for (const scene of SCENES) {
      const button = root.q(scene, Button);
      if (!button) continue;
      const title = button.text;
      button.onClick(() => this.#open(scene, title));
    }
    root.q("quit", Button)?.onClick(() => Scene.quit());
  }

  #open(map: string, title: string): void {
    if (this.#pending) return;
    this.#pending = map;
    // Big scenes take a few seconds to load: say so before the frame stalls.
    this.#waitFrames = 2;
    const root = this.#root;
    if (!root) return;
    const loading = root.q("loading", Label);
    if (loading) loading.text = `Loading ${title}…`;
    root.q("loading-overlay")?.enableInClassList("hidden", false);
  }

  onFrame(): void {
    if (!this.#pending || --this.#waitFrames > 0) return;
    // Once only: loadMap restarts the background load if it is called again.
    Scene.loadMap(`res://maps/${this.#pending}.mmap`);
    this.#pending = "";
  }

  onTick(): void {
    if (Input.isKeyJustPressed(Key.Escape)) Scene.quit();
  }
}
