#pragma once
#include <stdbool.h>
#include <stddef.h>

#define TOOLBOX_CURRENCY_COUNT 21U
#define TOOLBOX_EARTHQUAKE_COUNT 10U
#define TOOLBOX_WORD_MAX 48U
#define TOOLBOX_AMOUNT_SIZE 13U
#define TOOLBOX_EARTHQUAKES_URL \
    "https://earthquake.usgs.gov/fdsnws/event/1/query?format=geojson&orderby=time&limit=10&eventtype=earthquake"
#define TOOLBOX_TEXT_MAGIC "FIBTOOLS1\n"

const char* toolbox_currency_code(unsigned index);
const char* toolbox_currency_label(unsigned index);
bool toolbox_valid_amount(const char* amount);
bool toolbox_dictionary_url(const char* word, char* url, size_t capacity);
bool toolbox_currency_url(unsigned from, unsigned to, char* url, size_t capacity);
bool toolbox_format_conversion(
    const char* response, const char* amount, unsigned from, unsigned to,
    char* output, size_t capacity);
