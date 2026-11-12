enum StageState
{
    StagePlaying,
    StageGoal,
    StageFallOut
};

[script]
class GameManager : Behaviour
{
    [field tooltip="Player ball node"] [nodeRef] string player = "Player";
    [field tooltip="Y height below which player falls out"] float fallLimit = -12.0f;
    [field tooltip="Stage countdown time limit in seconds"] float timeLimit = 60.0f;

    private int score = 0;
    private int falls = 0;
    private float timeRemaining = 60.0f;
    private StageState state = StagePlaying;
    private int bananasCollected = 0;
    private int totalBananas = 0;
    private float elapsed = 0.0f;
    private float messageTimer = 0.0f;
    private Vector3 startPosition = Vector3(0.0f, 1.2f, 0.0f);
    private bool paused = false;
    private VisualElement hud;

    void onStart()
    {
        score = 0;
        bananasCollected = 0;
        totalBananas = Scene::FindObjectsWithScript("BananaPickup").length();
        falls = 0;
        elapsed = 0.0f;
        timeRemaining = timeLimit;
        state = StagePlaying;

        GameObject p = Scene::Find(player);
        if (p.isValid) startPosition = p.position;
        SetupHud();
    }

    bool IsPaused() const { return paused; }

    [callable]
    void CollectBanana(int points)
    {
        bananasCollected++;
        score += points;
        timeRemaining = Mathf::Min(timeLimit + 30.0f, timeRemaining + 3.0f);
    }

    [callable]
    void OnGoalReached()
    {
        if (state == StagePlaying) {
            state = StageGoal;
            score += int(timeRemaining * 50.0f);
        }
    }

    void RestartStage()
    {
        state = StagePlaying;
        timeRemaining = timeLimit;
        elapsed = 0.0f;
        score = 0;
        bananasCollected = 0;
        PutPlayerBack();
    }

    void onTick(double dt)
    {
        if (Input::IsKeyJustPressed(Key::Escape)) SetPaused(!paused);
        if (paused) return;
        if (Input::IsKeyJustPressed(Key::R)) RestartStage();

        if (state == StagePlaying) {
            elapsed += dt;
            timeRemaining = Mathf::Max(0.0f, timeRemaining - dt);

            GameObject p = Scene::Find(player);
            if (p.isValid && p.position.y < fallLimit) {
                falls++;
                messageTimer = 1.5f;
                state = StageFallOut;
                PutPlayerBack();
            }
        } else if (state == StageFallOut) {
            messageTimer -= dt;
            if (messageTimer <= 0.0f) state = StagePlaying;
        }
    }

    void onFrame(double dt)
    {
        if (!hud.isValid) return;
        VisualElement time = hud.Q("time");
        if (time.isValid) time.SetClass("stat__value--low", timeRemaining < 10.0f);
        VisualElement fallOut = hud.Q("fall-out");
        if (fallOut.isValid) fallOut.SetClass("banner--hidden", state != StageFallOut);
        VisualElement goal = hud.Q("goal");
        bool cleared = state == StageGoal;
        if (goal.isValid && cleared && goal.HasClass("banner--hidden")) {
            VisualElement summary = hud.Q("goal-summary");
            if (summary.isValid) {
                summary.SetText("Time <b>" + timeRemaining + "s</b>   " +
                                "Bananas <color=#FFE433><b>" + bananasCollected + " / " +
                                totalBananas + "</b></color>\n" +
                                "Score <b>" + score + "</b>   Falls <b>" + falls + "</b>");
            }
        }
        if (goal.isValid) goal.SetClass("banner--hidden", !cleared);
    }

    // --- HUD -------------------------------------------------------------------------------

    void SetupHud()
    {
        hud = UI::Root();
        if (!hud.isValid) return;
        hud.SetDataSource();
        VisualElement resume = hud.Q("resume");
        if (resume.isValid) resume.OnClick("ResumePressed");
        VisualElement restart = hud.Q("restart");
        if (restart.isValid) restart.OnClick("RestartPressed");
        VisualElement playAgain = hud.Q("play-again");
        if (playAgain.isValid) playAgain.OnClick("PlayAgainPressed");
        VisualElement quit = hud.Q("quit");
        if (quit.isValid) quit.OnClick("QuitPressed");
        VisualElement goalQuit = hud.Q("goal-quit");
        if (goalQuit.isValid) goalQuit.OnClick("QuitPressed");
        VisualElement hints = hud.Q("show-hints");
        if (hints.isValid) hints.OnToggleChanged("HintsChanged");
    }

    void ResumePressed() { SetPaused(false); }
    void RestartPressed() { SetPaused(false); RestartStage(); }
    void PlayAgainPressed() { RestartStage(); }
    void QuitPressed() { Scene::Quit(); }

    void HintsChanged(bool on)
    {
        if (!hud.isValid) return;
        VisualElement hint = hud.QClass("hint");
        if (hint.isValid) hint.SetClass("hint--hidden", !on);
    }

    void SetPaused(bool value)
    {
        paused = value;
        if (!hud.isValid) return;
        VisualElement pause = hud.Q("pause");
        if (pause.isValid) pause.SetClass("hidden", !paused);
        if (paused) {
            VisualElement resume = hud.Q("resume");
            if (resume.isValid) resume.Focus();
        } else {
            hud.BlurFocused();
        }
    }

    void PutPlayerBack()
    {
        GameObject p = Scene::Find(player);
        if (!p.isValid) return;
        p.position = startPosition;
        PlayerBall@ ball = cast<PlayerBall>(GetBehaviour(p, "PlayerBall"));
        if (ball !is null) ball.ResetPhysics();
    }

    [bindable] int get_score() const { return score; }
    [bindable] int get_falls() const { return falls; }
    [bindable] float get_timeRemaining() const { return timeRemaining; }
    [bindable] string get_timeText() const { return timeRemaining + " s"; }
    [bindable] string get_bananasText() const { return bananasCollected + " / " + totalBananas; }
}
