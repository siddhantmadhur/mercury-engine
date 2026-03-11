#ifndef EDITOR_SCENE_IMPL_HH_
#define EDITOR_SCENE_IMPL_HH_

#include <sokol/sokol_app.h>

namespace Pequod {
class EditorScene {
public:
    EditorScene(); 

    /**
    * Extend these functions to create functionality in scenes
    **/
    virtual void OnInitialLoad() =0;
    virtual void OnEventUpdate(const sapp_event* event) =0;
    virtual void OnFrameUpdate() =0;
    virtual void OnTickUpdate() =0;
    virtual void OnDestroy() =0;

    /**
    * These functions are meant for the engine to interface with scenes directly.
    **/
    void InitializeScene();
    void OnEventInternal(const sapp_event* event);     
    void OnFrameInternal();

};
}

#endif
