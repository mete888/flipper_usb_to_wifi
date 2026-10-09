#include "toolbox_ui.h"
#include "config.h"
#include <furi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { ToolboxUICurrency, ToolboxUIEarthquakes, ToolboxUIDictionary,
               ToolboxUIReader, ToolboxUIWeather, ToolboxUIISS } ToolboxUIKind;
typedef struct {
    char title[24], subtitle[64], body[FIB_RESPONSE_PREVIEW_SIZE + 1U];
    bool rich;
    ToolboxUIAction action;
} ToolboxReader;
typedef struct {
    char location[64], condition[24];
    char temperature[16], feels[16], humidity[16], wind[16], high_low[32], rain[16];
} ToolboxWeather;
typedef struct {
    char latitude[24], longitude[24], altitude[24], velocity[24], light[24];
} ToolboxISS;
typedef struct {
    ToolboxUIKind kind;
    unsigned focus;
    unsigned page;
    unsigned scroll;
    unsigned scroll_limit;
    char note[32];
    union {
        ToolboxConverter converter;
        ToolboxEarthquakes earthquakes;
        ToolboxDictionary dictionary;
        ToolboxReader reader;
        ToolboxWeather weather;
        ToolboxISS iss;
    } content;
} ToolboxUIModel;

struct ToolboxUI {
    View* view;
    ToolboxUICallback callback;
    void* context;
};

static void fit(Canvas* canvas, int x, int y, unsigned width, const char* text) {
    char line[96];
    snprintf(line, sizeof(line), "%s", text);
    bool shortened = false;
    while(line[0] && canvas_string_width(canvas, line) > width) {
        line[strlen(line) - 1U] = '\0';
        shortened = true;
    }
    if(shortened && strlen(line) > 3U) memcpy(line + strlen(line) - 3U, "...", 3U);
    canvas_draw_str(canvas, x, y, line);
}

/* Pixel-measured wrapping, not a hard-coded character count. Newlines keep
 * examples separate. Clipped text can always be read with Up/Down. */
static unsigned wrapped(Canvas* canvas, const char* text, unsigned width,
                        unsigned skip, int x, int y, unsigned rows, unsigned spacing) {
    unsigned count = 0U;
    const char* p = text;
    while(*p) {
        char line[96];
        unsigned used = 0U, space = 0U;
        while(p[used] && p[used] != '\n' && used < sizeof(line) - 1U) {
            line[used] = p[used];
            line[used + 1U] = '\0';
            if(canvas_string_width(canvas, line) > width) break;
            if(p[used] == ' ') space = used;
            ++used;
        }
        if(!used && *p != '\n') used = 1U;
        else if(p[used] && p[used] != '\n' && space) used = space;
        memcpy(line, p, used); line[used] = '\0';
        if(count >= skip && count < skip + rows) {
            canvas_draw_str(canvas, x, y + (int)((count - skip) * spacing), line);
        }
        ++count;
        p += used;
        if(*p == '\n') ++p;
        else while(*p == ' ') ++p;
    }
    return count;
}

static void header(Canvas* canvas, const char* title, unsigned page, unsigned count) {
    char counter[12];
    canvas_set_font(canvas, FontSecondary);
    snprintf(counter, sizeof(counter), "%u/%u", page + 1U, count);
    /* Measure the counter: 10/10 needs more room than 1/2. */
    unsigned badge_width = canvas_string_width(canvas, counter) + 8U;
    int badge_x = 126 - (int)badge_width;
    canvas_draw_rframe(canvas, badge_x, 0, badge_width, 13U, 3U);
    canvas_draw_str_aligned(canvas, 121, 9, AlignRight, AlignBottom, counter);
    canvas_set_font(canvas, FontPrimary);
    fit(canvas, 3, 10, (unsigned)badge_x - 6U, title);
    canvas_set_font(canvas, FontSecondary);
}

