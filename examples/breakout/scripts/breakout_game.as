[script]
class BreakoutGame : Behaviour
{
    private int bricks = 0;
    private int lost = 0;
    private bool announcedWin = false;

    void onStart()
    {
        bricks = Scene::FindAllWithTag("Brick").length();
    }

    void onTick(double dt)
    {
        if (bricks == 0 && !announcedWin) {
            announcedWin = true;
            Debug::Log("You win!");
        }
    }

    [callable]
    void BrickDestroyed()
    {
        if (bricks > 0) bricks--;
    }

    [callable]
    void BallLost()
    {
        lost++;
        Debug::Log("Ball has fallen off");
    }

    [bindable] int get_bricksLeft() const { return bricks; }
    [bindable] int get_ballsLost() const { return lost; }
}
