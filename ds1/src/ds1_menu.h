// ds1-nx - the port's options menu (ZL + ZR + Minus): see ds1_menu.cpp.

#pragma once

namespace rex {
class Runtime;
}

namespace ds1::menu {

// Takes the controller filter and shows the menu when asked; call once the runtime exists.
void Instalar(rex::Runtime& runtime);

}  // namespace ds1::menu