static void scroll_indicator(Canvas* canvas, unsigned position, unsigned limit, int y, unsigned height) {
    if(!limit) return;
    // A quiet progress rail replaces the textual '+' and crowded key legend.
    canvas_draw_rframe(canvas, 120, y, 3U, height, 1U);
    canvas_draw_box(canvas, 120, y + 1 + (int)(position * (height - 6U) / limit), 3U, 4U);
}

/* Preserve inline bold spans while wrapping by measured glyph width. Each
 * rollback at a word boundary restores the formatting state as well. Unknown
 * controls/non-ASCII bytes are ignored, never shown as '?' or firmware escapes. */
static unsigned reader_lines(Canvas* canvas, const ToolboxReader* reader,
                             unsigned skip, bool paint) {
    const char* p = reader->body;
    bool bold = false;
    unsigned count = 0U;
    unsigned rows = reader->subtitle[0] ? 3U : 4U;
    int baseline = reader->subtitle[0] ? 36 : 24;
    while(*p) {
        char line[96];
        bool weights[96];
        unsigned n = 0U, width = 0U, space_index = 0U;
        const char* after_space = NULL;
        bool space_bold = false;
        while(*p && *p != '\n' && n < sizeof(line) - 1U) {
            if(reader->rich && !strncmp(p, "[[B]]", 5U)) { bold = true; p += 5U; continue; }
            if(reader->rich && !strncmp(p, "[[/B]]", 6U)) {
                bold = false; p += 6U;
                continue;
            }
            unsigned char c = (unsigned char)*p;
            if(c < 32U || c > 126U) { ++p; continue; }
            canvas_set_font(canvas, bold ? FontPrimary : FontSecondary);
            /* String width is the painted bounding box, not the pen advance.
             * Summing single-letter string widths underestimates whole words
             * on real u8g2 fonts. Measure and paint with the same glyph advance. */
            unsigned advance = canvas_glyph_width(canvas, c);
            if(n && width + advance > 114U) break;
            line[n] = (char)c; weights[n] = bold;
            if(c == ' ') { space_index = n; after_space = p + 1; space_bold = bold; }
            ++n; width += advance; ++p;
        }
        if(*p && *p != '\n' && after_space) {
            n = space_index; p = after_space; bold = space_bold;
            while(*p == ' ') ++p;
        } else if(*p == '\n') ++p;
        line[n] = '\0';
        if(paint && count >= skip && count < skip + rows) {
            int x = 3;
            for(unsigned at = 0U; at < n; ++at) {
                char glyph[2] = {line[at], '\0'};
                canvas_set_font(canvas, weights[at] ? FontPrimary : FontSecondary);
                canvas_draw_str(canvas, x, baseline + (int)((count - skip) * 11U), glyph);
                x += canvas_glyph_width(canvas, (uint8_t)line[at]);
            }
        }
        ++count;
    }
    canvas_set_font(canvas, FontSecondary);
    return count;
}

static void reader(Canvas* canvas, ToolboxUIModel* model) {
    const ToolboxReader* text = &model->content.reader;
    unsigned lines = reader_lines(canvas, text, 0U, false);
    unsigned rows = text->subtitle[0] ? 3U : 4U;
    model->scroll_limit = lines > rows ? lines - rows : 0U;
    if(model->scroll > model->scroll_limit) model->scroll = model->scroll_limit;
    header(canvas, !text->subtitle[0] && model->note[0] ? model->note : text->title,
           model->scroll, model->scroll_limit + 1U);
    if(text->subtitle[0]) {
        canvas_set_font(canvas, FontPrimary);
        fit(canvas, 3, 24, 122U, model->note[0] ? model->note : text->subtitle);
    }
    reader_lines(canvas, text, model->scroll, true);
    scroll_indicator(canvas, model->scroll, model->scroll_limit,
                     text->subtitle[0] ? 31 : 18, text->subtitle[0] ? 29U : 42U);
}

