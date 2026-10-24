/* Linux tests for LCGarage's file and plate logic (no game needed) */
#include <windows.h>
#include <math.h>
#define VK_DELETE 0x2E
#define VK_UP 0x26
#define VK_DOWN 0x28
static inline int IsBadStringPtrA(const char *p, unsigned n) { return p == 0; }
#define LCG_TEST
#include "../src/lcgarage.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int matches(const char *text, const char *fmt)
{
    /* text against a vehicles.ide format: & letter, ^ digit, % either, # space, * emblem */
    if (strlen(text) != strlen(fmt)) return 0;
    for (int i = 0; fmt[i]; i++) {
        char t = text[i], f = fmt[i];
        if (f == '&' && !(t >= 'A' && t <= 'Z')) return 0;
        if (f == '^' && !(t >= '0' && t <= '9')) return 0;
        if (f == '%' && !((t >= 'A' && t <= 'Z') || (t >= '0' && t <= '9'))) return 0;
        if (f == '#' && t != ' ') return 0;
        if (f == '*' && t != '*') return 0;
        if (f == '(' && t != 'a' && t != 'b') return 0;
        if (((f >= 'A' && f <= 'Z') || (f >= '0' && f <= '9')) && t != f) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    strcpy(g_logPath, "/tmp/lcg_test.log");
    CHECK(joaat("adder") == 0xB779A091, "joaat adder = %08X", joaat("adder"));
    CHECK(joaat("ADDER") == joaat("adder"), "joaat is case-insensitive");

    /* vehicles.ide where the game keeps it: exe folder + "\\update\\common\\data\\vehicles.ide"
       (on Linux that's one file name with backslashes in it) */
    const char *ide = argc > 1 ? argv[1] : "vehicles.ide";
    strcpy(g_fakeExe, "/tmp/lcg/GTAIV.exe");
    char cmd[700];
    sprintf(cmd, "cp '%s' '/tmp/lcg\\update\\common\\data\\vehicles.ide'", ide);
    CHECK(system(cmd) == 0, "copy failed");
    g_ideSetting[0] = 0;
    load_ide();
    CHECK(g_ideLoaded, "vehicles.ide not loaded");
    CHECK(g_def[11].have && g_def[11].parent == 0 && g_def[11].chance == 50 && g_def[11].pic == 6 && g_def[11].font == 2, "plate 11 parsed wrong");
    CHECK(g_def[11].nrect == 1 && g_def[11].rect[0][0] == -2315.0f && g_def[11].rect[0][3] == 2100.0f, "plate 11 zone");
    CHECK(g_def[12].have && g_def[12].parent == 0xFF && g_def[12].n == 7, "plate 12 parsed wrong");
    CHECK(g_def[0].n == 7 && g_def[0].fmt[3] == PLATE_EMBLEM, "plate 0 format");
    CHECK(g_def[11].fmt[3] == FMT_AB && g_def[3].fmt[3] == PLATE_SPACE, "( and # in formats");
    CHECK(fabsf(g_def[11].sy - 0.475f) < 1e-5f, "float parse");
    printf("pictures %d, fonts %d, used models %d\n", g_npic, g_nfont, g_nused);

    /* random plates: Alderney garage position, a few models */
    float gx = -1743.04f, gy = 330.93f;
    struct { const char *model; int bike; } tests[] = { { "oldcar", 0 }, { "fixed", 0 }, { "infernus", 0 }, { "hexer", 1 }, { "police", 0 } };
    for (unsigned t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
        int picCount[255] = { 0 };
        char sample[13] = ""; int bad = 0;
        for (int i = 0; i < 4000; i++) {
            Plate pl; plate_random(&pl, joaat(tests[t].model), tests[t].bike, gx, gy);
            char txt[13]; plate_text(&pl, txt);
            picCount[pl.pic]++;
            int ok = 0;
            for (int d = 0; d < 255; d++) if (g_def[d].have && g_def[d].pic == pl.pic) {
                char f[13]; int k;
                for (k = 0; k < g_def[d].n; k++) {
                    BYTE v = g_def[d].fmt[k];
                    f[k] = v == FMT_CHAR ? '&' : v == FMT_NUM ? '^' : v == FMT_ANY ? '%' : v == FMT_AB ? '(' : code_char(v) == ' ' ? '#' : code_char(v);
                }
                f[k] = 0;
                if (matches(txt, f)) ok = 1;
            }
            if (!ok && ++bad > 3) continue;
            CHECK(ok, "%s: plate \"%s\" (picture %d) doesn't fit any format for that picture", tests[t].model, txt, pl.pic);
            if (!sample[0]) strcpy(sample, txt);
        }
        if (!strcmp(tests[t].model, "oldcar")) {
            /* 5% plate 10 (picture 7); of the rest half get 11 (picture 6); the others the Alderney plate (picture 2) */
            CHECK(picCount[7] > 120 && picCount[7] < 280, "rare plate: %d of 4000", picCount[7]);
            CHECK(picCount[6] > 1700 && picCount[6] < 2150, "plate 11: %d of 4000", picCount[6]);
        }
        if (!strcmp(tests[t].model, "fixed")) CHECK(!strcmp(sample, "DLW 796"), "fixed plate \"%s\"", sample);
        if (!strcmp(tests[t].model, "hexer")) CHECK(picCount[1] == 4000, "bikes use plate 1");
        if (!strcmp(tests[t].model, "police")) CHECK(picCount[4] == 4000, "police use plate 4");
        printf("%-9s e.g. %-9s pictures:", tests[t].model, sample);
        for (int p = 0; p < 255; p++) if (picCount[p]) printf(" %d=%.1f%%", p, picCount[p] / 40.0);
        printf("\n");
    }

    /* save / load round trip */
    sprintf(g_carsPath, "/tmp/lcg_test.cars");
    ZeroMemory(g_slot, sizeof(g_slot));
    Slot *s = &g_slot[0];
    s->used = 1; s->model = 0x72A4C31E; s->col[0] = 33; s->col[1] = 0; s->col[2] = 28; s->col[3] = 0;
    s->extra[2] = 1; s->livery = -1; s->away = 1;
    plate_random(&s->plate, joaat("sabregt"), 0, gx, gy);
    s->plate.len = 0; const char *txt = "DLW 796";
    for (int i = 0; txt[i]; i++) char_code(txt[i], &s->plate.code[s->plate.len++]);
    Slot s2 = g_slot[0];
    g_slot[3].used = 1; g_slot[3].model = 0x12345678; g_slot[3].plate.len = 0; g_slot[3].plate.a = 1;   /* empty plate */
    save_cars();
    load_cars();
    CHECK(count_cars() == 2, "loaded %d cars", count_cars());
    Slot *l = &g_slot[0];
    char a[13], b[13]; plate_text(&l->plate, a); plate_text(&s2.plate, b);
    CHECK(l->model == s2.model && l->col[0] == 33 && l->col[2] == 28 && l->extra[2] == 1 && l->extra[1] == 0 && l->livery == -1 && l->away == 1,
          "slot fields didn't round-trip");
    CHECK(!strcmp(a, b) && !strcmp(a, "DLW 796"), "plate text \"%s\" vs \"%s\"", a, b);
    CHECK(l->plate.pic == s2.plate.pic && l->plate.font == s2.plate.font && fabsf(l->plate.sx - s2.plate.sx) < 1e-3f &&
          fabsf(l->plate.oy - s2.plate.oy) < 1e-3f && fabsf(l->plate.r - s2.plate.r) < 1e-3f, "plate style didn't round-trip");
    CHECK(g_slot[1].used && g_slot[1].plate.len == 0, "empty plate slot");
    g_slots = 2;
    CHECK(free_slot() == -1, "full garage should have no free slot");
    g_slots = 20;
    CHECK(free_slot() == 2, "free slot %d", free_slot());
    system("cat /tmp/lcg_test.cars");

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
