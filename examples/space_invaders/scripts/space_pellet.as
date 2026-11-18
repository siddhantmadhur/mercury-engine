// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
