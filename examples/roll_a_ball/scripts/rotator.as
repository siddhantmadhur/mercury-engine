// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Spins the node at a constant rate.

[script]
class Rotator : Behaviour
{
    [field] Vector3 degreesPerSecond = Vector3(15.0f, 30.0f, 45.0f);

    void onTick(double dt)
    {
        GameObject self = GetGameObject();
        Vector3 next = self.eulerAngles + degreesPerSecond.Scale(dt);
        self.eulerAngles = Vector3(Mathf::Repeat(next.x, 360.0f),
                                   Mathf::Repeat(next.y, 360.0f),
                                   Mathf::Repeat(next.z, 360.0f));
    }
}