static void weather(Canvas* canvas, const ToolboxUIModel* model) {
    const ToolboxWeather* data = &model->content.weather;
    header(canvas, model->page && model->note[0] ? model->note : "Weather", model->page, 2U);
    if(!model->page) {
        canvas_set_font(canvas, FontPrimary);
        fit(canvas, 3, 24, 122U, data->location);
        canvas_draw_rframe(canvas, 2, 29, 124U, 25U, 3U);
        char temperature[24]; snprintf(temperature, sizeof(temperature), "%s C", data->temperature);
        fit(canvas, 8, 41, 95U, temperature);
        canvas_set_font(canvas, FontSecondary);
        fit(canvas, 8, 51, 95U, data->condition);
        // Draw the actual condition, not a permanent sun on rainy forecasts.
        if(strstr(data->condition, "clear") || strstr(data->condition, "Clear")) {
            canvas_draw_circle(canvas, 112, 39, 4U);
            canvas_draw_line(canvas, 112, 32, 112, 33);
            canvas_draw_line(canvas, 105, 39, 106, 39);
            canvas_draw_line(canvas, 118, 39, 119, 39);
            canvas_draw_line(canvas, 112, 45, 112, 46);
        } else {
            canvas_draw_rframe(canvas, 104, 36, 17U, 8U, 3U);
            canvas_draw_circle(canvas, 111, 36, 4U);
            if(strstr(data->condition, "Rain") || strstr(data->condition, "rain") ||
               strstr(data->condition, "Drizzle") || strstr(data->condition, "drizzle")) {
                canvas_draw_line(canvas, 108, 46, 107, 48);
                canvas_draw_line(canvas, 114, 46, 113, 48);
                canvas_draw_line(canvas, 120, 46, 119, 48);
            }
        }
        char range[48]; snprintf(range, sizeof(range), "High / Low  %s C", data->high_low);
        fit(canvas, 3, 63, 122U, model->note[0] ? model->note : range);
    } else {
        const char* labels[] = {"Feels like", "Humidity", "Wind", "Rain chance"};
        char feels[24], humidity[24], wind[24], rain[24];
        snprintf(feels, sizeof(feels), "%s C", data->feels);
        snprintf(humidity, sizeof(humidity), "%s%%", data->humidity);
        snprintf(wind, sizeof(wind), "%s km/h", data->wind);
        snprintf(rain, sizeof(rain), "%s%%", data->rain);
        const char* values[] = {feels, humidity, wind, rain};
        for(unsigned i = 0U; i < 4U; ++i) {
            int x = (i & 1U) ? 68 : 2;
            int y = i < 2U ? 16 : 40;
            canvas_draw_rframe(canvas, x, y, 58U, 22U, 3U);
            canvas_set_font(canvas, FontSecondary); fit(canvas, x + 5, y + 9, 48U, labels[i]);
            canvas_set_font(canvas, FontPrimary);
            if(canvas_string_width(canvas, values[i]) > 48U) canvas_set_font(canvas, FontSecondary);
            fit(canvas, x + 5, y + 19, 48U, values[i]);
        }
    }
}

