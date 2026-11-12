// Keeps the score (shown by ui/scoreboard.uxml) and quits on Escape.

[script]
class PongGame : Behaviour
{
    [field] [nodeRef] string ball = "Ball";

    private Ball@ ballScript;
    private int playerWins = 0;
    private int enemyWins = 0;
    private bool scoredThisTick = false;

    void onStart()
    {
        @ballScript = cast<Ball>(GetBehaviour(Scene::Find(ball), "Ball"));
        if (ballScript is null) Debug::Error("PongGame: \"" + ball + "\" has no Ball script");
        VisualElement root = UI::Root();
        if (root.isValid) root.SetDataSource();
    }

    [callable]
    void Score(int scorer)
    {
        if (scoredThisTick) return;
        scoredThisTick = true;
        if (scorer == 0) {
            Debug::Log("Player scored");
            playerWins += 1;
        } else {
            Debug::Log("Enemy scored");
            enemyWins += 1;
        }
        if (ballScript !is null) ballScript.Serve();
    }

    void onTick(double dt)
    {
        scoredThisTick = false;
        if (Input::IsKeyPressed(Key::Escape)) Scene::Quit();
    }

    [bindable] int get_playerWins() const { return playerWins; }
    [bindable] int get_enemyWins() const { return enemyWins; }
}
