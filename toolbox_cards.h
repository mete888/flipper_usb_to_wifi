#pragma once
#include "toolbox_tools.h"
#include <stdint.h>

typedef struct {
    char magnitude[12];
    char place[91];
    char time[24];
    char depth[24];
} ToolboxEarthquake;
typedef struct {
    ToolboxEarthquake events[TOOLBOX_EARTHQUAKE_COUNT];
    unsigned count;
} ToolboxEarthquakes;
typedef struct {
    char part[25];
    char text[321];
} ToolboxMeaning;
typedef struct {
    char word[49];
    ToolboxMeaning meanings[3];
    char credits[321];
    unsigned count;
} ToolboxDictionary;
typedef struct {
    unsigned currency[2];
    unsigned edited_side;
    char amount[2][32];
    double rate;
    char date[11];
    bool rate_valid;
} ToolboxConverter;

bool toolbox_parse_earthquakes(const char* text, ToolboxEarthquakes* out);
bool toolbox_parse_dictionary(const char* text, ToolboxDictionary* out);
void toolbox_converter_init(ToolboxConverter* state);
bool toolbox_converter_choose(ToolboxConverter* state, unsigned side, unsigned currency);
bool toolbox_converter_edit(ToolboxConverter* state, unsigned side, const char* amount);
bool toolbox_converter_rate(ToolboxConverter* state, const char* response);
