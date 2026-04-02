//
// Created by smadhur on 3/31/26.
//

#ifndef PEQUOD_EDITOR_OBJ_PROPERTIES_H_
#define PEQUOD_EDITOR_OBJ_PROPERTIES_H_
#include <memory>
#include <pobject/manager.h>

#include "panel.hh"
#include "ecs/ecs.hh"
#include "ecs/entity.hh"

namespace Pequod {
    class ObjectPropertiesPanel : public Panel {
    public:
        ObjectPropertiesPanel(entity_id &current_selected_id, std::shared_ptr<PObjectManager> manager);

        void Initialize() override;

        void Draw() override;

        void DrawProperties(entity_id id);

    private:
        entity_id &selected_id;
        entity_id prev_id = 0; // use to check if selection has changed
        std::shared_ptr<PObjectManager> manager = nullptr;

        char input_name[255];
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };
}

#endif //
