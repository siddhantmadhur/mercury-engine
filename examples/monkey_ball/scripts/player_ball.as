[script]
class PlayerBall : Behaviour
{
    [field tooltip="Continuous push force applied when rolling"] float rollForce = 24.0f;
    [field tooltip="Upward impulse applied when Space is pressed"] float jumpImpulse = 7.5f;
    [field tooltip="Maximum horizontal speed before damping is applied"] float maxSpeed = 20.0f;
    [field tooltip="Camera node for orienting movement direction"] [nodeRef] string cameraNode = "Camera";

    private int contactCount = 0;
    private bool grounded = false;
    private float jumpCooldown = 0.0f;

    bool IsGrounded() const { return grounded; }

    void onStart()
    {
        contactCount = 0;
        grounded = true;
        jumpCooldown = 0.0f;
    }

    void ResetPhysics()
    {
        contactCount = 0;
        grounded = true;
        jumpCooldown = 0.0f;
        GameObject self = GetGameObject();
        self.velocity = Vector3(0.0f, 0.0f, 0.0f);
        Physics::SetAngularVelocity(self, Vector3(0.0f, 0.0f, 0.0f));
    }

    void onTick(double dt)
    {
        GameManager@ manager = cast<GameManager>(
            GetBehaviour(Scene::Find("GameManager"), "GameManager"));
        if (manager !is null && manager.IsPaused()) return;

        jumpCooldown = Mathf::Max(0.0f, jumpCooldown - dt);
        GameObject self = GetGameObject();
        Vector3 vel = self.velocity;
        Vector3 pos = self.position;

        grounded = contactCount > 0 || Mathf::Abs(vel.y) < 0.2f;

        Vector3 forward(0.0f, 0.0f, -1.0f);
        GameObject cam = Scene::Find(cameraNode);
        if (cam.isValid) {
            Vector3 toBall = pos - cam.position;
            toBall.y = 0.0f;
            if (toBall.Length() > 1e-3f) forward = toBall.Normalized();
        }
        Vector3 right(-forward.z, 0.0f, forward.x);

        Vector3 inputDir(0.0f, 0.0f, 0.0f);
        if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up)) inputDir = inputDir + forward;
        if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down)) inputDir = inputDir - forward;
        if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left)) inputDir = inputDir - right;
        if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) inputDir = inputDir + right;

        if (inputDir.Length() > 1e-3f) {
            inputDir = inputDir.Normalized();
            Physics::AddForce(self, inputDir.Scale(rollForce), 0);
            Vector2 horizontalVelocity(vel.x, vel.z);
            Vector2 horizontalInput(inputDir.x, inputDir.z);
            if (horizontalVelocity.Dot(horizontalInput) < 0.0f) {
                Physics::AddForce(self, inputDir.Scale(rollForce * 0.7f), 0);
            }
        }

        float horizontalSpeed = Mathf::Sqrt(vel.x * vel.x + vel.z * vel.z);
        if (horizontalSpeed > maxSpeed) {
            float excess = horizontalSpeed - maxSpeed;
            Vector3 brake((-vel.x / horizontalSpeed) * excess * 5.0f, 0.0f,
                          (-vel.z / horizontalSpeed) * excess * 5.0f);
            Physics::AddForce(self, brake, 0);
        }

        if (Input::IsKeyJustPressed(Key::Space) && grounded && jumpCooldown <= 0.0f) {
            Physics::AddImpulse(self, Vector3(0.0f, jumpImpulse, 0.0f));
            grounded = false;
            jumpCooldown = 0.25f;
        }
    }

    bool IsGhost(GameObject other)
    {
        return GetBehaviour(other, "BananaPickup") !is null ||
               GetBehaviour(other, "GoalTrigger") !is null;
    }

    void onCollisionEnter(GameObject other)
    {
        if (IsGhost(other)) return;
        contactCount++;
    }

    void onCollisionLeave(GameObject other)
    {
        if (IsGhost(other)) return;
        contactCount = contactCount > 0 ? contactCount - 1 : 0;
    }
}
