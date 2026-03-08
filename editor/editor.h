/**
* editor.h
* 
* This file manages the main editor instance. It will allow the user
* to create and manage scenes, assets, handle files and link properties
* to objects (like physics or animations)
*
* This is the main interface for the editor and does not directly manage
* resources. That is left upto each scene in the editor. For example,
* the first scene you see might be a project picker, which will be its own
* file with its own resources. This editor interface only manages the cleanup
* 
**/

#ifndef PEQUOD_EDITOR_MAIN_IMPL_HH_
#define PEQUOD_EDITOR_MAIN_IMPL_HH_

#include "editor_scene.h"

namespace Pequod {

class Editor {
public:
    Editor(EditorScene& init_scene); 

    /**
    * These functions are to manage the main editor instance
    **/
    void InitializeEditor();
    void SetScene(EditorScene& scene);

    /**
    * These sokol_* functions are only to be used inside of sokol functions with 
    * no extension
    **/
    void sokol_init();
    void sokol_frame_cb();
    void sokol_cleanup();
    void sokol_event(const sapp_event* event);

private:
    EditorScene &currentScene;
};

};

#endif
