#include "toolbox_ui.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

View* view_alloc(void) { return calloc(1U, sizeof(View)); }
void view_free(View* view) { assert(!view->locked); free(view->model); free(view); }
void view_allocate_model(View* view, ViewModelType type, size_t size) {
    (void)type; view->model = calloc(1U, size); assert(view->model);
}
void* view_get_model(View* view) { assert(!view->locked); view->locked = true; return view->model; }
void view_commit_model(View* view, bool update) { (void)update; assert(view->locked); view->locked = false; }
void view_set_context(View* view, void* context) { view->context = context; }
void view_set_draw_callback(View* view, ViewDrawCallback callback) { view->draw = callback; }
void view_set_input_callback(View* view, ViewInputCallback callback) { view->input = callback; }
void canvas_clear(Canvas* canvas) { memset(canvas, 0, sizeof(*canvas)); }
void canvas_set_font(Canvas* canvas, Font font) { canvas->font = (unsigned)font; }
uint16_t canvas_string_width(Canvas* canvas, const char* text) {
    /* Like u8g2, the last letter's painted box can be narrower than its advance.
     * A constant-width stub concealed the old single-glyph measurement bug. */
    size_t width = strlen(text) * (canvas->font == FontPrimary ? 7U : 6U);
    return (uint16_t)(width ? width - 2U : 0U);
}
size_t canvas_glyph_width(Canvas* canvas, uint16_t symbol) {
    (void)symbol;
    return canvas->font == FontPrimary ? 7U : 6U;
}
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* text) {
    assert(x >= 0 && x < 128 && y >= 0 && y < 64);
    bool glyph = strlen(text) == 1U && x >= 3 && x < 117 &&
        (y == 24 || y == 35 || y == 46 || y == 57 || y == 36 || y == 47 || y == 58);
    size_t all = strlen(canvas->text);
    assert(all + strlen(text) + 2U < sizeof(canvas->text));
    if(glyph) {
        unsigned right = (unsigned)x + canvas_glyph_width(canvas, (uint8_t)text[0]);
        assert(right <= 117U); /* Leave the scrollbar and right padding untouched. */
        if(right > canvas->reader_right) canvas->reader_right = right;
        bool new_row = !canvas->last_glyph || canvas->last_glyph_y != y;
        if(new_row) ++canvas->reader_rows;
        if(canvas->font == FontPrimary) ++canvas->bold_glyphs;
        snprintf(canvas->text + all, sizeof(canvas->text) - all, "%s%s", new_row ? "\n" : "", text);
        size_t body_length = strlen(canvas->reader_body);
        assert(body_length + 3U < sizeof(canvas->reader_body));
        snprintf(canvas->reader_body + body_length, sizeof(canvas->reader_body) - body_length,
                 "%s%s", new_row && body_length ? "\n" : "", text);
    } else snprintf(canvas->text + all, sizeof(canvas->text) - all, "%s\n", text);
    canvas->last_glyph = glyph; canvas->last_glyph_y = y;
    if(x == 7 && (y == 35 || y == 46 || y == 57)) {
        size_t length = strlen(canvas->body);
        assert(length + strlen(text) + 2U < sizeof(canvas->body));
        snprintf(canvas->body + length, sizeof(canvas->body) - length, "%s\n", text);
        ++canvas->lines;
    }
}
void canvas_draw_str_aligned(Canvas* canvas, int32_t x, int32_t y, Align h, Align v, const char* text) {
    (void)h; (void)v; canvas_draw_str(canvas, x, y, text);
    if(y == 9) snprintf(canvas->counter, sizeof(canvas->counter), "%s", text);
}
void canvas_draw_rframe(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h, size_t r) {
    ++canvas->frames; (void)r;
    assert(x >= 0 && y >= 0 && (size_t)x + w <= 128U && (size_t)y + h <= 64U);
}
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t w, size_t h) {
    (void)canvas;
    assert(x >= 0 && y >= 0 && (size_t)x + w <= 128U && (size_t)y + h <= 64U);
}
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    (void)canvas;
    assert(x1 >= 0 && x1 < 128 && x2 >= 0 && x2 < 128);
    assert(y1 >= 0 && y1 < 64 && y2 >= 0 && y2 < 64);
}
void canvas_draw_circle(Canvas* canvas, int32_t x, int32_t y, size_t radius) {
    (void)canvas;
    assert(x >= (int32_t)radius && x + (int32_t)radius < 128);
    assert(y >= (int32_t)radius && y + (int32_t)radius < 64);
}

