// Chases the ball with the Paddle on the same node, capped at `speed` so it can lose.

[script]
class EnemyAI : Behaviour
{
    [field] [nodeRef] string ball = "Ball";
    [field tooltip="Keep it below the ball's speed"] float speed = 45.0f;
    [field tooltip="How hard it steers towards the ball"] float gain = 8.0f;

    private Paddle@ paddle;
    private GameObject ballObject;

    void onStart()
    {
        @paddle = cast<Paddle>(GetBehaviour(GetGameObject(), "Paddle"));
        if (paddle is null) Debug::Error("EnemyAI needs a Paddle on the same node");
        ballObject = Scene::Find(ball);
    }

    void onTick(double dt)
    {
        if (paddle is null || !ballObject.isValid) return;
        float diff = ballObject.position.y - GetGameObject().position.y;
        paddle.Move(Mathf::Clamp(diff * gain, -speed, speed));
    }
}
