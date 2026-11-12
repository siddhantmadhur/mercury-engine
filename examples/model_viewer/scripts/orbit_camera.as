// Turns the camera around a point with the mouse: left-drag orbits,
// right/middle-drag pans, the wheel zooms, R resets.

[script]
class OrbitCamera : Behaviour
{
    [field tooltip="Node whose mesh to frame (when distance is 0)"] [nodeRef] string target = "";
    [field tooltip="Point to orbit around"] Vector3 focus = Vector3(0.0f, 0.0f, 0.0f);
    [field tooltip="Distance from the focus; 0 frames the target"] float distance = 0.0f;
    [field tooltip="Starting angle around the vertical, degrees"] float yaw = 0.0f;
    [field tooltip="Starting angle above the horizon, degrees"] float pitch = 15.0f;
    [field tooltip="Degrees per second while idle"] float autoRotate = 6.0f;

    private Vector3 startFocus;
    private float startDistance = 1.0f;
    private float startYaw = 0.0f;
    private float startPitch = 0.0f;
    private float idle = 0.0f;
    private float scroll = 0.0f;

    void onStart()
    {
        if (distance <= 0.0f) {
            distance = 5.0f;
            GameObject object = Scene::Find(target);
            if (object.isValid && object.hasBounds) {
                Vector3 scale = object.localScale;
                Vector3 lo(object.boundsMin.x * scale.x, object.boundsMin.y * scale.y,
                           object.boundsMin.z * scale.z);
                Vector3 hi(object.boundsMax.x * scale.x, object.boundsMax.y * scale.y,
                           object.boundsMax.z * scale.z);
                focus = object.position + (lo + hi).Scale(0.5f);
                distance = Mathf::Max((hi - lo).Length() * 1.1f, 0.1f);
            }
        }
        startFocus = focus;
        startDistance = distance;
        startYaw = yaw;
        startPitch = pitch;
        Apply();
    }

    void onFrame(double dt)
    {
        if (!Input::IsPointerOverUI()) scroll += Input::Scroll().y;
    }

    void onTick(double dt)
    {
        Vector2 delta = Input::CursorDelta();
        float wheel = scroll;
        scroll = 0.0f;
        bool overUi = Input::IsPointerOverUI();
        bool moved = false;
        bool moving = !(delta == Vector2(0.0f, 0.0f));

        if (!overUi && Input::IsMouseButtonPressed(MouseButton::Left)) {
            yaw -= delta.x * 0.3f;
            pitch = Mathf::Min(89.0f, Mathf::Max(-89.0f, pitch + delta.y * 0.3f));
            moved = moving;
        }
        if (!overUi && (Input::IsMouseButtonPressed(MouseButton::Right) ||
                        Input::IsMouseButtonPressed(MouseButton::Middle))) {
            Quaternion rotation = QuaternionEuler(-pitch, yaw, 0.0f);
            float speed = distance * 0.0015f;
            focus = focus + rotation.Rotate(Vector3(-delta.x * speed, delta.y * speed, 0.0f));
            moved = moving;
        }
        if (wheel != 0.0f) {
            distance = Mathf::Max(distance * Mathf::Pow(0.9f, wheel), 0.01f);
            moved = true;
        }
        if (Input::IsKeyJustPressed(Key::R)) {
            focus = startFocus;
            distance = startDistance;
            yaw = startYaw;
            pitch = startPitch;
            moved = true;
        }

        idle = moved ? 0.0f : idle + dt;
        if (idle > 3.0f) yaw += autoRotate * dt;
        Apply();
    }

    private void Apply()
    {
        Quaternion rotation = QuaternionEuler(-pitch, yaw, 0.0f);
        GameObject self = GetGameObject();
        self.position = focus + rotation.Rotate(Vector3(0.0f, 0.0f, distance));
        self.eulerAngles = Vector3(-pitch, yaw, 0.0f);
    }
}