static void iss(Canvas* canvas, const ToolboxUIModel* model) {
    const ToolboxISS* data = &model->content.iss;
    header(canvas, "ISS Right Now", model->page, 2U);
    if(!model->page) {
        const char* labels[] = {"Latitude", "Longitude"};
        const char* values[] = {data->latitude, data->longitude};
        for(unsigned i = 0U; i < 2U; ++i) {
            int x = i ? 66 : 2;
            canvas_draw_rframe(canvas, x, 17, 60U, 29U, 3U);
            canvas_set_font(canvas, FontSecondary); fit(canvas, x + 5, 27, 50U, labels[i]);
            canvas_set_font(canvas, FontPrimary);
            if(canvas_string_width(canvas, values[i]) > 50U) canvas_set_font(canvas, FontSecondary);
            fit(canvas, x + 5, 40, 50U, values[i]);
        }
        // A tiny satellite silhouette, keeping the telemetry the focus.
        canvas_draw_rframe(canvas, 10, 52, 8U, 7U, 1U);
        canvas_draw_rframe(canvas, 3, 53, 5U, 5U, 1U);
        canvas_draw_rframe(canvas, 20, 53, 5U, 5U, 1U);
        canvas_draw_line(canvas, 8, 55, 10, 55);
        canvas_draw_line(canvas, 18, 55, 20, 55);
        canvas_set_font(canvas, FontSecondary);
        fit(canvas, 31, 59, 94U, model->note[0] ? model->note : data->light);
    } else {
        const char* labels[] = {"Altitude", "Speed"};
        const char* values[] = {data->altitude, data->velocity};
        for(unsigned i = 0U; i < 2U; ++i) {
            int y = i ? 38 : 15;
            canvas_draw_rframe(canvas, 2, y, 124U, 20U, 3U);
            canvas_set_font(canvas, FontSecondary); fit(canvas, 7, y + 13, 43U, labels[i]);
            canvas_set_font(canvas, FontPrimary);
            if(canvas_string_width(canvas, values[i]) > 66U) canvas_set_font(canvas, FontSecondary);
            char value[32];
            snprintf(value, sizeof(value), "%s", values[i]);
            while(canvas_string_width(canvas, value) > 66U && value[0]) value[strlen(value) - 1U] = '\0';
            canvas_draw_str_aligned(canvas, 120, y + 13, AlignRight, AlignBottom, value);
        }
        canvas_set_font(canvas, FontSecondary);
        fit(canvas, 3, 63, 122U, model->note[0] ? model->note : "WhereTheISS.at");
    }
}

static void currency(Canvas* canvas, const ToolboxUIModel* model) {
    const ToolboxConverter* state = &model->content.converter;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 3, 9, "Currency");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 125, 9, AlignRight, AlignBottom, "OK: Edit");
    for(unsigned side = 0U; side < 2U; ++side) {
        int x = side ? 68 : 2;
        for(unsigned row = 0U; row < 2U; ++row) {
            int y = row ? 34 : 14;
            canvas_draw_rframe(canvas, x, y, 58U, 17U, 3U);
            if(model->focus == row * 2U + side) canvas_draw_rframe(canvas, x + 1, y + 1, 56U, 15U, 2U);
            canvas_set_font(canvas, row ? FontSecondary : FontPrimary);
            fit(canvas, x + 5, y + 12, 48U,
                row ? state->amount[side] : toolbox_currency_code(state->currency[side]));
        }
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 61, 26, ">");
    canvas_draw_str(canvas, 61, 46, "=");
    unsigned side = model->focus & 1U;
    if(model->focus >= 2U && canvas_string_width(canvas, state->amount[side]) > 48U) {
        char full[40];
        snprintf(full, sizeof(full), "%s %s", state->amount[side], toolbox_currency_code(state->currency[side]));
        fit(canvas, 3, 62, 122U, full);
    } else if(model->note[0]) fit(canvas, 3, 62, 122U, model->note);
    else if(state->date[0]) {
        char date[32]; snprintf(date, sizeof(date), "Ref. rate %s", state->date);
        fit(canvas, 3, 62, 122U, date);
    } else canvas_draw_str(canvas, 3, 62, "Arrows: select a field");
}

