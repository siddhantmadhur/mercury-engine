// Engine-provided base classes. They exist so scripts can pass `this` to the
// native API and so one script can cast another to its concrete class:
//
//   [script]
//   class Ball : Behaviour
//   {
//       [field] float speed = 60.0f;
//       void onStart() { GetGameObject(this).position = Vector3(0, 0, 0); }
//   }
//
// `IBehaviour` and `IDataAsset` are registered by the engine before this file
// is compiled. Everything else is a native free function or value type.
static const char* kApiPreludeSource = R"AS(
class Behaviour : IBehaviour
{
}

class DataAsset : IDataAsset
{
}
)AS";
