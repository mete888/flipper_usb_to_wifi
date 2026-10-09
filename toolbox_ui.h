#pragma once
#include "toolbox_cards.h"
#include <gui/view.h>

typedef struct ToolboxUI ToolboxUI;
typedef enum {
    ToolboxUIEditLeftCurrency = 100U,
    ToolboxUIEditRightCurrency,
    ToolboxUIEditLeftAmount,
    ToolboxUIEditRightAmount,
    ToolboxUIRefreshEarthquakes,
    ToolboxUISearchWord,
    ToolboxUISearchWikipedia,
    ToolboxUIRefreshWeather,
    ToolboxUIRefreshNationalToday,
    ToolboxUIRefreshISS,
} ToolboxUIAction;
typedef void (*ToolboxUICallback)(void* context, ToolboxUIAction action);
ToolboxUI* toolbox_ui_alloc(ToolboxUICallback callback, void* context);
void toolbox_ui_free(ToolboxUI* ui);
View* toolbox_ui_view(ToolboxUI* ui);
void toolbox_ui_currency(ToolboxUI* ui, const ToolboxConverter* state, const char* note);
bool toolbox_ui_earthquakes(ToolboxUI* ui, const char* response);
bool toolbox_ui_dictionary(ToolboxUI* ui, const char* response);
/* Bounded copies share the existing card union; no extra response allocation. */
void toolbox_ui_reader(ToolboxUI* ui, const char* title, const char* subtitle,
                       const char* body, bool rich, ToolboxUIAction action);
/* Explicitly mark a bounded preview; does not allocate or grow its buffer. */
void toolbox_ui_reader_shortened(ToolboxUI* ui);
bool toolbox_ui_weather(ToolboxUI* ui, const char* location, const char* formatted);
bool toolbox_ui_iss(ToolboxUI* ui, const char* formatted);
void toolbox_ui_note(ToolboxUI* ui, const char* note);
