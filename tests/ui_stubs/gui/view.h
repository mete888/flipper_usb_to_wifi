#pragma once
/* Minimal test doubles for the verified SDK surface used by toolbox_ui.c.
 * This tests input/model behavior, not firmware scheduling or real fonts. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { FontPrimary, FontSecondary } Font;
typedef enum { AlignLeft, AlignRight, AlignTop, AlignBottom } Align;
typedef enum { InputKeyUp, InputKeyDown, InputKeyRight, InputKeyLeft, InputKeyOk, InputKeyBack } InputKey;
typedef enum { InputTypePress, InputTypeRelease, InputTypeShort, InputTypeLong, InputTypeRepeat } InputType;
typedef struct { InputKey key; InputType type; } InputEvent;
typedef struct {
    unsigned font; unsigned lines; unsigned frames;
    char body[512]; char counter[12]; char text[1024];
    char reader_body[512];
    unsigned reader_rows, bold_glyphs, reader_right;
    int last_glyph_y;
    bool last_glyph;
} Canvas;
typedef void (*ViewDrawCallback)(Canvas*, void*);
typedef bool (*ViewInputCallback)(InputEvent*, void*);
typedef struct {
    void* model; void* context; bool locked;
    ViewDrawCallback draw; ViewInputCallback input;
} View;
typedef enum { ViewModelTypeLocking } ViewModelType;
View* view_alloc(void);
void view_free(View* view);
void view_allocate_model(View* view, ViewModelType type, size_t size);
void* view_get_model(View* view);
void view_commit_model(View* view, bool update);
void view_set_context(View* view, void* context);
void view_set_draw_callback(View* view, ViewDrawCallback callback);
void view_set_input_callback(View* view, ViewInputCallback callback);
#define with_view_model(view, type, code, update) \
    { type = view_get_model(view); { code; } view_commit_model(view, update); }
void canvas_clear(Canvas* canvas);
void canvas_set_font(Canvas* canvas, Font font);
uint16_t canvas_string_width(Canvas* canvas, const char* text);
size_t canvas_glyph_width(Canvas* canvas, uint16_t symbol);
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* text);
void canvas_draw_str_aligned(Canvas* canvas, int32_t x, int32_t y, Align h, Align v, const char* text);
void canvas_draw_rframe(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h, size_t r);
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h);
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_circle(Canvas* canvas, int32_t x, int32_t y, size_t radius);
