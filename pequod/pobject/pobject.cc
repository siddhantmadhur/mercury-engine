//
// Created by Siddhant Madhur on 27/03/2026.
//

#include "pobject.h"
#include <format>
#include "debugger/debugger.hh"

namespace Pequod {
    PObject::PObject(uint64_t id) {
        this->id = id;
    }
    PObject::~PObject() {
    }

    void PObject::AddChild(uint64_t child_id) {
        children.push_back(child_id);
    }
}
