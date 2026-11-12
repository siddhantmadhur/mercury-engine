[script]
class RotatingHazard : Behaviour
{
    [field tooltip="Euler rotation speed in degrees per second"] Vector3 degreesPerSecond =
        Vector3(0.0f, 60.0f, 0.0f);

    void onTick(double dt)
    {
        GameObject self = GetGameObject();
        Vector3 rot = self.eulerAngles + degreesPerSecond.Scale(dt);
        self.eulerAngles = Vector3(Mathf::Repeat(rot.x, 360.0f), Mathf::Repeat(rot.y, 360.0f),
                                   Mathf::Repeat(rot.z, 360.0f));
    }
}
