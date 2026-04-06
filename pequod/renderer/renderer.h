//
// Created by smadhur on 4/5/2026.
//

#ifndef PEQUODENGINE_RENDERER_H
#define PEQUODENGINE_RENDERER_H
#include "properties/mesh.h"
#include <memory>

namespace Pequod
{
    class Renderer
    {
    public:
        Renderer();
        bool AddMesh(std::shared_ptr<Mesh>);
        bool Render();
    };
}

#endif //PEQUODENGINE_RENDERER_H