static void draw(Canvas* canvas, void* context) {
    ToolboxUIModel* model = context;
    canvas_clear(canvas);
    if(model->kind == ToolboxUICurrency) { currency(canvas, model); return; }
    if(model->kind == ToolboxUIReader) { reader(canvas, model); return; }
    if(model->kind == ToolboxUIWeather) { weather(canvas, model); return; }
    if(model->kind == ToolboxUIISS) { iss(canvas, model); return; }
    if(model->kind == ToolboxUIEarthquakes) {
        const ToolboxEarthquakes* events = &model->content.earthquakes;
        header(canvas, "Earthquakes", model->page, events->count ? events->count : 1U);
        canvas_set_font(canvas, FontSecondary);
        if(events->count) {
            const ToolboxEarthquake* event = &events->events[model->page];
            /* Like the converter, use padded fields rather than a separate
             * MAG label touching the border. Location remains full width. */
            canvas_draw_rframe(canvas, 2, 14, 49U, 15U, 3U);
            canvas_draw_rframe(canvas, 57, 14, 69U, 15U, 3U);
            char magnitude[16]; snprintf(magnitude, sizeof(magnitude), "M %s", event->magnitude);
            canvas_set_font(canvas, FontPrimary);
            fit(canvas, 8, 25, 37U, magnitude);
            canvas_set_font(canvas, FontSecondary);
            char depth[40]; snprintf(depth, sizeof(depth), "Depth %s", event->depth);
            fit(canvas, 63, 25, 57U, depth);
            canvas_draw_rframe(canvas, 2, 32, 124U, 22U, 3U);
            unsigned lines = wrapped(canvas, event->place, 111U, 0U, 0, 0, 0U, 10U);
            model->scroll_limit = lines > 2U ? lines - 2U : 0U;
            if(model->scroll > model->scroll_limit) model->scroll = model->scroll_limit;
            wrapped(canvas, event->place, 111U, model->scroll, 7, 41, 2U, 10U);
            scroll_indicator(canvas, model->scroll, model->scroll_limit, 36, 15U);
            fit(canvas, 3, 63, 93U, model->note[0] ? model->note : event->time);
        } else canvas_draw_str(canvas, 3, 32, "No recent earthquakes.");
        canvas_draw_str_aligned(canvas, 125, 63, AlignRight, AlignBottom, "USGS");
    } else {
        const ToolboxDictionary* word = &model->content.dictionary;
        header(canvas, word->word, model->page, word->count + 1U);
        bool credits = model->page == word->count;
        const char* part = credits ? "Source / License" : word->meanings[model->page].part;
        /* A single padded reader panel groups the part of speech and body;
         * avoid stacking several tiny framed rows around the definition. */
        canvas_draw_rframe(canvas, 2, 15, 124U, 47U, 3U);
        fit(canvas, 7, 25, 110U, part);
        const char* body = credits ? word->credits : word->meanings[model->page].text;
        unsigned lines = wrapped(canvas, body, 108U, 0U, 0, 0, 0U, 11U);
        model->scroll_limit = lines > 3U ? lines - 3U : 0U;
        if(model->scroll > model->scroll_limit) model->scroll = model->scroll_limit;
        wrapped(canvas, body, 108U, model->scroll, 7, 35, 3U, 11U);
        scroll_indicator(canvas, model->scroll, model->scroll_limit, 31, 27U);
    }
}

static bool input(InputEvent* event, void* context) {
    ToolboxUI* ui = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    ToolboxUIAction action = 0;
    with_view_model(ui->view, ToolboxUIModel* model, {
        if(model->kind == ToolboxUICurrency) {
            if(event->key == InputKeyLeft) model->focus &= ~1U;
            else if(event->key == InputKeyRight) model->focus |= 1U;
            else if(event->key == InputKeyUp) model->focus &= 1U;
            else if(event->key == InputKeyDown) model->focus |= 2U;
            else if(event->key == InputKeyOk && event->type == InputTypeShort)
                action = (ToolboxUIAction)(ToolboxUIEditLeftCurrency + model->focus);
        } else if(model->kind == ToolboxUIReader) {
            if(event->key == InputKeyUp && model->scroll) --model->scroll;
            else if(event->key == InputKeyDown && model->scroll < model->scroll_limit) ++model->scroll;
            else if(event->key == InputKeyLeft) model->scroll = model->scroll > 3U ? model->scroll - 3U : 0U;
            else if(event->key == InputKeyRight) {
                unsigned next = model->scroll + 3U;
                model->scroll = next < model->scroll_limit ? next : model->scroll_limit;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort)
                action = model->content.reader.action;
        } else if(model->kind == ToolboxUIWeather || model->kind == ToolboxUIISS) {
            if(event->key == InputKeyLeft || event->key == InputKeyUp) model->page = 0U;
            else if(event->key == InputKeyRight || event->key == InputKeyDown) model->page = 1U;
            else if(event->key == InputKeyOk && event->type == InputTypeShort)
                action = model->kind == ToolboxUIWeather ? ToolboxUIRefreshWeather : ToolboxUIRefreshISS;
        } else {
            unsigned count = model->kind == ToolboxUIEarthquakes ? model->content.earthquakes.count : model->content.dictionary.count + 1U;
            if(event->key == InputKeyLeft && model->page) { --model->page; model->scroll = 0U; }
            else if(event->key == InputKeyRight && model->page + 1U < count) { ++model->page; model->scroll = 0U; }
            else if(event->key == InputKeyUp && model->scroll) --model->scroll;
            else if(event->key == InputKeyDown) {
                if(model->scroll < model->scroll_limit) ++model->scroll;
            } else if(event->key == InputKeyOk && event->type == InputTypeShort)
                action = model->kind == ToolboxUIEarthquakes ? ToolboxUIRefreshEarthquakes : ToolboxUISearchWord;
        }
    }, true);
    // Queue the action after releasing the model lock. Never change a view
    // or cancel USB/network work from the GUI thread while holding that lock.
    if(action && ui->callback) ui->callback(ui->context, action);
    return true;
}

