[script]
class GoalTrigger : Behaviour
{
    [field tooltip="Node with GameManager script"] [nodeRef] string gameManager = "GameManager";

    private bool triggered = false;

    void onTriggerEnter(GameObject other)
    {
        if (triggered) return;
        if (GetBehaviour(other, "PlayerBall") is null) return;
        triggered = true;
        GameManager@ manager = cast<GameManager>(GetBehaviour(Scene::Find(gameManager), "GameManager"));
        if (manager !is null) {
            manager.OnGoalReached();
            return;
        }
        array<GameObject>@ managers = Scene::FindObjectsWithScript("GameManager");
        if (managers.length() > 0) {
            manager = cast<GameManager>(GetBehaviour(managers[0], "GameManager"));
            if (manager !is null) manager.OnGoalReached();
        }
    }
}
