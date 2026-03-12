/**
* game_scene.hh
* 
* This is similar to the "scene.hh" found in the pequod src code
* It is responsible for creating the shaders, handling the vertices, and loading
* the objects for the actual game view
* 
* What is different though is that this does so ONLY for the editor and so 
* objects aren't layed out nor rendered efficiently. Furthermore, the point is
* to allow editing object information easily and then save and load from the disk.
*
**/

#include "ecs/ecs.hh"
#include <memory>
#include <sokol/sokol_app.h>

namespace Pequod {

class GameScene {
public:
    GameScene();
    ~GameScene();
    virtual void OnFrameUpdate() =0;
    virtual void OnEventUpdate(sapp_event* event) =0;
    virtual void OnTickUpdate() =0;

private:
    std::unique_ptr<ECS> ecs;
};

} // namespace Pequod
