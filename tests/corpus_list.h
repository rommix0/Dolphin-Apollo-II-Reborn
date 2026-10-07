#ifndef CORPUS_LIST_H
#define CORPUS_LIST_H
/* Golden corpus (spec §7.1): tests/corpus/<name>.txt -> tests/golden/<name>.frm.
   The first six are plain text from milestone 1. The rest exercise the control codes of
   docs/apollo2.txt section 6. tools/calibrate.py uses only the plain-text six for its F0 fit.
   long_clause (M4.1) is one clause long enough to fill tn_phonetic, so tn_convert_clause speaks it mid-clause.
   The next five (M4.2) reach the commands the rest never does: replies and ignored commands, the immediate
   @I+ / @I?, unusual arguments, an @ that matches no command, and @= (an empty socket, then a bank entered).
   The next seven (M4.3a) reach the segment generator's rare paths: a phrase pause that computes 0, and @D0;
   a level and a rising baseline; phonetic symbols without features, and a second stress in a word; a phrase of
   exactly 127 phonetic letters; and the 16-bit segment index (exactly 256 segments, then a page wrap at a phrase
   pause, at a word pause and at a segment). The seg_* texts depend on exact segment counts: one per file.
   The last three (M4.3b) reach the stress and phrase passes' rare paths: a word of five or more syllables;
   the linking R before a vowel; and a stressed syllabic consonant at the nucleus, with the accent still set
   after a stress toggle, and a linking R after a vowel that counted no syllable. */
#define CORPUS_NAMES \
    "hello", "prose", "numbers", "punctuation", "letters_k_to_z", "abbreviations", \
    "voice_b", "presets", "source_filter", "mark_space", "breathy", "pitch", "speed", \
    "prosody", "spell", "punct_mode", "pauses", "phonetic", "hypermode", "effects", \
    "formant_mod", "version", "volume", "mute", "queries", "long_clause", \
    "cmd_misc", "index_marks", "cmd_args", "cmd_unmatched", "bank_select", \
    "phrase_pauses", "baseline", "phonetic_stress", "phrase_127", "seg_256", "seg_wrap_phrase", \
    "seg_wrap_word", \
    "long_words", "linking_r", "syllabic_stress"
#endif
