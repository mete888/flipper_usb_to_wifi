#pragma once
#include "view_port.h"
typedef struct Gui Gui;
typedef enum { GuiLayerFullscreen } GuiLayer;
#define RECORD_GUI "gui"
void gui_add_view_port(Gui*, ViewPort*, GuiLayer);
void gui_remove_view_port(Gui*, ViewPort*);