ToolboxUI* toolbox_ui_alloc(ToolboxUICallback callback, void* context) {
    ToolboxUI* ui = malloc(sizeof(*ui));
    if(!ui) return NULL;
    ui->view = view_alloc();
    ui->callback = callback; ui->context = context;
    view_allocate_model(ui->view, ViewModelTypeLocking, sizeof(ToolboxUIModel));
    with_view_model(ui->view, ToolboxUIModel* model, {
        memset(model, 0, sizeof(*model));
    }, false);
    view_set_context(ui->view, ui);
    view_set_draw_callback(ui->view, draw);
    view_set_input_callback(ui->view, input);
    return ui;
}
void toolbox_ui_free(ToolboxUI* ui) { if(ui) { view_free(ui->view); free(ui); } }
View* toolbox_ui_view(ToolboxUI* ui) { return ui->view; }
void toolbox_ui_currency(ToolboxUI* ui, const ToolboxConverter* state, const char* note) {
    with_view_model(ui->view, ToolboxUIModel* model, {
        if(model->kind != ToolboxUICurrency) model->focus = 0U;
        model->kind = ToolboxUICurrency;
        model->content.converter = *state;
        snprintf(model->note, sizeof(model->note), "%s", note ? note : "");
    }, true);
}
bool toolbox_ui_earthquakes(ToolboxUI* ui, const char* response) {
    bool valid = false;
    with_view_model(ui->view, ToolboxUIModel* model, {
        model->kind = ToolboxUIEarthquakes;
        model->page = model->scroll = 0U;
        model->note[0] = '\0';
        valid = toolbox_parse_earthquakes(response, &model->content.earthquakes);
    }, true);
    return valid;
}
bool toolbox_ui_dictionary(ToolboxUI* ui, const char* response) {
    bool valid = false;
    with_view_model(ui->view, ToolboxUIModel* model, {
        model->kind = ToolboxUIDictionary;
        model->page = model->scroll = 0U;
        model->note[0] = '\0';
        valid = toolbox_parse_dictionary(response, &model->content.dictionary);
    }, true);
    return valid;
}
void toolbox_ui_note(ToolboxUI* ui, const char* note) {
    with_view_model(ui->view, ToolboxUIModel* model, {
        snprintf(model->note, sizeof(model->note), "%s", note ? note : "");
    }, true);
}

void toolbox_ui_reader(ToolboxUI* ui, const char* title, const char* subtitle,
                       const char* body, bool rich, ToolboxUIAction action) {
    with_view_model(ui->view, ToolboxUIModel* model, {
        model->kind = ToolboxUIReader; model->page = model->scroll = model->scroll_limit = 0U;
        model->note[0] = '\0';
        ToolboxReader* data = &model->content.reader;
        snprintf(data->title, sizeof(data->title), "%s", title ? title : "");
        snprintf(data->subtitle, sizeof(data->subtitle), "%s", subtitle ? subtitle : "");
        snprintf(data->body, sizeof(data->body), "%s", body ? body : "");
        data->rich = rich; data->action = action;
    }, true);
}

