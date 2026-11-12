// Flies the camera through the children of `path` on a smooth Catmull-Rom path.

Vector3 CatmullRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    Vector3 a = p1.Scale(2.0f);
    Vector3 b = (p2 - p0).Scale(t);
    Vector3 c = (p0.Scale(2.0f) - p1.Scale(5.0f) + p2.Scale(4.0f) - p3).Scale(t2);
    Vector3 d = (p1.Scale(3.0f) - p0 - p2.Scale(3.0f) + p3).Scale(t3);
    return (a + b + c + d).Scale(0.5f);
}

[script]
class FlythroughCamera : Behaviour
{
    [field tooltip="Node whose children are the waypoints, in order"] [nodeRef] string path = "Path";
    [field tooltip="Time for one lap of the path"] float lapSeconds = 60.0f;
    [field tooltip="Return to the first waypoint at the end"] bool loop = true;

    private array<Vector3>@ positions;
    private array<Quaternion>@ rotations;
    private array<float>@ frameTimes;
    private float time = 0.0f;
    private bool paused = false;
    private float warmup = 1.0f;
    private string lastLap;
    private int laps = 0;

    string LastLapSummary() const { return lastLap; }
    int Laps() const { return laps; }

    float LapProgress() const
    {
        return lapSeconds > 0.0f ? Mathf::Repeat(time, lapSeconds) / lapSeconds : 0.0f;
    }

    void onStart()
    {
        @positions = array<Vector3>();
        @rotations = array<Quaternion>();
        @frameTimes = array<float>();
        GameObject root = Scene::Find(path);
        if (root.isValid) {
            array<GameObject>@ children = root.children;
            for (uint i = 0; i < children.length(); i++) {
                positions.insertLast(children[i].position);
                rotations.insertLast(children[i].rotation);
            }
        }
        if (positions.length() == 0) {
            Debug::Warn("FlythroughCamera: " + path + " has no waypoints");
            return;
        }
        Place(0.0f);
    }

    void onFrame(double dt)
    {
        if (paused || positions.length() < 2) return;
        if (warmup > 0.0f) {
            warmup -= dt;
            return;
        }
        frameTimes.insertLast(dt);
    }

    void onTick(double dt)
    {
        if (Input::IsKeyJustPressed(Key::Space)) {
            paused = !paused;
            frameTimes.resize(0);
        }
        if (paused || positions.length() < 2 || warmup > 0.0f) return;
        float before = time;
        time += dt;
        if (Mathf::Floor(time / lapSeconds) > Mathf::Floor(before / lapSeconds)) {
            FinishLap();
            if (!loop) {
                time = lapSeconds;
                paused = true;
            }
        }
        Place(Mathf::Repeat(time, lapSeconds) / lapSeconds);
    }

    private void Place(float t)
    {
        int count = positions.length();
        int segments = loop ? count : count - 1;
        float f = Mathf::Clamp01(t) * segments;
        int index = int(f);
        if (index > segments - 1) index = segments - 1;
        float local = f - index;
        Vector3 position = CatmullRom(At(index - 1), At(index), At(index + 1), At(index + 2),
                                      local);
        float ease = local * local * (3.0f - 2.0f * local);
        GameObject self = GetGameObject();
        self.position = position;
        Quaternion from = rotations[Index(index)];
        Quaternion to = rotations[Index(index + 1)];
        self.rotation = from.Slerp(to, ease);
    }

    private int Index(int index)
    {
        int count = positions.length();
        if (loop) return ((index % count) + count) % count;
        return int(Mathf::Clamp(float(index), 0.0f, float(count - 1)));
    }

    private Vector3 At(int index) { return positions[Index(index)]; }

    private void FinishLap()
    {
        laps++;
        if (frameTimes.length() == 0) return;
        array<float>@ times = array<float>();
        for (uint i = 0; i < frameTimes.length(); i++) times.insertLast(frameTimes[i]);
        frameTimes.resize(0);
        float total = 0.0f;
        for (uint i = 0; i < times.length(); i++) total += times[i];
        times.sortAsc();
        int worst = int(times.length() / 100);
        if (worst < 1) worst = 1;
        float worstTotal = 0.0f;
        for (uint i = times.length() - worst; i < times.length(); i++) worstTotal += times[i];
        float avgFps = times.length() / total;
        float lowFps = worst / worstTotal;
        float minFps = 1.0f / times[times.length() - 1];
        lastLap = "avg " + avgFps + " fps, 1% low " + lowFps + ", min " + minFps;
        Debug::Log("Lap " + laps + ": " + times.length() + " frames, " + lastLap);
    }
}
