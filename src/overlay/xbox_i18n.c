/*
 * xbox_i18n.c - the overlay's words in the interface's language; see
 * xbox_i18n.h.
 */
#include "xbox_i18n.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define i18n_strcasecmp _stricmp
#else
#include <strings.h>
#define i18n_strcasecmp strcasecmp
#endif

typedef struct { const char *en, *tr; } i18n_pair;

static const i18n_pair k_fr[] = {
#include "i18n/fr.inc"
};
static const i18n_pair k_de[] = {
#include "i18n/de.inc"
};
static const i18n_pair k_it[] = {
#include "i18n/it.inc"
};

/* The languages with words of their own, by the dashboard's names. */
static struct {
    const char *name;
    const i18n_pair *pairs;
    size_t n;
    const i18n_pair **sorted;         /* by the English, for bsearch */
} s_lang[] = {
    { "english", NULL, 0, NULL },
    { "french", k_fr, sizeof k_fr / sizeof k_fr[0], NULL },
    { "german", k_de, sizeof k_de / sizeof k_de[0], NULL },
    { "italian", k_it, sizeof k_it / sizeof k_it[0], NULL },
};
#define LANG_COUNT ((int)(sizeof s_lang / sizeof s_lang[0]))

static volatile int s_cur;            /* index in s_lang; 0 English */
static volatile int s_built;

static int cmp_pair(const void *a, const void *b)
{
    return strcmp((*(const i18n_pair *const *)a)->en, (*(const i18n_pair *const *)b)->en);
}

static int cmp_key(const void *key, const void *elem)
{
    return strcmp((const char *)key, (*(const i18n_pair *const *)elem)->en);
}

/* The sorted indexes, all of them, once (before any language but English is
 * in use, so a reader on another thread never sees one half made). */
static void build(void)
{
    int l;
    size_t i;
    if (s_built)
        return;
    for (l = 1; l < LANG_COUNT; l++) {
        const i18n_pair **v = (const i18n_pair **)malloc(s_lang[l].n * sizeof *v);
        if (!v)
            continue;
        for (i = 0; i < s_lang[l].n; i++)
            v[i] = &s_lang[l].pairs[i];
        qsort(v, s_lang[l].n, sizeof *v, cmp_pair);
        s_lang[l].sorted = v;
    }
    s_built = 1;
}

void xbox_I18nSetLanguage(const char *name)
{
    int l, pick = 0;
    for (l = 1; name && l < LANG_COUNT; l++)
        if (!i18n_strcasecmp(name, s_lang[l].name))
            pick = l;
    if (pick)
        build();
    s_cur = pick && s_lang[pick].sorted ? pick : 0;
}

const char *xbox_I18nLanguage(void)
{
    return s_lang[s_cur].name;
}

const char *xbox_tr(const char *en)
{
    const int l = s_cur;
    const i18n_pair *const *hit;
    if (!en || !*en || !l)
        return en;
    hit = (const i18n_pair *const *)bsearch(en, s_lang[l].sorted, s_lang[l].n, sizeof *s_lang[l].sorted, cmp_key);
    return hit ? (*hit)->tr : en;
}
