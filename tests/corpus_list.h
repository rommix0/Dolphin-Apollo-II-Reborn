#ifndef CORPUS_LIST_H
#define CORPUS_LIST_H
/* Golden corpus (spec §7.1): tests/corpus/<name>.txt -> tests/golden/<name>.frm.
   The first six are plain text from milestone 1. The rest exercise the control codes of
   docs/apollo2.txt section 6. tools/calibrate.py uses only the plain-text six for its F0 fit.
   long_clause (M4.1) is one clause long enough to fill tn_phonetic, so tn_convert_clause speaks it mid-clause. */
#define CORPUS_NAMES \
    "hello", "prose", "numbers", "punctuation", "letters_k_to_z", "abbreviations", \
    "voice_b", "presets", "source_filter", "mark_space", "breathy", "pitch", "speed", \
    "prosody", "spell", "punct_mode", "pauses", "phonetic", "hypermode", "effects", \
    "formant_mod", "version", "volume", "mute", "queries", "long_clause"
#endif
