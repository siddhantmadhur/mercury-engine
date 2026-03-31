/**
 * world_scene.h
 *
 * Scene management file for the game engine. This classes responsibilities are
 * to manage the entire scene for a game world, i.e. the objects rendered inside
 * it, the camera positioning etc.
 * The goal is to be able to extend it for things like the editors game preview,
 * or hard coded scenes such as in the 20-games-examples, though less often.
 *
 * A design choice I've made is that this world scene needs to be completely
 * self-contained, so for example the engine just needs to run an internal
 * function on start, every frame and on events and the scene will automatically
 * decide when to compute physics, update tick etc.
**/

#ifndef PEQUOD_WORLD_SCENE_IMPL_H_
#define PEQUOD_WORLD_SCENE_IMPL_H_

#include <sokol/sokol_app.h>
#include "gameobjects/camera.hh"
#include <ecs/ecs.hh>
#include <vector>

#include <sokol/sokol_gfx.h>

namespace Pequod {
class WorldScene {
public:

    // Initial scene creation, does NOT load any scene, create any buffers etc.
    WorldScene();
    // Initialize the scene and allocate the buffers, resources needed to function
    void Initialize();
    // Destroys the scene, unloads any assets, frees any buffers, etc.
    void Destroy();
    // Renders the scene with selected viewport
    void BeginRenderPass(float width, float height);
    void SetupRenderState();
    void RenderObjects();
    void CompleteRender();
    void ComputeTick(); // call every frame, processes whether tick is run

    // Step that computes all the physics. Should only run once a physics tick.
    // @param steps the number of physics steps to take (should sum to atleast 60 per second)
    void ComputePhysics(int);

    void SetPlayerCamera(Camera& cam);
    Camera& GetPlayerCamera();

    void SetBgColor(glm::vec4);
    bool IsKeyPressed(sapp_keycode key);
    sg_pass_action GetPassAction();


    virtual void OnStart() =0; // runs at scene creation
    virtual void OnFrameUpdate() =0; // runs once every frame
    virtual void OnEvent(const sapp_event* event) =0; // runs once every event
    virtual void OnDestroy() =0; // runs when scene is closing
    virtual void OnTickUpdate(float tick_t) =0; // runs every tick (rn is 60 per second)

    void OnStartInternal();
    void OnFrameInternal();
    void OnEventInternal(const sapp_event* event);

protected:
    // Scene de-constructor, does NOT free resources, should be done by destroy
    ~WorldScene();
    ECS ecs = ECS();

private:
    void handleKeys(const sapp_event*event);

    std::unordered_map<sapp_keycode, bool> keys_pressed;

    float delta_t=0.0f; // time difference per frame
    float tick_t=0.0f;  // time difference per tick
    float elapsed_t=0.0f; // total time elapsed since scene began
    uint64_t frame_time=0;
    uint64_t tick_time=0;

    uint64_t current_tick{};
    uint64_t last_processed_tick{};

    Camera playerCamera;
    glm::vec4 bgColor = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f);

    //@{
    // Variables required by sokol to function
    sg_pipeline pip{};
    sg_pass_action pass_action{};
    sg_bindings bind{};
    //@}
};
}
#endif
