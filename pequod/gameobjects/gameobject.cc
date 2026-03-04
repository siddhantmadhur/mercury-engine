#include "ecs/ecs.hh"
#include <gameobjects/gameobject.hh>
#include <iostream>
#include <debugger/debugger.hh>

GameObject::~GameObject() {
    PDebug::log("releasing game object...");
}
/**
GameObject::GameObject() {
    std::cout << "game object" << std::endl;
}
**/

