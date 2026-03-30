/**
* game_preview.hh
*
* renders the game scene in a small panel 
**/
#ifndef PEQUOD_PANEL_GAME_PREVIEW_IMPL_HH_
#define PEQUOD_PANEL_GAME_PREVIEW_IMPL_HH_


#include <panel/panel.hh>

namespace Pequod {

class GamePreview : public Panel {
public:
    GamePreview();
    void Initialize() override;
    void Draw() override;


private:

};

} // namespace Pequod

#endif
