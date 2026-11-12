[script]
class OscillatingObstacle : Behaviour
{
    [field tooltip="Direction of translation"] Vector3 moveAxis = Vector3(1.0f, 0.0f, 0.0f);
    [field tooltip="Half-distance of movement"] float distance = 2.5f;
    [field tooltip="Oscillation frequency in radians/sec"] float speed = 2.0f;

    private Vector3 origin;
    private float time = 0.0f;

    void onStart()
    {
        origin = GetGameObject().position;
    }

    void onTick(double dt)
    {
        time += dt;
        float offset = Mathf::Sin(time * speed) * distance;
        GetGameObject().position = origin + moveAxis.Scale(offset);
    }
}