typedef struct { View* view; ToolboxUIAction last; unsigned count; } Actions;
static void action(void* context, ToolboxUIAction value) {
    Actions* actions = context;
    assert(!actions->view->locked); /* Never call the app while holding its view model. */
    actions->last = value; ++actions->count;
}
static bool key(View* view, InputKey value, InputType type) {
    InputEvent event = {.key = value, .type = type};
    return view->input(&event, view->context);
}
static void render(View* view, Canvas* canvas) {
    view->draw(canvas, view_get_model(view)); view_commit_model(view, false);
}
int main(void) {
    Actions actions = {0};
    ToolboxUI* ui = toolbox_ui_alloc(action, &actions);
    assert(ui); actions.view = toolbox_ui_view(ui);
    View* view = actions.view; Canvas canvas;
    ToolboxConverter converter; toolbox_converter_init(&converter);
    toolbox_ui_currency(ui, &converter, NULL); render(view, &canvas);
    assert(canvas.frames == 5U); /* Keep the four-field converter unchanged. */
    key(view, InputKeyOk, InputTypePress); assert(actions.count == 0U);
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIEditLeftCurrency);
    key(view, InputKeyRight, InputTypeShort);
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIEditRightCurrency);
    key(view, InputKeyDown, InputTypeShort);
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIEditRightAmount);
    key(view, InputKeyLeft, InputTypeShort);
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIEditLeftAmount);
    assert(!key(view, InputKeyBack, InputTypeShort));

    assert(toolbox_ui_earthquakes(ui,
        "FIBTOOLS1\nM 4.1 - Offshore Fiji\n10-04 07:30 UTC\nDepth: 12.7 km\n\n"
        "M 2.5 - Near Japan\n10-04 07:20 UTC\nDepth: 4.0 km\n\nSource: USGS"));
    render(view, &canvas); assert(!strcmp(canvas.counter, "1/2"));
    assert(!strstr(canvas.text, "MAG") && strstr(canvas.text, "M 4.1\n"));
    assert(canvas.frames == 4U && strstr(canvas.text, "USGS"));
    key(view, InputKeyRight, InputTypeShort); render(view, &canvas);
    assert(!strcmp(canvas.counter, "2/2"));
    key(view, InputKeyRight, InputTypeRepeat); render(view, &canvas);
    assert(!strcmp(canvas.counter, "2/2"));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIRefreshEarthquakes);

    char ten[1400] = "FIBTOOLS1\n";
    for(unsigned n = 0U; n < 10U; ++n)
        strcat(ten, "M 1.0 - A remote earthquake location with enough words to exercise scrolling\n10-04 07:30 UTC\nDepth: 1.0 km\n\n");
    strcat(ten, "Source: USGS");
    assert(toolbox_ui_earthquakes(ui, ten));
    render(view, &canvas); assert(!strcmp(canvas.counter, "1/10"));
    for(unsigned n = 0U; n < 20U; ++n) { key(view, InputKeyRight, InputTypeRepeat); render(view, &canvas); }
    assert(!strcmp(canvas.counter, "10/10"));
    for(unsigned n = 0U; n < 100U; ++n) { key(view, InputKeyDown, InputTypeRepeat); render(view, &canvas); }
    assert(strstr(canvas.text, "scrolling"));

    assert(toolbox_ui_dictionary(ui,
        "FIBTOOLS1\nhello\n\nnoun: A greeting used when meeting another person to start "
        "a friendly conversation.\nExample: Hello, friend!\n\nverb: To greet someone.\n\n"
        "Source: https://en.wiktionary.org/wiki/hello\nCC BY-SA 3.0\n"
        "https://creativecommons.org/licenses/by-sa/3.0\nShortened ASCII text"));
    render(view, &canvas); assert(!strcmp(canvas.counter, "1/3"));
    assert(canvas.frames == 3U && !strstr(canvas.text, "Meaning  ^v Scroll"));
    assert(canvas.lines == 3U);
    for(unsigned n = 0; n < 100U; ++n) { key(view, InputKeyDown, InputTypeRepeat); render(view, &canvas); }
    assert(canvas.lines == 3U && strstr(canvas.body, "friend!"));
    key(view, InputKeyRight, InputTypeShort); render(view, &canvas);
    assert(!strcmp(canvas.counter, "2/3") && strstr(canvas.body, "To greet someone."));
    key(view, InputKeyRight, InputTypeShort); render(view, &canvas);
    assert(!strcmp(canvas.counter, "3/3"));
    for(unsigned n = 0; n < 100U; ++n) { key(view, InputKeyDown, InputTypeRepeat); render(view, &canvas); }
    assert(canvas.lines == 3U && strstr(canvas.body, "text"));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUISearchWord);
    toolbox_ui_reader(ui, "Wikipedia", "Ocean", "An ocean is a large body of salt water. "
        "Ocean currents carry heat around the planet and influence its climate. "
        "This final sentence remains reachable by scrolling.", false, ToolboxUISearchWikipedia);
    render(view, &canvas); assert(strstr(canvas.text, "Ocean") && !strstr(canvas.text, "encyclopedia"));
    for(unsigned i = 0U; i < 100U; ++i) { key(view, InputKeyDown, InputTypeRepeat); render(view, &canvas); }
    assert(strstr(canvas.text, "scrolling."));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUISearchWikipedia);
    toolbox_ui_reader(ui, "National Today", "",
        "[[B]]World Ocean Day[[/B]] celebrates the ocean. [[B]]A very long holiday title"
        " that wraps over multiple lines[[/B]] has readable bold text.", true, ToolboxUIRefreshNationalToday);
    render(view, &canvas);
    assert(canvas.reader_rows == 4U && canvas.bold_glyphs && canvas.reader_right <= 117U);
    assert(!strstr(canvas.text, "Today's highlights"));
    assert(!strstr(canvas.text, "[[B]]") && !strstr(canvas.text, "[[/B]]"));
    for(unsigned i = 0U; i < 100U; ++i) { key(view, InputKeyRight, InputTypeRepeat); render(view, &canvas); }
    assert(strstr(canvas.text, "text."));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIRefreshNationalToday);
    toolbox_ui_reader(ui, "National Today", "", "pre[[B]]fix[[/B]]es", true, ToolboxUIRefreshNationalToday);
    render(view, &canvas); assert(!strcmp(canvas.reader_body, "prefixes"));
    toolbox_ui_note(ui, "Updating..."); render(view, &canvas);
    assert(strstr(canvas.text, "Updating...") && !strcmp(canvas.reader_body, "prefixes"));
    char long_body[1600]; memset(long_body, 'W', sizeof(long_body) - 1U); long_body[sizeof(long_body) - 1U] = '\0';
    toolbox_ui_reader(ui, "National Today", "", long_body, true, ToolboxUIRefreshNationalToday);
    toolbox_ui_reader_shortened(ui); render(view, &canvas);
    for(unsigned i = 0U; i < 200U; ++i) { key(view, InputKeyDown, InputTypeRepeat); render(view, &canvas); }
    assert(strstr(canvas.reader_body, "[Text shortened]"));
    assert(!strstr(canvas.text, "[[/B]]"));
    toolbox_ui_reader(ui, "National Today", "", "[[B]][[/B]]", true, ToolboxUIRefreshNationalToday);
    render(view, &canvas); assert(canvas.reader_rows == 0U);
    assert(toolbox_ui_weather(ui, "London, GB", "Clear sky\nTemp: 18.2 C\nFeels: 17.5 C\n"
        "Humidity: 64%\nWind: 12.1 km/h\nHigh/Low: 20.4/12.2 C\nRain chance: 10%"));
    render(view, &canvas); assert(strstr(canvas.text, "London, GB") && strstr(canvas.text, "18.2 C"));
    assert(!strcmp(canvas.counter, "1/2"));
    key(view, InputKeyRight, InputTypeShort); render(view, &canvas);
    assert(!strcmp(canvas.counter, "2/2") && strstr(canvas.text, "Humidity"));
    toolbox_ui_note(ui, "Updating..."); render(view, &canvas);
    assert(strstr(canvas.text, "Updating..."));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIRefreshWeather);
    assert(!toolbox_ui_weather(ui, "Paris", "incomplete"));
    assert(toolbox_ui_iss(ui, "Latitude: 32.54 N\nLongitude: 120.21 W\nAltitude: 415.2 km\n"
        "Speed: 27600 km/h\nIn daylight"));
    render(view, &canvas); assert(strstr(canvas.text, "Latitude") && strstr(canvas.text, "In daylight"));
    key(view, InputKeyRight, InputTypeShort); render(view, &canvas);
    assert(strstr(canvas.text, "415.2 km") && strstr(canvas.text, "27600 km/h"));
    key(view, InputKeyOk, InputTypeShort); assert(actions.last == ToolboxUIRefreshISS);
    assert(!toolbox_ui_iss(ui, "incomplete"));
    assert(!key(view, InputKeyBack, InputTypeShort));
    toolbox_ui_free(ui);
    puts("toolbox_ui input / wrapping tests: PASS (host doubles, not hardware fonts)");
    return 0;
}
