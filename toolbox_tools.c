#include "toolbox_tools.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const codes[] = {
    "USD", "EUR", "GBP", "TRY", "JPY", "CHF", "CAD", "AUD",
    "CNY", "INR", "BRL", "MXN", "SEK", "NOK", "PLN", "KRW",
    "HKD", "SGD", "NZD", "DKK", "ZAR"};
static const char* const labels[] = {
    "USD - US Dollar", "EUR - Euro", "GBP - British Pound", "TRY - Turkish Lira",
    "JPY - Japanese Yen", "CHF - Swiss Franc", "CAD - Canadian Dollar",
    "AUD - Australian Dollar", "CNY - Chinese Yuan", "INR - Indian Rupee",
    "BRL - Brazilian Real", "MXN - Mexican Peso", "SEK - Swedish Krona",
    "NOK - Norwegian Krone", "PLN - Polish Zloty", "KRW - Korean Won",
    "HKD - Hong Kong Dollar", "SGD - Singapore Dollar", "NZD - New Zealand Dollar",
    "DKK - Danish Krone", "ZAR - South African Rand"};

_Static_assert(sizeof(codes) / sizeof(codes[0]) == TOOLBOX_CURRENCY_COUNT, "Currency count mismatch");
_Static_assert(sizeof(labels) / sizeof(labels[0]) == TOOLBOX_CURRENCY_COUNT, "Currency label mismatch");

const char* toolbox_currency_code(unsigned index) {
    return index < TOOLBOX_CURRENCY_COUNT ? codes[index] : NULL;
}
const char* toolbox_currency_label(unsigned index) {
    return index < TOOLBOX_CURRENCY_COUNT ? labels[index] : NULL;
}

bool toolbox_valid_amount(const char* amount) {
    if(!amount || !*amount || strlen(amount) >= TOOLBOX_AMOUNT_SIZE) return false;
    unsigned whole = 0U, fraction = 0U;
    bool dot = false;
    for(const char* p = amount; *p; ++p) {
        if(*p == '.' && !dot && whole) { dot = true; continue; }
        if(*p < '0' || *p > '9') return false;
        if(dot) ++fraction;
        else ++whole;
    }
    return whole > 0U && whole <= 9U && fraction <= 2U && (!dot || fraction > 0U);
}

bool toolbox_dictionary_url(const char* word, char* url, size_t capacity) {
    if(!url || !capacity) return false;
    url[0] = '\0';
    if(!word) return false;
    const size_t length = strlen(word);
    if(!length || length > TOOLBOX_WORD_MAX) return false;
    char encoded[TOOLBOX_WORD_MAX * 3U + 1U];
    size_t used = 0U;
    bool letter = false;
    for(size_t i = 0U; i < length; ++i) {
        char c = word[i];
        if(c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if(c >= 'a' && c <= 'z') { encoded[used++] = c; letter = true; }
        else if(c == '-') encoded[used++] = c;
        else if(c == '\'') { memcpy(encoded + used, "%27", 3U); used += 3U; }
        else return false;
    }
    if(!letter) return false;
    encoded[used] = '\0';
    int n = snprintf(url, capacity, "https://api.dictionaryapi.dev/api/v2/entries/en/%s", encoded);
    if(n < 0 || (size_t)n >= capacity) { url[0] = '\0'; return false; }
    return true;
}

bool toolbox_currency_url(unsigned from, unsigned to, char* url, size_t capacity) {
    if(!url || !capacity) return false;
    url[0] = '\0';
    if(from >= TOOLBOX_CURRENCY_COUNT || to >= TOOLBOX_CURRENCY_COUNT) return false;
    int n = snprintf(url, capacity, "https://api.frankfurter.dev/v2/rate/%s/%s", codes[from], codes[to]);
    if(n < 0 || (size_t)n >= capacity) { url[0] = '\0'; return false; }
    return true;
}

bool toolbox_format_conversion(
    const char* response, const char* amount, unsigned from, unsigned to,
    char* output, size_t capacity) {
    if(!output || !capacity) return false;
    output[0] = '\0';
    if(!response || !toolbox_valid_amount(amount) || from >= TOOLBOX_CURRENCY_COUNT ||
       to >= TOOLBOX_CURRENCY_COUNT || strncmp(response, "FIBRATE1\n", 9U)) return false;
    /* Validate all four bounded fields before using strtod. Reject old helpers,
     * incomplete packets, and a response for a different currency pair. */
    const char* value = response + 9U;
    if(strncmp(value, codes[from], 3U) || value[3] != '\t') return false;
    value += 4U;
    if(strncmp(value, codes[to], 3U) || value[3] != '\t') return false;
    value += 4U;
    const char* end = strchr(value, '\t');
    if(!end || end == value || (size_t)(end - value) >= 32U) return false;
    char rate_text[32];
    memcpy(rate_text, value, (size_t)(end - value));
    rate_text[end - value] = '\0';
    unsigned dots = 0U;
    for(const char* p = rate_text; *p; ++p) {
        if(*p == '.') { ++dots; continue; }
        if(*p < '0' || *p > '9') return false;
    }
    if(dots > 1U || rate_text[0] == '.' || rate_text[strlen(rate_text) - 1U] == '.') return false;
    const char* date = end + 1U;
    if(strlen(date) != 11U || date[10] != '\n') return false;
    for(size_t i = 0U; i < 10U; ++i) {
        if(i == 4U || i == 7U) { if(date[i] != '-') return false; }
        else if(date[i] < '0' || date[i] > '9') return false;
    }
    double rate = strtod(rate_text, NULL);
    double converted = strtod(amount, NULL) * rate;
    if(!isfinite(rate) || rate <= (double)0 || !isfinite(converted) || converted > (double)100000000000000ULL) return false;
    int n = snprintf(output, capacity,
        "%s %s\n= %.2f %s\n\nRate: %.6f\nDate: %.10s\nSource: Frankfurter\nReference rate",
        amount, codes[from], converted, codes[to], rate, date);
    if(n < 0 || (size_t)n >= capacity) { output[0] = '\0'; return false; }
    return true;
}
