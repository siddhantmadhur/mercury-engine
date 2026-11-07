export class Ball {
  speed = 3;
  onTick(dt: number): void {
    const step = this.speed * dt;
    if (step > 100) {
      throw new Error("too fast");
    }
  }
}
