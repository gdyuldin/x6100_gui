#include "broadcast_db.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BROADCAST_FILE         "/mnt/broadcast.csv"
#define BROADCAST_DEFAULT_FILE "/usr/share/x6100/broadcast.csv"
#define LINE_SIZE              1024

typedef struct {
    double freq_khz;
    int start_minute;
    int stop_minute;
    char station[202];
    char language[50];
    char target[63];
    char days[60];
    char itu[50];
    char remarks[136];
    char start_date[61];
    char stop_date[61];
} broadcast_entry_t;

static broadcast_entry_t *entries = NULL;
static size_t entry_count = 0;
static size_t entry_capacity = 0;

static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }

    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) unlink(dst);
    return ok;
}

static int parse_hhmm(const char *s) {
    if (!s || strlen(s) < 4) return -1;
    if (!isdigit((unsigned char)s[0]) || !isdigit((unsigned char)s[1]) ||
        !isdigit((unsigned char)s[2]) || !isdigit((unsigned char)s[3])) return -1;
    int hh = (s[0]-'0')*10 + s[1]-'0';
    int mm = (s[2]-'0')*10 + s[3]-'0';
    if (hh == 24 && mm == 0) return 1440;
    if (hh > 23 || mm > 59) return -1;
    return hh * 60 + mm;
}

static bool parse_time_range(const char *s, int *start, int *stop) {
    if (!s || strlen(s) < 9 || s[4] != '-') return false;
    *start = parse_hhmm(s);
    *stop = parse_hhmm(s + 5);
    return *start >= 0 && *stop >= 0;
}

static void copy_text(char *dst, size_t size, const char *src) {
    if (!size) return;
    if (!src) src = "";
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
    size_t n = strlen(dst);
    while (n && (dst[n-1] == '\r' || dst[n-1] == '\n')) dst[--n] = '\0';
}


/*
 * Station names in EiBi files may contain accented characters.  The Sony
 * bitmap fonts used by the X6100 do not contain the full Unicode repertoire,
 * and some EiBi CSV copies have also passed through a MacRoman conversion
 * (for example Latin-1 "ñ" appears as Unicode U+00D2, "Ò").
 *
 * Convert station names to display-safe ASCII when the database is loaded.
 * This keeps both the Broadcast Info overlay and channels.json safe for LVGL.
 */
static uint32_t utf8_next(const unsigned char **pp) {
    const unsigned char *p = *pp;
    uint32_t cp;

    if (*p < 0x80) {
        cp = *p++;
    } else if ((*p & 0xE0) == 0xC0 && p[1]) {
        cp = ((uint32_t)(p[0] & 0x1F) << 6) |
             (uint32_t)(p[1] & 0x3F);
        p += 2;
    } else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
        cp = ((uint32_t)(p[0] & 0x0F) << 12) |
             ((uint32_t)(p[1] & 0x3F) << 6) |
             (uint32_t)(p[2] & 0x3F);
        p += 3;
    } else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
        cp = ((uint32_t)(p[0] & 0x07) << 18) |
             ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) |
             (uint32_t)(p[3] & 0x3F);
        p += 4;
    } else {
        /* Invalid UTF-8: consume one byte and replace it safely. */
        cp = '?';
        ++p;
    }

    *pp = p;
    return cp;
}