void toolbox_ui_reader_shortened(ToolboxUI* ui) {
    with_view_model(ui->view, ToolboxUIModel* model, {
        if(model->kind == ToolboxUIReader) {
            ToolboxReader* data = &model->content.reader;
            const char* suffix = data->rich ? "[[/B]]\n[Text shortened]" : "\n[Text shortened]";
            size_t suffix_length = strlen(suffix);
            size_t length = strlen(data->body);
            size_t limit = sizeof(data->body) - 1U - suffix_length;
            if(length > limit) length = limit;
            /* Avoid exposing half a formatting marker at the preview boundary. */
            if(data->rich) {
                for(size_t at = length > 5U ? length - 5U : 0U; at < length; ++at) {
                    if(data->body[at] == '[' && at + 1U < length && data->body[at + 1U] == '[') {
                        length = at;
                        break;
                    }
                }
            }
            memcpy(data->body + length, suffix, suffix_length + 1U);
        }
    }, true);
}

static bool line_value(const char* text, const char* prefix, char* out, size_t size) {
    for(const char* p = text; *p;) {
        const char* end = strchr(p, '\n');
        if(!end) end = p + strlen(p);
        size_t prefix_size = strlen(prefix);
        if((size_t)(end - p) >= prefix_size && !strncmp(p, prefix, prefix_size)) {
            size_t length = (size_t)(end - p) - prefix_size;
            if(!length || length >= size) return false;
            memcpy(out, p + prefix_size, length); out[length] = '\0'; return true;
        }
        p = *end ? end + 1 : end;
    }
    return false;
}

static void remove_unit(char* text, const char* unit) {
    size_t length = strlen(text), suffix = strlen(unit);
    if(length >= suffix && !strcmp(text + length - suffix, unit)) text[length - suffix] = '\0';
}

bool toolbox_ui_weather(ToolboxUI* ui, const char* location, const char* formatted) {
    ToolboxWeather data = {0};
    if(!formatted || !line_value(formatted, "", data.condition, sizeof(data.condition)) ||
       !line_value(formatted, "Temp: ", data.temperature, sizeof(data.temperature)) ||
       !line_value(formatted, "Feels: ", data.feels, sizeof(data.feels)) ||
       !line_value(formatted, "Humidity: ", data.humidity, sizeof(data.humidity)) ||
       !line_value(formatted, "Wind: ", data.wind, sizeof(data.wind)) ||
       !line_value(formatted, "High/Low: ", data.high_low, sizeof(data.high_low)) ||
       !line_value(formatted, "Rain chance: ", data.rain, sizeof(data.rain))) return false;
    remove_unit(data.temperature, " C"); remove_unit(data.feels, " C");
    remove_unit(data.humidity, "%"); remove_unit(data.wind, " km/h");
    remove_unit(data.high_low, " C"); remove_unit(data.rain, "%");
    snprintf(data.location, sizeof(data.location), "%s", location ? location : "Current weather");
    with_view_model(ui->view, ToolboxUIModel* model, {
        model->kind = ToolboxUIWeather; model->page = model->scroll = 0U; model->note[0] = '\0';
        model->content.weather = data;
    }, true);
    return true;
}

bool toolbox_ui_iss(ToolboxUI* ui, const char* formatted) {
    ToolboxISS data = {0};
    if(!formatted || !line_value(formatted, "Latitude: ", data.latitude, sizeof(data.latitude)) ||
       !line_value(formatted, "Longitude: ", data.longitude, sizeof(data.longitude)) ||
       !line_value(formatted, "Altitude: ", data.altitude, sizeof(data.altitude)) ||
       !line_value(formatted, "Speed: ", data.velocity, sizeof(data.velocity))) return false;
    snprintf(data.light, sizeof(data.light), "%s", strstr(formatted, "In daylight") ? "In daylight" : "In Earth's shadow");
    with_view_model(ui->view, ToolboxUIModel* model, {
        model->kind = ToolboxUIISS; model->page = model->scroll = 0U; model->note[0] = '\0';
        model->content.iss = data;
    }, true);
    return true;
}
