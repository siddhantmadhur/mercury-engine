// The first map: a button per scene (ui/menu.uxml). Each button is named after
// its map, maps/<name>.mmap. Esc quits.

[script]
class MainMenu : Behaviour
{
    private string pending = "";
    private int waitFrames = 0;
    private VisualElement root;

    void onStart()
    {
        root = UI::Root();
        if (!root.isValid) return;
        AddButton("sponza");
        AddButton("san_miguel");
        AddButton("sibenik");
        AddButton("conference");
        AddButton("breakfast_room");
        AddButton("living_room");
        AddButton("gallery");
        AddButton("bunny");
        AddButton("mori_knob");
        AddButton("cornell_original");
        AddButton("cornell_sphere");
        AddButton("cornell_water");
        AddButton("cornell_glossy");
        AddButton("cornell_mirror");
        VisualElement quit = root.Q("quit");
        if (quit.isValid) quit.OnClick("Quit");
    }

    private void AddButton(string scene)
    {
        VisualElement button = root.Q(scene);
        if (button.isValid) button.OnClick("Open", scene);
    }

    void Quit() { Scene::Quit(); }

    void Open(string map)
    {
        if (pending != "") return;
        pending = map;
        waitFrames = 2;
        VisualElement loading = root.Q("loading");
        if (loading.isValid) loading.SetText("Loading " + map + "...");
        VisualElement overlay = root.Q("loading-overlay");
        if (overlay.isValid) overlay.SetClass("hidden", false);
    }

    void onFrame(double dt)
    {
        if (pending == "" || --waitFrames > 0) return;
        Scene::LoadMap("res://maps/" + pending + ".mmap");
        pending = "";
    }

    void onTick(double dt)
    {
        if (Input::IsKeyJustPressed(Key::Escape)) Scene::Quit();
    }
}