static char station_ascii_char(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7E) return (char)cp;

    /* Normal Latin-1 accented letters. */
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3:
        case 0x00C4: case 0x00C5: return 'A';
        case 0x00C6: return 'A';
        case 0x00C7: return 'C';
        case 0x00C9: case 0x00CA: case 0x00CB: return 'E';
        case 0x00CE: case 0x00CF: return 'I';
        case 0x00D0: return 'D';
        case 0x00D1: return 'N';
        case 0x00D2: /* In the current EiBi CSV this is MacRoman mojibake for ñ. */ return 'n';
        case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8: return 'O';
        case 0x00D9: /* Current CSV: MacRoman mojibake for ô. */ return 'o';
        case 0x00DA: case 0x00DC: return 'U';
        case 0x00DD: return 'Y';
        case 0x00DE: return 'T';
        case 0x00DF: return 's';
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3:
        case 0x00E4: case 0x00E5: return 'a';
        case 0x00E6: return 'a';
        case 0x00E7: return 'c';
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
        case 0x00F0: return 'd';
        case 0x00F1: return 'n';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5:
        case 0x00F6: case 0x00F8: return 'o';
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: return 'u';
        case 0x00FD: case 0x00FF: return 'y';

        /* MacRoman mojibake present in EiBi CSV files produced on macOS. */
        case 0x00B7: return 'a'; /* á */
        case 0x00C8: return 'e'; /* é */
        case 0x00CD: return 'e'; /* ê */
        case 0x00DB: return 'o'; /* ó */
        case 0x00CC: return 'i'; /* í */
        case 0x00AF: return 'o'; /* ø */
        case 0x00AC: return 'A'; /* Â */
        case 0x02D9: return 'o'; /* common damaged Vietnamese diacritic */
        default: return '?';
    }
}

static void sanitize_station_name(char *dst, size_t size, const char *src) {
    if (!size) return;
    if (!src) src = "";

    const unsigned char *p = (const unsigned char *)src;
    size_t out = 0;

    while (*p && out + 1 < size) {
        uint32_t cp = utf8_next(&p);
        char c = station_ascii_char(cp);
        if (c) dst[out++] = c;
    }

    while (out && (dst[out - 1] == '\r' || dst[out - 1] == '\n')) --out;
    dst[out] = '\0';
}

static bool append_entry(const broadcast_entry_t *entry) {
    if (entry_count == entry_capacity) {
        size_t cap = entry_capacity ? entry_capacity * 2 : 1024;
        broadcast_entry_t *p = realloc(entries, cap * sizeof(*entries));
        if (!p) return false;
        entries = p;
        entry_capacity = cap;
    }
    entries[entry_count++] = *entry;
    return true;
}

bool broadcast_db_init(void) {
    free(entries);
    entries = NULL;
    entry_count = entry_capacity = 0;

    if (access(BROADCAST_FILE, F_OK) != 0 &&
        !copy_file(BROADCAST_DEFAULT_FILE, BROADCAST_FILE)) return false;

    FILE *f = fopen(BROADCAST_FILE, "r");
    if (!f) return false;

    char line[LINE_SIZE];
    bool first = true;
    while (fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }

        char *fields[11] = {0};
        size_t count = 0;
        char *p = line;
        while (count < 11) {
            fields[count++] = p;
            char *sep = strchr(p, ';');
            if (!sep) break;
            *sep = '\0';
            p = sep + 1;
        }
        if (count < 5) continue;

        broadcast_entry_t e;
        memset(&e, 0, sizeof(e));
        e.freq_khz = strtod(fields[0], NULL);
        if (e.freq_khz <= 0 || !parse_time_range(fields[1], &e.start_minute, &e.stop_minute)) continue;
        copy_text(e.days, sizeof(e.days), count > 2 ? fields[2] : "");
        copy_text(e.itu, sizeof(e.itu), count > 3 ? fields[3] : "");
        sanitize_station_name(e.station, sizeof(e.station), fields[4]);
        copy_text(e.language, sizeof(e.language), count > 5 ? fields[5] : "");
        copy_text(e.target, sizeof(e.target), count > 6 ? fields[6] : "");
        copy_text(e.remarks, sizeof(e.remarks), count > 7 ? fields[7] : "");
        copy_text(e.start_date, sizeof(e.start_date), count > 9 ? fields[9] : "");
        copy_text(e.stop_date, sizeof(e.stop_date), count > 10 ? fields[10] : "");
        if (!e.station[0]) continue;
        if (!append_entry(&e)) { fclose(f); return false; }
    }
    fclose(f);
    return entry_count > 0;
}

static bool time_is_active(int now, int start, int stop) {
    if (start == 0 && stop == 1440) return true;
    if (start < stop) return now >= start && now < stop;
    if (start > stop) return now >= start || now < stop;
    return true;
}

