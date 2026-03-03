#include "ecs/ecs.hh"
#include <gameobjects/gameobject.hh>
#include <iostream>


GameObject::~GameObject() {
    std::cout << "releasing game object" << std::endl;
}
/**
GameObject::GameObject() {
    std::cout << "game object" << std::endl;
}
**/

