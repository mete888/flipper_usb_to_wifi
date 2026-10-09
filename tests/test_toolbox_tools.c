#include "toolbox_tools.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_input(void) {
    char url[192];
    const char* const added[] = {"HKD", "SGD", "NZD", "DKK", "ZAR"};
    for(unsigned i = 0U; i < sizeof(added) / sizeof(added[0]); ++i) {
        assert(!strcmp(toolbox_currency_code(16U + i), added[i]));
    }
    /* Both selectors must expose every currency and build valid pair URLs. */
    for(unsigned from = 0U; from < TOOLBOX_CURRENCY_COUNT; ++from) {
        assert(toolbox_currency_label(from));
        for(unsigned to = 0U; to < TOOLBOX_CURRENCY_COUNT; ++to) {
            char expected[192];
            snprintf(expected, sizeof(expected), "https://api.frankfurter.dev/v2/rate/%s/%s",
                toolbox_currency_code(from), toolbox_currency_code(to));
            assert(toolbox_currency_url(from, to, url, sizeof(url)));
            assert(!strcmp(url, expected));
        }
    }
    assert(toolbox_currency_code(TOOLBOX_CURRENCY_COUNT) == NULL);
    assert(toolbox_dictionary_url("Hello", url, sizeof(url)));
    assert(strcmp(url, "https://api.dictionaryapi.dev/api/v2/entries/en/hello") == 0);
    assert(toolbox_dictionary_url("can't", url, sizeof(url)));
    assert(strstr(url, "can%27t"));
    assert(toolbox_dictionary_url("a", url, sizeof(url)));
    assert(!toolbox_dictionary_url("two words", url, sizeof(url)));
    assert(!toolbox_dictionary_url("../../foo", url, sizeof(url)));
    assert(!toolbox_dictionary_url("hello?x=1", url, sizeof(url)));
    assert(!toolbox_dictionary_url("'--", url, sizeof(url)));
    assert(!toolbox_dictionary_url("Hello", url, 4));
    assert(url[0] == '\0');
    assert(toolbox_currency_url(0, 3, url, sizeof(url)));
    assert(strcmp(url, "https://api.frankfurter.dev/v2/rate/USD/TRY") == 0);
    assert(!toolbox_currency_url(99, 0, url, sizeof(url)));
    assert(toolbox_currency_code(99) == NULL);
    const char* valid[] = {"0", "1", "0.01", "100.50", "999999999.99"};
    for(size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); ++i) assert(toolbox_valid_amount(valid[i]));
    const char* invalid[] = {"", "-1", "+1", "NaN", "1e3", "1.001", "1.", ".50", "1000000000", "1/2", "1 2", "1..2"};
    for(size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) assert(!toolbox_valid_amount(invalid[i]));
}

static void test_conversion(void) {
    const char* response = "FIBRATE1\nUSD\tTRY\t49.145000000000\t2026-10-02\n";
    char result[256], partial[128];
    assert(toolbox_format_conversion(response, "100.50", 0, 3, result, sizeof(result)));
    assert(strstr(result, "100.50 USD\n= 4939.07 TRY"));
    assert(strstr(result, "Date: 2026-10-02"));
    assert(!toolbox_format_conversion(response, "100", 1, 3, result, sizeof(result)));
    assert(!toolbox_format_conversion(response, "100", 0, 3, result, 5));
    assert(result[0] == '\0');
    for(size_t length = 0; length < strlen(response); ++length) {
        memcpy(partial, response, length); partial[length] = '\0';
        assert(!toolbox_format_conversion(partial, "1", 0, 3, result, sizeof(result)));
    }
    const char* invalid[] = {
        "FIBRATE1\nUSD\tTRY\tNaN\t2026-10-02\n",
        "FIBRATE1\nUSD\tTRY\t0\t2026-10-02\n",
        "FIBRATE1\nUSD\tTRY\t1..2\t2026-10-02\n",
        "FIBRATE1\nUSD\tTRY\t1\t2026-10-02\nextra",
        "FIBRATE1\nUSD\tTRY\t1\t2026-x0-02\n",
        "{\"rate\":49.1}"};
    for(size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i)
        assert(!toolbox_format_conversion(invalid[i], "1", 0, 3, result, sizeof(result)));
}

int main(void) {
    test_input(); test_conversion();
    puts("toolbox_tools tests: PASS");
    return 0;
}
