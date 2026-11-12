[script]
class BreakoutBall : Behaviour
{
    [field tooltip="World units per second"] float speed = 160.0f;
    [field tooltip="Smallest share of the direction that is vertical"] float minVertical = 0.35f;
    [field tooltip="Y position the ball is served from"] float startY = -136.0f;

    private bool launchedThisTick = false;

    void onStart()
    {
        Launch();
    }

    [callable]
    void Launch()
    {
        GameObject self = GetGameObject();
        float side = Random::Value() < 0.5f ? -1.0f : 1.0f;
        float degree = 90.0f + side * Random::Range(20.0f, 40.0f);
        float radians = degree * Mathf::Deg2Rad();
        Vector3 direction(Mathf::Cos(radians), Mathf::Sin(radians), 0.0f);
        self.position = Vector3(0.0f, startY, 0.0f);
        self.velocity = direction.Scale(speed);
        launchedThisTick = true;
    }

    void onTick(double dt)
    {
        // A velocity set by Launch overrides this tick's bounce; leave it alone.
        if (launchedThisTick) {
            launchedThisTick = false;
            return;
        }

        GameObject self = GetGameObject();
        Vector3 velocity = self.velocity;
        float current = velocity.Length();
        if (current <= 0.0f) return;

        Vector3 dir = velocity.Scale(1.0f / current);
        bool correct = Mathf::Abs(current - speed) > 1.0f;
        if (Mathf::Abs(dir.y) < minVertical) {
            float sign = dir.y == 0.0f ? 1.0f : Mathf::Sign(dir.y);
            dir.y = sign * minVertical;
            dir.x = Mathf::Sign(dir.x == 0.0f ? 1.0f : dir.x) *
                    Mathf::Sqrt(1.0f - minVertical * minVertical);
            correct = true;
        }
        if (correct) self.velocity = dir.Scale(speed);
    }

    void onCollisionEnter(GameObject other)
    {
        BreakoutGame@ game = cast<BreakoutGame>(GetBehaviour(Scene::Find("Breakout"), "BreakoutGame"));
        if (other.tag == "Floor") {
            if (game !is null) game.BallLost();
            Launch();
        } else if (other.tag == "Brick") {
            other.Destroy();
            if (game !is null) game.BrickDestroyed();
        }
    }
}
