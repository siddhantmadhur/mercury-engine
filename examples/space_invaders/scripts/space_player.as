[script]
class SpacePlayer : Behaviour
{
    [field tooltip="World units per second"] float speed = 90.0f;

    void onTick(double dt)
    {
        float direction = 0.0f;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left)) direction -= 1.0f;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) direction += 1.0f;

        GameObject self = GetGameObject();
        self.velocity = Vector3(direction * speed, 0.0f, 0.0f);

        if (Input::IsKeyJustPressed(Key::Space)) {
            SpaceGame@ game = cast<SpaceGame>(GetBehaviour(Scene::Find("SpaceInvaders"), "SpaceGame"));
            if (game !is null) game.SpawnPellet(self.position + Vector3(0.0f, 12.0f, 0.0f));
        }
    }
}
