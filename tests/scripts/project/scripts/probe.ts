import {
  Animator, Assets, AudioSource, Behaviour, CharacterController, Debug, Event, GameObject, NodeRef, Quaternion, Scene, Vector3, bindable, callable,
  dataAsset, enumField, field, int, script,
} from "mercury";

// What happened, in order; tests read it back with `globalThis.__log`.
const log: string[] = [];
(globalThis as any).__log = log;
// The math classes, for tests that compare them with the engine's own (glm) results.
(globalThis as any).__math = { Quaternion, Vector3 };

@script()
class Probe extends Behaviour {
  @field({ tooltip: "A float" }) speed = 2.5;
  @int() lives = 3;
  @field() label = "hi";
  @field() flag = true;
  @field() offset = new Vector3(1, 2, 3);
  @enumField(["a", "b"]) mode = "a";
  @field() target = new NodeRef();
  @field() fired = new Event();
  @bindable() get doubled(): number { return this.speed * 2; }

  total = 0;
  bumps = 0;

  awake(): void { log.push(`${this.gameObject.name}.awake`); }
  onEnable(): void { log.push(`${this.gameObject.name}.onEnable`); }
  onDisable(): void { log.push(`${this.gameObject.name}.onDisable`); }
  onStart(): void { log.push(`${this.gameObject.name}.onStart`); }
  onTick(): void { log.push(`${this.gameObject.name}.onTick`); }
  onDestroy(): void { log.push(`${this.gameObject.name}.onDestroy`); }
  onCollisionEnter(other: GameObject): void { log.push(`${this.gameObject.name}.collision.${other.name}`); }

  @callable("float") add(amount: number): void { this.total += amount; log.push(`add ${amount}`); }
  @callable() bump(): void { this.bumps++; log.push("bump"); }
}

// Throws on its first tick; the game must carry on.
@script()
class Thrower extends Behaviour {
  ticks = 0;
  onTick(): void {
    this.ticks++;
    log.push(`Thrower.tick ${this.ticks}`);
    if (this.ticks === 1) throw new Error("boom");
  }
}

// Removes the object named "Victim" on its first tick.
@script()
class Killer extends Behaviour {
  done = false;
  onTick(): void {
    if (this.done) return;
    this.done = true;
    Scene.find("Victim")?.destroy();
  }
}

// Keeps a handle to "Victim" and uses it after Victim is gone.
@script()
class Stale extends Behaviour {
  victim: GameObject | null = null;
  onStart(): void { this.victim = Scene.find("Victim"); }
  onTick(): void {
    try {
      this.victim?.transform.position;
      log.push("Stale.ok");
    } catch (e) {
      log.push(`Stale.caught ${(e as Error).message}`);
    }
  }
}

// Defined before the constant it uses: the field default is read after the whole bundle ran.
@script()
class Late extends Behaviour {
  @field() value = LATE_DEFAULT;
}
const LATE_DEFAULT = 42;

@script()
class Base extends Behaviour {
  @field() baseField = 1;
}

@script()
class Derived extends Base {
  @field() derivedField = 2;
}

// A ScriptableObject-style asset type.
@dataAsset()
class LevelData {
  @field() par = 3;
  @field() title = "First";
}

@script()
class AssetUser extends Behaviour {
  onStart(): void {
    const level = Assets.load(LevelData, "res://assets/level.masset");
    log.push(`level ${level.par} ${level.title}`);
  }
}

// Uses the runtime methods of engine components.
@script()
class ComponentUser extends Behaviour {
  onTick(): void {
    const controller = this.gameObject.getComponent(CharacterController);
    if (!controller) { log.push("no controller"); return; }
    controller.move(new Vector3(0, -0.05, -0.1));
    log.push(`grounded ${controller.isGrounded} v ${controller.velocity.length >= 0}`);
    const audio = this.gameObject.getComponent(AudioSource);
    if (!audio) { log.push("no audio"); return; }
    audio.setVolume(0.25);
    audio.setPitch(1.5);
    audio.setLoop(true);
    audio.setMute(true);
    audio.setClip("res://none.wav");
    audio.play();
    audio.pause();
    audio.unPause();
    audio.stop();
    audio.playOneShot("res://none.wav", 0.5);
    log.push(`audio v${audio.volume} p${audio.pitch} l${audio.loop} m${audio.mute} playing ${audio.isPlaying}`);
    const animator = this.gameObject.getComponent(Animator);
    if (!animator) { log.push("no animator"); return; }
    animator.setFloat("speed", 2);
    animator.setInteger("count", 3);
    animator.setBool("armed", true);
    animator.setTrigger("jump");
    animator.resetTrigger("jump");
    animator.play("Idle");
    log.push(`anim ${typeof animator.getFloat("speed")} ${typeof animator.getInteger("count")} ${typeof animator.getBool("armed")} ${typeof animator.getCurrentState()}`);
  }
}
