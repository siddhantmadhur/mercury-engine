import {
  Assets, Behaviour, Debug, Event, GameObject, NodeRef, Scene, Vector3, bindable, callable,
  dataAsset, enumField, field, int, script,
} from "mercury";

// What happened, in order; tests read it back with `globalThis.__log`.
const log: string[] = [];
(globalThis as any).__log = log;

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
