// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Collected (and removed) when the player rolls into it.

[script]
class Pickup : Behaviour
{
    [field tooltip="Node with the GameManager script"] [nodeRef] string game = ".";
    [field] int points = 1;

    private bool collected = false;

    void onTriggerEnter(GameObject other)
    {
        if (collected || GetBehaviour(other, "PlayerController") is null) return;
        collected = true;
        GameManager@ manager = cast<GameManager>(GetBehaviour(Scene::Find(game), "GameManager"));
        if (manager !is null) manager.Collect(points);
        GetGameObject().Destroy();
    }
}
