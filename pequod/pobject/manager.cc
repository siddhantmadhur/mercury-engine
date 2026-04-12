//
// Created by smadhur on 4/1/26.
//

#include "manager.h"

#include "nodes/box2d.h"

#include "properties/mesh.h"


// [CLAUDE] TODO: RUN_ON_ALL_NODES macro is defined but never called — remove
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


    // [CLAUDE] TODO: DeleteObject is an empty stub — objects added via NewObject are never freed (memory leak)
    void PObjectManager::DeleteObject(std::shared_ptr<PObject> object) {
    }
}
