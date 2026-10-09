#include "toolbox_cards.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char* earthquakes =
    "FIBTOOLS1\nM 4.1 - Offshore Fiji\n10-04 07:30 UTC\nDepth: 12.7 km\n\n"
    "M 2.5 - Near Mugla\n10-04 07:20 UTC\nDepth: 4.0 km\n\nSource: USGS";
static const char* dictionary =
    "FIBTOOLS1\nhello\n\nnoun: A greeting.\nExample: Hello, friend!\n\n"
    "verb: To greet someone.\n\nSource: https://en.wiktionary.org/wiki/hello\n"
    "CC BY-SA 3.0\nhttps://creativecommons.org/licenses/by-sa/3.0\nShortened ASCII text";

static void test_cards(void) {
    ToolboxEarthquakes eq;
    ToolboxDictionary word;
    assert(toolbox_parse_earthquakes(earthquakes, &eq));
    assert(eq.count == 2U && !strcmp(eq.events[0].magnitude, "4.1"));
    assert(!strcmp(eq.events[1].place, "Near Mugla"));
    assert(!strcmp(eq.events[0].time, "10-04 07:30 UTC"));
    assert(!strcmp(eq.events[0].depth, "12.7 km"));
    assert(toolbox_parse_earthquakes("FIBTOOLS1\nNo recent earthquakes reported.\n\nSource: USGS", &eq));
    assert(eq.count == 0U);
    assert(toolbox_parse_dictionary(dictionary, &word));
    assert(word.count == 2U && !strcmp(word.word, "hello"));
    assert(!strcmp(word.meanings[0].part, "noun"));
    assert(strstr(word.meanings[0].text, "Example: Hello, friend!"));
    assert(strstr(word.credits, "CC BY-SA 3.0"));
    char partial[512];
    for(size_t n = 0U; n < strlen(earthquakes); ++n) {
        memcpy(partial, earthquakes, n); partial[n] = '\0';
        assert(!toolbox_parse_earthquakes(partial, &eq));
    }
    for(size_t n = 0U; n < strlen(dictionary); ++n) {
        memcpy(partial, dictionary, n); partial[n] = '\0';
        assert(!toolbox_parse_dictionary(partial, &word));
    }
    assert(!toolbox_parse_dictionary("FIBTOOLS1\nhello\n\nnoun: \033#bad\n\nSource: x", &word));
    assert(!toolbox_parse_dictionary("FIBTOOLS1\nhello\n\nnoun: truncated", &word));
    assert(!toolbox_parse_earthquakes("FIBTOOLS1\nM 1 - Place\nDate\nDepth: 2 km", &eq));
}

static void test_two_way_converter(void) {
    ToolboxConverter state;
    toolbox_converter_init(&state);
    assert(state.currency[0] == 0U && state.currency[1] == 1U);
    assert(!state.rate_valid && !strcmp(state.amount[1], "--"));
    assert(toolbox_converter_choose(&state, 1U, 3U));
    assert(toolbox_converter_edit(&state, 0U, "100"));
    assert(toolbox_converter_rate(&state, "FIBRATE1\nUSD\tTRY\t50.000000000000\t2026-10-04\n"));
    assert(!strcmp(state.amount[1], "5000.00"));
    assert(toolbox_converter_edit(&state, 1U, "2500"));
    assert(!strcmp(state.amount[0], "50.00"));
    assert(state.edited_side == 1U);
    assert(toolbox_converter_choose(&state, 0U, 1U));
    assert(!state.rate_valid && !strcmp(state.amount[1], "2500") && !strcmp(state.amount[0], "--"));
    assert(!toolbox_converter_rate(&state, "FIBRATE1\nUSD\tTRY\t50\t2026-10-04\n"));
    assert(toolbox_converter_rate(&state, "FIBRATE1\nEUR\tTRY\t40\t2026-10-04\n"));
    assert(!strcmp(state.amount[0], "62.50"));
    assert(toolbox_converter_choose(&state, 0U, 3U));
    assert(state.rate_valid && !strcmp(state.amount[0], "2500.00"));
    assert(toolbox_converter_edit(&state, 0U, "0"));
    assert(!strcmp(state.amount[1], "0.00"));
    assert(!toolbox_converter_edit(&state, 1U, "-1"));
    assert(!toolbox_converter_edit(&state, 2U, "1"));
    assert(!toolbox_converter_choose(&state, 2U, 1U));
    assert(!toolbox_converter_choose(&state, 1U, 999U));
}

static void test_ten_earthquake_cards(void) {
    char text[2048] = "FIBTOOLS1\n";
    char place[91]; memset(place, 'x', sizeof(place) - 1U); place[90] = '\0';
    size_t used = strlen(text);
    for(unsigned n = 0U; n < TOOLBOX_EARTHQUAKE_COUNT; ++n) {
        int size = snprintf(text + used, sizeof(text) - used,
            "M 4.1 - %s\n10-04 07:30 UTC\nDepth: 12.7 km\n\n", place);
        assert(size > 0 && (size_t)size < sizeof(text) - used); used += (size_t)size;
    }
    strcat(text, "Source: USGS");
    assert(strlen(text) <= 1400U);
    ToolboxEarthquakes cards;
    assert(toolbox_parse_earthquakes(text, &cards) && cards.count == 10U);
    assert(!strcmp(cards.events[9].place, place));
    char partial[2048];
    for(size_t n = 0U; n < strlen(text); ++n) {
        memcpy(partial, text, n); partial[n] = '\0';
        assert(!toolbox_parse_earthquakes(partial, &cards));
    }
    snprintf(text, sizeof(text), "FIBTOOLS1\n");
    for(unsigned n = 0U; n < 11U; ++n)
        strcat(text, "M 1.0 - Place\n10-04 07:30 UTC\nDepth: 1.0 km\n\n");
    strcat(text, "Source: USGS");
    assert(strlen(text) < 1400U);
    assert(!toolbox_parse_earthquakes(text, &cards));
}

int main(void) {
    test_cards(); test_two_way_converter(); test_ten_earthquake_cards();
    puts("toolbox_cards tests: PASS");
    return 0;
}
