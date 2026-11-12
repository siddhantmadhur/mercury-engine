// Rolls the ball with the Player/Move action (W/A/S/D, the arrow keys or the
// left stick) and jumps with Player/Jump; see input/actions.minput.

[script]
class PlayerController : Behaviour
{
    [field tooltip="Push in newtons at full Move input"] float force = 12.0f;
    [field tooltip="Upward push when Jump is pressed on the ground"] float jumpImpulse = 5.0f;

    private InputAction move;
    private InputAction jump;
    // Actions update once per frame, and a frame can run zero or several ticks,
    // so a press is remembered until the next tick uses it.
    private bool jumpRequested = false;

    void onStart()
    {
        move = Input::RequireAction(GameInput::Player::Move);
        jump = Input::RequireAction(GameInput::Player::Jump);
    }

    void onFrame(double dt)
    {
        if (jump.wasPressedThisFrame) jumpRequested = true;
    }

    void onTick(double dt)
    {
        // The camera looks down -Z, so "up" on screen (Move's +Y) is -Z.
        Vector2 moveValue = move.ReadVector2();
        Vector3 direction(moveValue.x, 0.0f, -moveValue.y);
        if (direction.SqrLength() != 0.0f) {
            Physics::AddForce(GetGameObject(), direction.Scale(force), 0);
        }

        // Only jump when the ball is resting on something.
        bool grounded = Mathf::Abs(GetGameObject().velocity.y) < 0.05f;
        if (jumpRequested && grounded) {
            Physics::AddImpulse(GetGameObject(), Vector3(0.0f, jumpImpulse, 0.0f));
        }
        jumpRequested = false;
    }
}
