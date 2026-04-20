#ifndef PEQUOD_ENTITY_IMPL_HH_
#define PEQUOD_ENTITY_IMPL_HH_

#include <globals.h>
#include <sokol/sokol_app.h>

#include <glm/glm.hpp>

// [CLAUDE] TODO: This class is defined outside namespace Pequod and is never
// inherited or used — remove or integrate
class Entity {
 public:
  Entity();

  ~Entity();

  virtual void OnStart() = 0;

  virtual void OnTick() = 0;

  virtual void OnFrame() = 0;

  virtual void OnEvent(const sapp_event *) = 0;

  virtual void OnEnd() = 0;

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
