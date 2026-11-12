[script]
class SpacePellet : Behaviour
{
    void onCollisionEnter(GameObject other)
    {
        SpaceGame@ game = cast<SpaceGame>(GetBehaviour(Scene::Find("SpaceInvaders"), "SpaceGame"));
        if (other.tag == "Roof") {
            GetGameObject().active = false;
        } else if (other.tag == "Enemy") {
            other.Destroy();
            if (game !is null) {
                game.EnemyKilled();
                game.AddScore(10);
            }
            GetGameObject().active = false;
        }
    }
}
