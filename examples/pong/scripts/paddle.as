// Chases the ball with the Paddle on the same node, capped at `speed` so it can lose.

[script]
class Paddle : Behaviour
{
    [field tooltip="Furthest the paddle's center may go from y = 0"]
    float limit = 40.0f;

    void Move(float velocityY)
    {
        GameObject self = GetGameObject();
        Vector3 pos = self.position;
        if (pos.y > limit || pos.y < -limit) {
            pos.y = Mathf::Clamp(pos.y, -limit, limit);
            self.position = pos;
        }
        if ((pos.y >= limit && velocityY > 0.0f) || (pos.y <= -limit && velocityY < 0.0f)) {
            velocityY = 0.0f;
        }
        self.velocity = Vector3(0.0f, velocityY, 0.0f);
    }
}
