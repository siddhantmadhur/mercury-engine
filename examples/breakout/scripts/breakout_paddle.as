[script]
class BreakoutPaddle : Behaviour
{
    [field tooltip="World units per second"] float speed = 260.0f;
    [field tooltip="Furthest the paddle's center may go from x = 0"] float limit = 300.0f;

    void onTick(double dt)
    {
        float direction = 0.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left)) direction -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) direction += 1.0f;

        GameObject self = GetGameObject();
        Vector3 pos = self.position;
        if (Mathf::Abs(pos.x) > limit) {
            pos.x = Mathf::Clamp(pos.x, -limit, limit);
            self.position = pos;
        }
        if ((pos.x >= limit && direction > 0.0f) || (pos.x <= -limit && direction < 0.0f)) {
            direction = 0.0f;
        }
        self.velocity = Vector3(direction * speed, 0.0f, 0.0f);
    }
}
