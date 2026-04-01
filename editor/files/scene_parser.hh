/**
* scene_parser.hh
*
* Responsible for parsing XML files and returning entities and components that
* can be loaded into a game scene.
* It is NOT responsible for generating any .cc code to actually compile the
* scene for the final build of the game and is only relevant during editing
*
**/

#ifndef PEQUOD_SCENE_PARSER_IMPL_HH_
#define PEQUOD_SCENE_PARSER_IMPL_HH_

#include <memory>
#include <string>
#include "ecs/ecs.hh"

namespace Pequod {

class SceneParser {
public:
    explicit SceneParser(std::shared_ptr<ECS> ecs);
    void Load(std::string path);
    void Save(std::string path);

private:
    std::shared_ptr<ECS> ecs;
    void ClearECS();
};

}

#endif
