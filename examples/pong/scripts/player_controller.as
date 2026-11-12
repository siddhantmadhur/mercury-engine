// Moves the Paddle on the same node with the keyboard.

[script]
class PlayerController : Behaviour
{
    [field tooltip="World units per second"] float speed = 70.0f;
    [field tooltip="Key name, e.g. W or Up"] string upKey = "W";
    [field] string downKey = "S";

    private Paddle@ paddle;
    private int up = -1;
    private int down = -1;

    void onStart()
    {
        @paddle = cast<Paddle>(GetBehaviour(GetGameObject(), "Paddle"));
        if (paddle is null) Debug::Error("PlayerController needs a Paddle on the same node");
        up = Input::KeyFromName(upKey);
        down = Input::KeyFromName(downKey);
    }

    void onTick(double dt)
    {
        if (paddle is null) return;
        float direction = 0.0f;
        if (Input::IsKeyPressed(up)) direction += 1.0f;
        if (Input::IsKeyPressed(down)) direction -= 1.0f;
        paddle.Move(direction * speed);
    }
}
