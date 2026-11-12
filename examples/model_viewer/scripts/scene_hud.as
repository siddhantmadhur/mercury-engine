// The overlay of every scene map (ui/hud.uxml): the scene's name, how to move
// the camera, the frame rate and, with a FlythroughCamera, its lap results.

[script]
class SceneHud : Behaviour
{
    [field] string title = "Scene";
    [field tooltip="Controls shown under the title"] string hint = "";
    [field tooltip="Camera node (its FlythroughCamera reports laps)"] [nodeRef] string camera = "Camera";

    private string fpsText = "-- fps";
    private int frames = 0;
    private float elapsed = 0.0f;

    void onStart()
    {
        VisualElement root = UI::Root();
        if (!root.isValid) return;
        root.SetDataSource();
        VisualElement back = root.Q("back");
        if (back.isValid) back.OnClick("BackToMenu");
    }

    void onFrame(double dt)
    {
        frames++;
        elapsed += dt;
        if (elapsed >= 0.5f) {
            fpsText = "" + int(frames / elapsed) + " fps";
            frames = 0;
            elapsed = 0.0f;
        }
    }

    void onTick(double dt)
    {
        if (Input::IsKeyJustPressed(Key::Escape)) BackToMenu();
    }

    void BackToMenu() { Scene::LoadMap("res://maps/menu.mmap"); }

    [bindable] string get_fpsText() const { return fpsText; }

    [bindable] string get_lapText() const
    {
        FlythroughCamera@ fly = cast<FlythroughCamera>(
            GetBehaviour(Scene::Find(camera), "FlythroughCamera"));
        if (fly is null) return "";
        string text = "Lap " + (fly.Laps() + 1) + "  " + int(fly.LapProgress() * 100.0f) + "%";
        if (fly.LastLapSummary() != "") text += "\nLast lap: " + fly.LastLapSummary();
        return text;
    }
}
