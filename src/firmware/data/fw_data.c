/* Lays the converted tables out at their ROM addresses (M4.0 spec section 4.3). Every rule table must fill
   exactly its annotated size, no byte may be written twice, and every pointer table is computed from where the
   lists landed, so no address is stated twice in the data. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fw_data.h"

#define MAX_LISTS 640

typedef struct {
    fw_image *img;
    const fw_rule_list *list[MAX_LISTS];
    uint16_t addr[MAX_LISTS];
    size_t nlists;
    char *err;
    size_t errlen;
    bool ok;
} enc;

static void fail(enc *e, const char *fmt, ...)
{
    if (e->ok && e->errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->err, e->errlen, fmt, ap);
        va_end(ap);
    }
    e->ok = false;
}

static void put(enc *e, bool low, uint32_t addr, uint8_t v)
{
    if (addr < 0x4000 || addr > 0xFFFF || (low && addr >= 0x8000)) {
        fail(e, "byte at $%04X is outside its space", (unsigned)addr);
        return;
    }
    uint8_t *mem = low ? e->img->low : e->img->rom, *ok = low ? e->img->low_ok : e->img->rom_ok;
    uint32_t i = low ? addr - 0x4000 : addr;
    if (ok[i >> 3] & (1u << (i & 7))) {
        fail(e, "$%04X%s written twice", (unsigned)addr, low ? " (LOW)" : "");
        return;
    }
    mem[i] = v;
    ok[i >> 3] |= (uint8_t)(1u << (i & 7));
}

static uint32_t put_text(enc *e, bool low, uint32_t at, const char *s, bool context)
{
    for (; *s; s++) {
        if (context && (uint8_t)*s < 0x1E)
            fail(e, "context byte %02X at $%04X is below $1E", (uint8_t)*s, (unsigned)at);
        put(e, low, at++, (uint8_t)*s);
    }
    return at;
}

static uint32_t put_rule_list(enc *e, bool low, uint32_t at, const fw_rule_list *l)
{
    for (uint16_t i = 0; i < l->count; i++) {
        const fw_rule *r = &l->rules[i];
        for (size_t n = strlen(r->left); n-- > 0;) {   /* stored nearest character first */
            if ((uint8_t)r->left[n] < 0x1E)
                fail(e, "context byte below $1E at $%04X", (unsigned)at);
            put(e, low, at++, (uint8_t)r->left[n]);
        }
        size_t nc = strlen(r->consumed), no = strlen(r->output);
        if (nc >= 0x1E || no >= 0x1E)
            fail(e, "rule at $%04X: count or length >= $1E", (unsigned)at);
        put(e, low, at++, (uint8_t)nc);
        at = put_text(e, low, at, r->consumed, true);
        at = put_text(e, low, at, r->right, true);
        put(e, low, at++, (uint8_t)no);
        at = put_text(e, low, at, r->output, false);
    }
    put(e, low, at++, 0xFF);
    return at;
}

static uint16_t list_addr(enc *e, const fw_rule_list *l)
{
    for (size_t i = 0; i < e->nlists; i++)
        if (e->list[i] == l)
            return e->addr[i];
    fail(e, "a pointer table refers to a list in no rule table");
    return 0;
}

static void put_word(enc *e, uint32_t at, uint16_t v)
{
    put(e, false, at, (uint8_t)v);
    put(e, false, at + 1, (uint8_t)(v >> 8));
}

bool fw_image_build(fw_image *img, char *err, size_t errlen)
{
    static enc e;
    memset(img, 0, sizeof *img);
    memset(&e, 0, sizeof e);
    e.img = img;
    e.err = err;
    e.errlen = errlen;
    e.ok = true;

    /* 1. The rule tables, each at its annotated address and exactly its annotated size. */
    for (size_t t = 0; t < fw_rule_table_count; t++) {
        const fw_rule_table *tab = fw_rule_tables[t];
        uint32_t at = tab->addr;
        for (uint16_t i = 0; i < tab->count; i++) {
            if (e.nlists == MAX_LISTS) {
                fail(&e, "more than %d rule lists", MAX_LISTS);
                return false;
            }
            e.list[e.nlists] = tab->lists[i];
            e.addr[e.nlists++] = (uint16_t)at;
            at = put_rule_list(&e, tab->low, at, tab->lists[i]);
        }
        if (at - tab->addr != tab->size)
            fail(&e, "%s: %u bytes, the annotation says %u", tab->name, (unsigned)(at - tab->addr), tab->size);
    }
    /* 2. tn_char_rules_lo/hi: each character's list. */
    for (int c = 0; c < 256; c++) {
        uint16_t a = list_addr(&e, tn_char_lists[c]);
        put(&e, false, TN_CHAR_RULES_LO + (uint32_t)c, (uint8_t)a);
        put(&e, false, TN_CHAR_RULES_HI + (uint32_t)c, (uint8_t)(a >> 8));
    }
    /* 3. lts_pair_index and lts_pairs_a..z ($0000 = no list for the pair). */
    for (int k = 0; k < 26; k++) {
        uint32_t tab = LTS_PAIRS_A + 52u * (uint32_t)k;
        put_word(&e, LTS_PAIR_INDEX + 2u * (uint32_t)k, (uint16_t)tab);
        for (int s = 0; s < 26; s++) {
            const fw_rule_list *l = lts_pair_lists[k][s];
            put_word(&e, tab + 2u * (uint32_t)s, l ? list_addr(&e, l) : 0);
        }
    }
    /* 4. The affix lists of the context symbols % & @, and the stray $00 before them. */
    put(&e, false, UNUSED_9824, 0x00);
    for (size_t i = 0; i < fw_affix_list_count; i++) {
        const fw_affix_list *a = &fw_affix_lists[i];
        uint32_t at = a->addr;
        put(&e, false, at++, 0x00);
        put(&e, false, at++, 0x0D);
        put(&e, false, at++, 0x0A);
        for (uint8_t n = 0; n < a->count; n++) {
            at = put_text(&e, false, at, a->entries[n], false);
            put(&e, false, at++, 0x0D);
            put(&e, false, at++, 0x0A);
        }
        put(&e, false, at, 0x00);
    }
    /* 5. lts_char_class. */
    for (int c = 0; c < 256; c++)
        put(&e, false, LTS_CHAR_CLASS + (uint32_t)c, lts_char_class[c]);
    /* 6. The length-prefixed phonetic texts (M4.1 spec 4). */
    for (size_t i = 0; i < fw_phonetic_text_count; i++) {
        const fw_phonetic_text *t = &fw_phonetic_texts[i];
        size_t n = strlen(t->text);
        if (n == 0 || n > 0xFF)
            fail(&e, "%s: text length %u", t->name, (unsigned)n);
        uint32_t at = t->addr;
        put(&e, false, at++, (uint8_t)n);
        at = put_text(&e, false, at, t->text, false);
        at = put_text(&e, false, at, t->tail, false);
        if (at - t->addr != t->size)
            fail(&e, "%s: %u bytes, the annotation says %u", t->name, (unsigned)(at - t->addr), t->size);
    }
    return e.ok;
}

const fw_image *fw_image_get(void)
{
    static fw_image img;
    static int state;   /* 0 = not built, 1 = built, -1 = failed */
    if (state == 0) {
        char err[160] = "";
        state = fw_image_build(&img, err, sizeof err) ? 1 : -1;
        if (state < 0)
            fprintf(stderr, "firmware data image: %s\n", err);
    }
    return state > 0 ? &img : NULL;
}
