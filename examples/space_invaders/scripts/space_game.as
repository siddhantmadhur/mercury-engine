[script]
class SpaceGame : Behaviour
{
    private int score = 0;
    private int enemies = 0;
    private bool announcedWin = false;
    private array<GameObject>@ pellets;

    void onStart()
    {
        enemies = Scene::FindAllWithTag("Enemy").length();
        @pellets = Scene::FindAllWithTag("Pellet");
        for (uint i = 0; i < pellets.length(); i++) pellets[i].active = false;
    }

    void onTick(double dt)
    {
        if (enemies == 0 && !announcedWin) {
            announcedWin = true;
            Debug::Log("You win! Score: " + score);
        }
    }

    void SpawnPellet(Vector3 position)
    {
        if (pellets is null) return;
        for (uint i = 0; i < pellets.length(); i++) {
            GameObject pellet = pellets[i];
            if (pellet.activeSelf) continue;
            pellet.position = position;
            pellet.velocity = Vector3(0.0f, 140.0f, 0.0f);
            pellet.active = true;
            return;
        }
    }

    [callable]
    void EnemyKilled()
    {
        if (enemies > 0) enemies--;
    }

    [callable]
    void AddScore(int points)
    {
        score += points;
    }

    [bindable] int get_score() const { return score; }
    [bindable] int get_enemiesLeft() const { return enemies; }
}
