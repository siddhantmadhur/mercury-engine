// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Fixture for script_behaviour_test.cc: reads a sibling script component
// through the component API, which needs TypeRegistry field accessors.

[script]
class Reader : Behaviour
{
    [field] bool has_probe = false;
    [field] float read_speed = 0.0f;
    [field] int read_lives = 0;
    [field] string read_label = "";

    void onStart()
    {
        GameObject object = GetGameObject();
        has_probe = Components::Has(object, "Probe");
        Component probe = Components::Get(object, "Probe");
        if (!probe.isValid) return;
        read_speed = probe.GetFloat("speed");
        read_lives = probe.GetInt("lives");
        read_label = probe.GetString("label");
    }
}
