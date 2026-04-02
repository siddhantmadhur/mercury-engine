//
// Created by smadhur on 4/1/26.
//

#include "manager.h"

#include "nodes/box2d.h"

#include "properties/position.h"
#include "properties/mesh.h"



#define RUN_ON_ALL_NODES(func) {    \
    func(Box2D);                    \
}

namespace Pequod {
    PObjectManager::PObjectManager(std::shared_ptr<ECS> ecs) {
        if (!ecs) {
            PDebug::error("nullptr ecs provided to object manager");
        }
        this->ecs = ecs;
        this->root = NewObject<PObject>();
    }



    void PObjectManager::DeleteObject(std::shared_ptr<PObject> object) {

    }

}
