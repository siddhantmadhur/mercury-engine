// Fixtures for tests/script_behaviour_test.cc. Each callback appends a letter to
// the object's tag so the test can see the order without a log sink.

[script]
class Probe : Behaviour
{
    [field tooltip="A float"] float speed = 2.5f;
    [field] int lives = 3;
    [field] string label = "hi";
    [field] bool flag = true;
    [field] Vector3 offset = Vector3(1, 2, 3);
    [field] [enum "a", "b"] int mode = 0;
    [field] [nodeRef] string target = "";
    [field] Event fired;

    float total = 0;

    private void Append(string text)
    {
        GameObject object = GetGameObject();
        object.tag = object.tag + text;
    }

    void awake() { Append("a"); }
    void onEnable() { Append("e"); }
    void onDisable() { Append("d"); }
    void onStart() { Append("s"); }
    void onTick(double dt) { Append("t"); fired.Invoke(); }
    void onDestroy() { Append("x"); }

    [callable]
    void Add(float amount)
    {
        total += amount;
        Append("+");
    }
}

[script]
class Thrower : Behaviour
{
    int ticks = 0;

    void onTick(double dt)
    {
        ticks++;
        GameObject object = GetGameObject();
        object.tag = object.tag + "t";
        if (ticks == 1) {
            int[] values;
            values[3] = 1;
        }
    }
}
