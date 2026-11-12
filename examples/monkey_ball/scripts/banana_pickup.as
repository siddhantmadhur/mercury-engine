[script]
class BananaPickup : Behaviour
{
    [field tooltip="Score awarded upon collection"] int points = 100;
    [field tooltip="Rotation speed in degrees/sec"] float rotateSpeed = 90.0f;
    [field tooltip="Node with GameManager script"] [nodeRef] string gameManager = "GameManager";

    private bool collected = false;

    void onTick(double dt)
    {
        GameObject self = GetGameObject();
        self.eulerAngles = self.eulerAngles + Vector3(0.0f, rotateSpeed * dt, 0.0f);
    }

    void onTriggerEnter(GameObject other)
    {
        if (collected) return;
        if (GetBehaviour(other, "PlayerBall") is null) return;
        collected = true;
        GameManager@ manager = cast<GameManager>(GetBehaviour(Scene::Find(gameManager), "GameManager"));
        if (manager is null) {
            array<GameObject>@ managers = Scene::FindObjectsWithScript("GameManager");
            if (managers.length() > 0) manager = cast<GameManager>(GetBehaviour(managers[0], "GameManager"));
        }
        if (manager !is null) manager.CollectBanana(points);
        GetGameObject().Destroy();
    }
}
