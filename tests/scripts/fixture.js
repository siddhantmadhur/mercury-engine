(() => {
  var __defProp = Object.defineProperty;
  var __name = (target, value) => __defProp(target, "name", { value, configurable: true });

  // scripts/ball.ts
  var Ball = class {
    static {
      __name(this, "Ball");
    }
    speed = 3;
    onTick(dt) {
      const step = this.speed * dt;
      if (step > 100) {
        throw new Error("too fast");
      }
    }
  };
})();
