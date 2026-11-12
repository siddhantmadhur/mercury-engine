// Smooth third-person chase camera for Monkey Ball with manual orbit control.

[script]
class MonkeyCamera : Behaviour
{
    [field tooltip="Node to follow (typically Player)"] [nodeRef] string target = "Player";
    [field tooltip="Distance behind the ball"] float distance = 7.0f;
    [field tooltip="Height above the ball"] float height = 3.5f;
    [field tooltip="Downwards look angle in degrees"] float pitch = -22.0f;
    [field tooltip="Horizontal orbit angle in degrees"] float yaw = 0.0f;
    [field tooltip="Rotation speed in degrees/sec when Q/E are held"] float turnSpeed = 90.0f;
    [field tooltip="Follow interpolation rate"] float smoothSpeed = 10.0f;

    private GameObject targetObject;

    Vector3 Offset()
    {
        float yawRad = yaw * Mathf::Deg2Rad();
        return Vector3(Mathf::Sin(yawRad) * distance, height, Mathf::Cos(yawRad) * distance);
    }

    void onStart()
    {
        targetObject = Scene::Find(target);
        if (targetObject.isValid) {
            GameObject self = GetGameObject();
            self.position = targetObject.position + Offset();
            self.eulerAngles = Vector3(pitch, yaw, 0.0f);
        }
    }

    void onTick(double dt)
    {
        if (!targetObject.isValid) {
            targetObject = Scene::Find(target);
            if (!targetObject.isValid) return;
        }
        if (Input::IsKeyPressed(Key::Q)) yaw -= turnSpeed * dt;
        if (Input::IsKeyPressed(Key::E)) yaw += turnSpeed * dt;
        yaw = Mathf::Repeat(yaw, 360.0f);

        GameObject self = GetGameObject();
        Vector3 targetPos = targetObject.position + Offset();
        float factor = Mathf::Min(1.0f, smoothSpeed * dt);
        self.position = self.position + (targetPos - self.position).Scale(factor);
        self.eulerAngles = Vector3(pitch, yaw, 0.0f);
    }
}
