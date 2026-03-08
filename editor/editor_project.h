#ifndef PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_
#define PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_

#include "editor_scene.h"

namespace Pequod {
class ProjectSelectionScene : public EditorScene {
public:
    void OnInitialLoad();
    void OnEventUpdate(const sapp_event* event);
    void OnFrameUpdate();
    void OnTickUpdate();
    void OnDestroy();
};
}

#endif
