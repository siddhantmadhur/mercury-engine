#include "scene.hh"
#include "Jolt/Math/Quat.h"
#include "Jolt/Math/Real.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/NarrowPhaseQuery.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/EActivation.h"
#include "ecs/entity.hh"
#include "gameobjects/camera.hh"
#include "glm/fwd.hpp"
#include "rigidbody/rigidbody.hh"
#include <format>
#include <iostream>
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include <debugger/debugger.hh>


#define ZOOM 8.0f
    
#define heights (sapp_heightf() * (1 / ZOOM))
#define widths (sapp_widthf() * (1 / ZOOM))

void PongScene::ResetRound() {

}

void PongScene::OnStart() {
}

void PongScene::OnDestroy() {
}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    } 
}
    
#define SPEED 0.4f


void PongScene::OnTickUpdate(float tick_t) {

}

void PongScene::OnFrameUpdate() {
}
