#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <input/input.h>
typedef enum { FontPrimary, FontSecondary } Font;
typedef struct { Font font; char text[1024]; } Canvas;
typedef struct ViewPort ViewPort;
void canvas_clear(Canvas*);
void canvas_set_font(Canvas*, Font);
uint16_t canvas_string_width(Canvas*, const char*);
void canvas_draw_str(Canvas*, int32_t, int32_t, const char*);
void canvas_draw_rframe(Canvas*, int32_t, int32_t, size_t, size_t, size_t);
ViewPort* view_port_alloc(void);
void view_port_free(ViewPort*);
void view_port_draw_callback_set(ViewPort*, void (*)(Canvas*, void*), void*);
void view_port_input_callback_set(ViewPort*, void (*)(InputEvent*, void*), void*);
void view_port_enabled_set(ViewPort*, bool);
void view_port_update(ViewPort*);
