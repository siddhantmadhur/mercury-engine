#ifndef PEQUOD_ENTITY_IMPL_HH_
#define PEQUOD_ENTITY_IMPL_HH_

#include <glm/glm.hpp>
#include <sokol/sokol_app.h>

#define entity_id uint64_t

class Entity {
public:
    Entity();
    ~Entity();
    virtual void OnStart() =0;
    virtual void OnTick() =0;
    virtual void OnFrame() =0;
    virtual void OnEvent(const sapp_event*) =0;
    virtual void OnEnd() =0;
    void setEnabled();
    void setDisabled();
    void toggleDisabled();
    bool isDisabled();
    bool isEnabled();
private:
    entity_id id;
    bool disabled;
};

#endif
