#include "toolbox_cards.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool copy_field(char* out, size_t capacity, const char* begin, const char* end) {
    if(!end || end < begin || (size_t)(end - begin) >= capacity) return false;
    memcpy(out, begin, (size_t)(end - begin));
    out[end - begin] = '\0';
    return true;
}

static const char* payload(const char* text) {
    if(!text || strlen(text) > 1400U || strncmp(text, TOOLBOX_TEXT_MAGIC, 10U)) return NULL;
    for(const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        if(*p != '\n' && (*p < 32U || *p > 126U)) return NULL;
    }
    return text + 10U;
}

bool toolbox_parse_earthquakes(const char* text, ToolboxEarthquakes* out) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    const char* p = payload(text);
    if(!p) return false;
    if(strcmp(p, "No recent earthquakes reported.\n\nSource: USGS") == 0) return true;
    while(strncmp(p, "M ", 2U) == 0 && out->count < TOOLBOX_EARTHQUAKE_COUNT) {
        ToolboxEarthquake* event = &out->events[out->count];
        const char* line = strchr(p, '\n');
        const char* separator = strstr(p, " - ");
        if(!line || !separator || separator >= line ||
           !copy_field(event->magnitude, sizeof(event->magnitude), p + 2U, separator) ||
           !copy_field(event->place, sizeof(event->place), separator + 3U, line)) return false;
        p = line + 1U;
        line = strchr(p, '\n');
        if(!copy_field(event->time, sizeof(event->time), p, line)) return false;
        p = line + 1U;
        if(strncmp(p, "Depth: ", 7U)) return false;
        line = strstr(p, "\n\n");
        if(!copy_field(event->depth, sizeof(event->depth), p + 7U, line)) return false;
        ++out->count;
        p = line + 2U;
    }
    return out->count > 0U && strcmp(p, "Source: USGS") == 0;
}

bool toolbox_parse_dictionary(const char* text, ToolboxDictionary* out) {
    if(!out) return false;
    memset(out, 0, sizeof(*out));
    const char* p = payload(text);
    if(!p) return false;
    const char* end = strstr(p, "\n\n");
    if(!copy_field(out->word, sizeof(out->word), p, end) || !out->word[0]) return false;
    p = end + 2U;
    while(strncmp(p, "Source: ", 8U) && out->count < 3U) {
        end = strstr(p, "\n\n");
        const char* separator = strstr(p, ": ");
        if(!end || !separator || separator >= end) return false;
        ToolboxMeaning* meaning = &out->meanings[out->count];
        if(!copy_field(meaning->part, sizeof(meaning->part), p, separator) ||
           !copy_field(meaning->text, sizeof(meaning->text), separator + 2U, end)) return false;
        ++out->count;
        p = end + 2U;
    }
    const char* notice = "Shortened ASCII text";
    const size_t length = strlen(p);
    if(length < strlen(notice) || strcmp(p + length - strlen(notice), notice)) return false;
    return out->count > 0U && strncmp(p, "Source: ", 8U) == 0 &&
           copy_field(out->credits, sizeof(out->credits), p, p + strlen(p));
}

static bool recalculate(ToolboxConverter* state) {
    unsigned other = 1U - state->edited_side;
    snprintf(state->amount[other], sizeof(state->amount[other]), "--");
    if(!state->rate_valid) return true;
    const double amount = strtod(state->amount[state->edited_side], NULL);
    const double result = state->edited_side == 0U ? amount * state->rate : amount / state->rate;
    if(!isfinite(result) || result > (double)100000000000000ULL) return false;
    snprintf(state->amount[other], sizeof(state->amount[other]), "%.2f", result);
    return true;
}

void toolbox_converter_init(ToolboxConverter* state) {
    memset(state, 0, sizeof(*state));
    state->currency[1] = 1U;
    snprintf(state->amount[0], sizeof(state->amount[0]), "1.00");
    recalculate(state);
}

bool toolbox_converter_choose(ToolboxConverter* state, unsigned side, unsigned currency) {
    if(!state || side > 1U || currency >= TOOLBOX_CURRENCY_COUNT) return false;
    state->currency[side] = currency;
    state->rate_valid = state->currency[0] == state->currency[1];
    state->rate = state->rate_valid ? (double)1 : (double)0;
    state->date[0] = '\0';
    return recalculate(state);
}

bool toolbox_converter_edit(ToolboxConverter* state, unsigned side, const char* amount) {
    if(!state || side > 1U || !toolbox_valid_amount(amount)) return false;
    state->edited_side = side;
    snprintf(state->amount[side], sizeof(state->amount[side]), "%s", amount);
    return recalculate(state);
}

bool toolbox_converter_rate(ToolboxConverter* state, const char* response) {
    if(!state) return false;
    char validated[256];
    /* Share the existing strict wire validator; never consume an unverified
     * rate or one belonging to a different pair. */
    if(!toolbox_format_conversion(response, "1", state->currency[0], state->currency[1],
           validated, sizeof(validated))) return false;
    const char* value = response + 17U;
    const char* end = strchr(value, '\t');
    double rate = strtod(value, NULL);
    if(!isfinite(rate) || rate <= (double)0 || rate > (double)1000000000) return false;
    state->rate = rate;
    memcpy(state->date, end + 1U, 10U);
    state->date[10] = '\0';
    state->rate_valid = true;
    return recalculate(state);
}