static int weekday_token(const char *s) {
    static const char *names[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    for (int i = 0; i < 7; ++i) if (!strncmp(s, names[i], 2)) return i;
    return -1;
}

static bool day_in_range(int today, int a, int b) {
    if (a <= b) return today >= a && today <= b;
    return today >= a || today <= b;
}

static bool days_is_active(const char *days, int today) {
    if (!days || !days[0]) return true;
    if (!strcmp(days, "irr") || !strcmp(days, "Test")) return true;

    /* Numeric EiBi day notation: 1=Monday ... 7=Sunday. */
    bool only_digits = true;
    for (const char *p = days; *p; ++p) {
        if (!isdigit((unsigned char)*p) && *p != ',') { only_digits = false; break; }
    }
    if (only_digits) {
        int eibi_today = today == 0 ? 7 : today;
        return strchr(days, '0' + eibi_today) != NULL;
    }

    const char *p = days;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        int a = weekday_token(p);
        if (a < 0) { ++p; continue; }
        p += 2;
        if (*p == '-') {
            ++p;
            int b = weekday_token(p);
            if (b >= 0) {
                if (day_in_range(today, a, b)) return true;
                p += 2;
                continue;
            }
        }
        if (today == a) return true;
        /* Handles compact forms such as SaSu. */
    }
    return false;
}

static int month_day_value(const char *s) {
    if (!s || strlen(s) < 4) return -1;
    if (s[0] == '[') return -1; /* EiBi bracketed values are informational, not schedule limits. */
    for (int i = 0; i < 4; ++i) if (!isdigit((unsigned char)s[i])) return -1;
    int day = (s[0]-'0')*10 + s[1]-'0';
    int mon = (s[2]-'0')*10 + s[3]-'0';
    if (day < 1 || day > 31 || mon < 1 || mon > 12) return -1;
    return mon * 100 + day;
}

static bool date_is_active(const char *start_s, const char *stop_s, const struct tm *utc) {
    int start = month_day_value(start_s);
    int stop = month_day_value(stop_s);
    if (start < 0 && stop < 0) return true;
    int now = (utc->tm_mon + 1) * 100 + utc->tm_mday;
    if (start >= 0 && stop >= 0) {
        if (start <= stop) return now >= start && now <= stop;
        return now >= start || now <= stop;
    }
    if (start >= 0) return now >= start;
    return now <= stop;
}

static void make_match(const broadcast_entry_t *e, broadcast_match_t *m) {
    memset(m, 0, sizeof(*m));
    m->freq_khz = e->freq_khz;
    m->start_minute = e->start_minute;
    m->stop_minute = e->stop_minute;
    copy_text(m->station, sizeof(m->station), e->station);
    copy_text(m->language, sizeof(m->language), e->language);
    copy_text(m->target, sizeof(m->target), e->target);
    copy_text(m->days, sizeof(m->days), e->days);
    copy_text(m->itu, sizeof(m->itu), e->itu);
    copy_text(m->remarks, sizeof(m->remarks), e->remarks);
}

size_t broadcast_db_find_matches(int32_t freq_hz, int tolerance_hz,
                                 broadcast_match_t *matches, size_t max_matches) {
    if (!entries || !matches || !max_matches) return 0;

    time_t now_t = time(NULL);
    struct tm utc;
    if (!gmtime_r(&now_t, &utc)) return 0;
    int now_minute = utc.tm_hour * 60 + utc.tm_min;
    size_t found = 0;

    for (size_t i = 0; i < entry_count && found < max_matches; ++i) {
        const broadcast_entry_t *e = &entries[i];
        if (fabs(e->freq_khz * 1000.0 - (double)freq_hz) > (double)tolerance_hz) continue;
        if (!time_is_active(now_minute, e->start_minute, e->stop_minute)) continue;

        /* For a schedule crossing midnight, the after-midnight portion still
         * belongs to the day on which the transmission started. */
        int schedule_wday = utc.tm_wday;
        if (e->start_minute > e->stop_minute && now_minute < e->stop_minute)
            schedule_wday = (schedule_wday + 6) % 7;
        if (!days_is_active(e->days, schedule_wday)) continue;
        if (!date_is_active(e->start_date, e->stop_date, &utc)) continue;
        make_match(e, &matches[found++]);
    }
    return found;
}

size_t broadcast_db_count(void) { return entry_count; }
