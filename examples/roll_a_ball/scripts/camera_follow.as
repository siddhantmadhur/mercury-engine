// Keeps the camera at the offset it starts with from the target.

[script]
class CameraFollow : Behaviour
{
    [field] [nodeRef] string target = "";

    private GameObject targetObject;
    private Vector3 offset;

    void onStart()
    {
        targetObject = Scene::Find(target);
        if (targetObject.isValid) {
            offset = GetGameObject().position - targetObject.position;
        }
    }

    void onTick(double dt)
    {
        if (!targetObject.isValid) return;
        GetGameObject().position = targetObject.position + offset;
    }
}
