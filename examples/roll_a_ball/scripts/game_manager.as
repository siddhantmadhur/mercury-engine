// Keeps the score and shows it (ui/score.uxml). Escape quits.

[script]
class GameManager : Behaviour
{
    [field tooltip="Node with the PlayerController"] [nodeRef] string player = "Player";
    [field tooltip="Below this height the player is put back at the start"] float fallLimit = -10.0f;

    private int score = 0;
    private int total = 0;
    private Vector3 startPosition;

    void onStart()
    {
        array<GameObject>@ pickups = Scene::FindObjectsWithScript("Pickup");
        for (uint i = 0; i < pickups.length(); i++) {
            Pickup@ pickup = cast<Pickup>(GetBehaviour(pickups[i], "Pickup"));
            if (pickup !is null) total += pickup.points;
        }
        GameObject object = Scene::Find(player);
        if (object.isValid) startPosition = object.position;
        VisualElement root = UI::Root();
        if (root.isValid) root.SetDataSource();
    }

    [callable]
    void Collect(int points)
    {
        score += points;
        Debug::Log("Picked up " + score + " of " + total);
    }

    void onTick(double dt)
    {
        if (Input::IsKeyPressed(Key::Escape)) Scene::Quit();
        GameObject object = Scene::Find(player);
        if (!object.isValid) return;
        if (object.position.y < fallLimit) {
            object.position = startPosition;
            object.velocity = Vector3(0.0f, 0.0f, 0.0f);
        }
    }

    void onFrame(double dt)
    {
        VisualElement root = UI::Root();
        if (!root.isValid) return;
        VisualElement win = root.Q("win");
        if (win.isValid) win.SetClass("win--hidden", total == 0 || score < total);
    }

    [bindable] string get_scoreText() const
    {
        return "Score: " + score + " / " + total;
    }
}
