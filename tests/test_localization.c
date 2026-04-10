#include <stdio.h>

#include "core/localization.h"

int run_localization_tests(void) {
    int fails = 0;

    if (!localization_normalize_language("pt") ||
        localization_normalize_language("pt")[0] != 'p') {
        printf("[i18n] falha ao normalizar pt\n");
        fails++;
    }
    if (!localization_normalize_language("en-US") ||
        localization_normalize_language("en-US")[0] != 'e' ||
        localization_normalize_language("en-US")[1] != 'n') {
        printf("[i18n] falha ao normalizar en-US\n");
        fails++;
    }
    if (!localization_normalize_language("es-es") ||
        localization_normalize_language("es-es")[0] != 'e' ||
        localization_normalize_language("es-es")[1] != 's') {
        printf("[i18n] falha ao normalizar es-es\n");
        fails++;
    }
    if (localization_language_supported("xx")) {
        printf("[i18n] idioma invalido aceito incorretamente\n");
        fails++;
    }
    if (localization_text_for("en", LOC_TEXT_LANGUAGE_CURRENT)[0] != 'C') {
        printf("[i18n] texto ingles inesperado para idioma atual\n");
        fails++;
    }
    if (localization_text_for("es", LOC_TEXT_UNKNOWN_COMMAND_PREFIX)[0] != 'C') {
        printf("[i18n] texto espanhol inesperado para comando desconhecido\n");
        fails++;
    }
    if (localization_text_for("pt-BR", LOC_TEXT_SPLASH_CURRENT)[0] != 'S') {
        printf("[i18n] texto pt-BR inesperado para splash atual\n");
        fails++;
    }
    if (localization_select("en", "pt", "en", "es")[0] != 'e') {
        printf("[i18n] selecao ingles inesperada\n");
        fails++;
    }
    if (localization_select("es", "pt", "en", "es")[0] != 'e' ||
        localization_select("es", "pt", "en", "es")[1] != 's') {
        printf("[i18n] selecao espanhol inesperada\n");
        fails++;
    }
    if (localization_select(NULL, "pt", "en", "es")[0] != 'p') {
        printf("[i18n] selecao padrao inesperada\n");
        fails++;
    }

    if (fails == 0) {
        printf("[tests] localization OK\n");
    }
    return fails;
}
