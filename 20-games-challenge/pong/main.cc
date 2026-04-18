#include <iostream>

#include <engine/engine.hh>
#include <ecs/ecs.hh>


#include "application/application.h"
#include "application/d3d11_application.h"

using namespace Pequod;


int main(void)
{
    auto *pong = new D3D11Application("Pong");

    return pong->Run();
}