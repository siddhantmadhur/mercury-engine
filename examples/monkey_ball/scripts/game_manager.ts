import {
  Behaviour, Button, Debug, Input, Key, Label, NodeRef, Scene, Toggle, VisualElement, Vector3,
  bindable, field, script,
} from "mercury";
import { BananaPickup } from "./banana_pickup";
import { PlayerBall } from "./player_ball";

type StageState = "playing" | "goal" | "fallOut";

@script()
export class GameManager extends Behaviour {
  @field({ tooltip: "Player ball node" }) player = new NodeRef("Player");
  @field({ tooltip: "Y height below which player falls out" }) fallLimit = -12;
  @field({ tooltip: "Stage countdown time limit in seconds" }) timeLimit = 60;

  // Values the HUD (ui/hud.uxml) binds to.
  @bindable() score = 0;
  @bindable() falls = 0;
  @bindable() timeRemaining = 60;
  @bindable() get timeText(): string { return `${this.timeRemaining.toFixed(2)} s`; }
  @bindable() get bananasText(): string { return `${this.#bananasCollected} / ${this.#totalBananas}`; }

  #state: StageState = "playing";
  #bananasCollected = 0;
  #totalBananas = 0;
  #elapsed = 0;
  #messageTimer = 0;
  #startPosition = new Vector3(0, 1.2, 0);
  #paused = false;
  #hud: VisualElement | null = null;

  get isPaused(): boolean { return this.#paused; }

  #setupHud(): void {
    this.#hud = this.document?.rootVisualElement ?? null;
    const hud = this.#hud;
    if (!hud) return;
    // Labels in hud.uxml bind to the @bindable members above.
    hud.setDataSource(this);

    hud.q("resume", Button)?.onClick(() => this.#setPaused(false));
    hud.q("restart", Button)?.onClick(() => {
      this.#setPaused(false);
      this.restartStage();
    });
    hud.q("play-again", Button)?.onClick(() => this.restartStage());
    hud.q("quit", Button)?.onClick(() => Scene.quit());
    hud.q("goal-quit", Button)?.onClick(() => Scene.quit());
    hud.q("show-hints", Toggle)?.onValueChanged((on) => {
      hud.qClass("hint", Label)?.enableInClassList("hint--hidden", !on);
    });
  }

  #setPaused(paused: boolean): void {
    this.#paused = paused;
    const hud = this.#hud;
    if (!hud) return;
    hud.q("pause")?.enableInClassList("hidden", !paused);
    if (paused) hud.q("resume", Button)?.focus();
    else hud.blurFocused(); // so Enter and the arrow keys go back to the game
  }

  onStart(): void {
    this.score = 0;
    this.#bananasCollected = 0;
    this.#totalBananas = Scene.findObjectsOfType(BananaPickup).length;
    this.falls = 0;
    this.#elapsed = 0;
    this.timeRemaining = this.timeLimit;
    this.#state = "playing";

    const p = this.get(this.player);
    if (p) this.#startPosition = p.transform.position;
    this.#setupHud();
  }

  collectBanana(points: number): void {
    this.#bananasCollected++;
    this.score += points;
    this.timeRemaining = Math.min(this.timeLimit + 30, this.timeRemaining + 3);
  }

  onGoalReached(): void {
    if (this.#state === "playing") {
      this.#state = "goal";
      this.score += Math.trunc(this.timeRemaining * 50);
    }
  }

  restartStage(): void {
    this.#state = "playing";
    this.timeRemaining = this.timeLimit;
    this.#elapsed = 0;
    this.score = 0;
    this.#bananasCollected = 0;
    this.#putPlayerBack();
  }

  #putPlayerBack(): void {
    const p = this.get(this.player);
    if (!p) return;
    p.transform.position = this.#startPosition;
    p.getBehaviour(PlayerBall)?.resetPhysics();
  }

  onTick(dt: number): void {
    if (Input.isKeyJustPressed(Key.Escape)) this.#setPaused(!this.#paused);
    if (this.#paused) return;
    if (Input.isKeyJustPressed(Key.R)) this.restartStage();

    if (this.#state === "playing") {
      this.#elapsed += dt;
      this.timeRemaining = Math.max(0, this.timeRemaining - dt);

      const p = this.get(this.player);
      if (p && p.transform.position.y < this.fallLimit) {
        this.falls++;
        this.#messageTimer = 1.5;
        this.#state = "fallOut";
        this.#putPlayerBack();
      }
    } else if (this.#state === "fallOut") {
      this.#messageTimer -= dt;
      if (this.#messageTimer <= 0) this.#state = "playing";
    }
  }

  onFrame(): void {
    const hud = this.#hud;
    if (!hud) return;
    // Values update through bindings; only classes are toggled here, and hud.uss animates the changes.
    hud.q("time", Label)?.enableInClassList("stat__value--low", this.timeRemaining < 10);
    hud.q("fall-out")?.enableInClassList("banner--hidden", this.#state !== "fallOut");
    const goal = hud.q("goal");
    const cleared = this.#state === "goal";
    if (goal && cleared && goal.hasClass("banner--hidden")) {
      const summary = hud.q("goal-summary", Label);
      if (summary) {
        summary.text =
          `Time <b>${this.#elapsed.toFixed(2)}s</b>   ` +
          `Bananas <color=#FFE433><b>${this.#bananasCollected} / ${this.#totalBananas}</b></color>\n` +
          `Score <b>${this.score}</b>   Falls <b>${this.falls}</b>`;
      }
    }
    goal?.enableInClassList("banner--hidden", !cleared);
  }
}
