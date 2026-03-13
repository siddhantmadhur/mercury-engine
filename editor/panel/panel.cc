#include "panel.hh"
#include <panel.h>

namespace Pequod {

Panel::Panel(bool init_show) {
    this->show_panel = init_show;
}

void Panel::Hide() {
    this->show_panel = false;
}

void Panel::Show() {
    this->show_panel = true;
}
void Panel::Toggle() {
    show_panel = !show_panel;
}

bool Panel::IsShown() {
    return show_panel; 
}

} // namespace Pequod
