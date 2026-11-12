// A wall that scores a point for `scorer` when the ball reaches it.

[script]
class Goal : Behaviour
{
    [field] [enum "player", "enemy"] [tooltip="Who gets the point when the ball hits this wall"]
    int scorer = 0;

    [field tooltip="Node with the PongGame script"] [nodeRef] string game = ".";

    void onCollisionEnter(GameObject other)
    {
        if (GetBehaviour(other, "Ball") is null) return;
        IBehaviour@ target = GetBehaviour(Scene::Find(game), "PongGame");
        PongGame@ pong = cast<PongGame>(target);
        if (pong !is null) pong.Score(scorer);
    }
}
