#include "ecs/ecs.hh"
#include <gameobjects/gameobject.hh>
#include <iostream>
#include <debugger/debugger.hh>

namespace Pequod {
    GameObject::~GameObject() {
        PDebug::log("releasing game object...");
    }
}
