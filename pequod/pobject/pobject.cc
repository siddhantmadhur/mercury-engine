//
// Created by Siddhant Madhur on 27/03/2026.
//

#include "pobject.h"
#include <format>
#include "debugger/debugger.h"


namespace Pequod {
    PObject::PObject() {
    }

    PObject::~PObject() {
    }

    void PObject::AddChild(uint64_t child_id) {
        children.push_back(child_id);
    }


    void PObject::SetECS(std::shared_ptr<ECS> _ecs) {
        this->ecs = _ecs;
        for (auto &reg: pending_registrations) {
            reg(ecs, id);
        }
        pending_registrations.clear();
    }
}
