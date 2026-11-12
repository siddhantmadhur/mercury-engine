// Serves the ball and keeps it moving at a constant speed. Moving paddles add
// energy on contact, and a near-vertical ball would never reach either side, so
// onTick corrects both.

[script]
class Ball : Behaviour
{
    [field tooltip="World units per second"]
    float speed = 60.0f;

    [field tooltip="Smallest share of the direction that is horizontal"]
    float minHorizontal = 0.45f;

    [field tooltip="Serves go within this many degrees of horizontal"]
    float serveAngle = 35.0f;

    private bool servedThisTick = false;

    void onStart()
    {
        Serve();
    }

    [callable]
    void Serve()
    {
        if (servedThisTick) return;
        servedThisTick = true;
        Debug::Log("Starting new round...");

        float degree = serveAngle > 0.0f ? Random::Range(-serveAngle, serveAngle) : 0.0f;
        float side = Random::Value() < 0.5f ? 1.0f : -1.0f;
        float radians = degree * Mathf::Deg2Rad();
        Vector3 direction(side * Mathf::Cos(radians), Mathf::Sin(radians), 0.0f);

        GameObject self = GetGameObject();
        self.position = Vector3(0.0f, 0.0f, 0.0f);
        self.velocity = direction.Scale(speed);
    }

    void onTick(double dt)
    {
        if (servedThisTick) {
            servedThisTick = false;
            return;
        }

        GameObject self = GetGameObject();
        Vector3 velocity = self.velocity;
        float current = velocity.Length();
        if (current <= 0.0f) return;

        Vector3 dir = velocity.Scale(1.0f / current);
        bool correct = Mathf::Abs(current - speed) > 1.0f;
        if (Mathf::Abs(dir.x) < minHorizontal) {
            float x = Mathf::Sign(dir.x == 0.0f ? 1.0f : dir.x) * minHorizontal;
            float y = Mathf::Sign(dir.y) * Mathf::Sqrt(1.0f - minHorizontal * minHorizontal);
            dir = Vector3(x, y, dir.z);
            correct = true;
        }
        if (correct) self.velocity = dir.Scale(speed);
    }
}
