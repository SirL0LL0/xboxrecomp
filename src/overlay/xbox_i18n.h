/*
 * xbox_i18n.h - the overlay's words in the interface's language.
 *
 * The overlay is written in English; the words a player reads go through
 * TR(), which gives them in the language interface.language names (the
 * settings overlay's Display page, RECOMP_UI_LANGUAGE). French, German and
 * Italian have their words (i18n/fr.inc, i18n/de.inc, i18n/it.inc: the
 * English text and its translation, one pair a line); every other language,
 * and any text a table does not have, stays English.
 *
 * The text is the key: TR("Quit game") finds the pair whose English is
 * "Quit game". A printf format is translated whole, before it is filled
 * (snprintf(out, n, TR("Saved to %s"), path)), and its translation has the
 * same conversions in the same order (tools/i18n_check.py checks that). A
 * table of names (the choices of a row) is translated where it is drawn, as
 * the language can change while the overlay is open.
 *
 * Settings keys, the installer's line protocol and the log stay English:
 * they are read by programs, not people.
 */
#ifndef XBOX_OVERLAY_I18N_H
#define XBOX_OVERLAY_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

/* The language by the dashboard's name ("english", "italian", ...); one with
 * no words of its own, or NULL, is English. Any thread may call TR while
 * another sets the language: the tables are built once, at the first call. */
void xbox_I18nSetLanguage(const char *name);
/* "english", "french", "german" or "italian": the words in use. */
const char *xbox_I18nLanguage(void);

/* en in the interface's language, or en itself (and NULL for NULL). */
const char *xbox_tr(const char *en);

#ifndef TR
#define TR(s) xbox_tr(s)
#endif

#ifdef __cplusplus
}
#endif

#endif /* XBOX_OVERLAY_I18N_H */
