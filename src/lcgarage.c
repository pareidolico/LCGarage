/*
 * LCGarage.asi - a Mafia II style garage for GTA IV / EFLC
 * (Complete Edition 1.2.0.59 with FusionFix; works alongside PlateChanger
 * and LibertyCityPlates).
 *
 *   Store: stop at a garage door facing it and honk. The camera shows the
 *     back of the car while you type its plate (blank = a random plate made
 *     from your LibertyCityPlates vehicles.ide, the way LibertyCityPlates
 *     would pick one there). Enter stores it. A car that came out of the
 *     garage goes back in with the plate it already has.
 *   Take out: on foot at the door, press the take-out key (F6) and pick a
 *     car. It comes out repaired and clean with its plate. If a car you took
 *     out is wrecked or lost, it's back in the garage: take it out again.
 *
 * Cars are kept in LCGarage.cars, settings in LCGarage.ini, log in
 * LCGarage.log, all next to this file. Plain Win32, no C runtime needed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef LCG_TEST
void *memset(void *d, int c, unsigned int n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, unsigned int n) { unsigned char *a = d; const unsigned char *b = s; while (n--) *a++ = *b++; return d; }
#endif

#define VERSION "1.0"

/* ------------------------------------------------------------ logging */
static char g_dir[MAX_PATH], g_logPath[MAX_PATH], g_iniPath[MAX_PATH], g_carsPath[MAX_PATH];

static void plog(const char *fmt, ...)
{
    char buf[700];
    va_list ap;
    va_start(ap, fmt);
    int n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    lstrcatA(buf, "\r\n");
    HANDLE h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, buf, lstrlenA(buf), &w, NULL);
    CloseHandle(h);
}

/* ------------------------------------------------------------ small helpers */
typedef union { float f; DWORD d; } FBits;
static DWORD fbits(float f) { FBits b; b.f = f; return b.d; }

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ','; }

static int next_token(const char **p, char *out, int max)
{
    const char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    int n = 0;
    while (*s && !is_space(*s)) { if (n < max - 1) out[n++] = *s; s++; }
    out[n] = 0;
    *p = s;
    return n;
}

static int parse_int(const char *s, int *v)
{
    int neg = 0, n = 0, any = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    for (; *s >= '0' && *s <= '9'; s++) { n = n * 10 + (*s - '0'); any = 1; }
    if (!any || *s) return 0;
    *v = neg ? -n : n;
    return 1;
}

static int parse_hex(const char *s, DWORD *v)
{
    DWORD n = 0; int any = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' : (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 : (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
        if (d < 0) return 0;
        n = n * 16 + (DWORD)d; any = 1;
    }
    if (!any) return 0;
    *v = n;
    return 1;
}

static int parse_float(const char *s, float *v)
{
    int neg = 0, any = 0;
    float r = 0, scale = 1;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    for (; *s >= '0' && *s <= '9'; s++) { r = r * 10 + (float)(*s - '0'); any = 1; }
    if (*s == '.') { s++; for (; *s >= '0' && *s <= '9'; s++) { scale /= 10; r += (float)(*s - '0') * scale; any = 1; } }
    if (!any || *s) return 0;
    *v = neg ? -r : r;
    return 1;
}

/* float as text with the given number of decimals (wsprintf has no %f) */
static char *ffn(char *o, float v, int dec)
{
    char *s = o;
    int mul = 1;
    for (int i = 0; i < dec; i++) mul *= 10;
    if (v < 0) { *s++ = '-'; v = -v; }
    if (v > 1e7f) v = 1e7f;
    int ip = (int)v, fp = (int)((v - (float)ip) * (float)mul + 0.5f);
    if (fp >= mul) { ip++; fp -= mul; }
    if (dec == 0) wsprintfA(s, "%d", ip);
    else if (dec == 2) wsprintfA(s, "%d.%02d", ip, fp);
    else wsprintfA(s, "%d.%04d", ip, fp);
    return o;
}

static float dist3(float ax, float ay, float az, float bx, float by, float bz)
{
    float dx = ax - bx, dy = ay - by, dz = az - bz, d2 = dx * dx + dy * dy + dz * dz;
    if (d2 <= 0) return 0;
    float r = d2 > 1 ? d2 / 2 : 1;
    for (int i = 0; i < 24; i++) r = 0.5f * (r + d2 / r);
    return r;
}

static float wrap360(float h) { while (h >= 360.0f) h -= 360.0f; while (h < 0) h += 360.0f; return h; }

static DWORD g_rng = 0x2545F491;
static DWORD rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rnd_range(int lo, int hi) { return lo + (int)(rnd() % (DWORD)(hi - lo + 1)); }

/* the game's model name hash */
static DWORD joaat(const char *s)
{
    DWORD h = 0;
    for (; *s; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c += 32;
        h += (BYTE)c; h += h << 10; h ^= h >> 6;
    }
    h += h << 3; h ^= h >> 11; h += h << 15;
    return h;
}

static char *read_file(const char *path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(h, NULL), got = 0;
    char *buf = (size != INVALID_FILE_SIZE && size < 8 * 1024 * 1024) ? (char *)HeapAlloc(GetProcessHeap(), 0, size + 1) : NULL;
    if (buf && ReadFile(h, buf, size, &got, NULL)) buf[got] = 0;
    else if (buf) { HeapFree(GetProcessHeap(), 0, buf); buf = NULL; }
    CloseHandle(h);
    return buf;
}

/* ------------------------------------------------------------ pattern scan */
static BYTE *g_base;
static DWORD g_size;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static BYTE *find_pattern_n(const char *pat, int nth, int *count)
{
    BYTE bytes[96]; BYTE mask[96]; int n = 0;
    for (const char *p = pat; *p && n < 96;) {
        if (*p == ' ') { p++; continue; }
        if (*p == '?') { bytes[n] = 0; mask[n++] = 0; p++; if (*p == '?') p++; continue; }
        int hi = hexval(p[0]), lo = hexval(p[1]);
        if (hi < 0 || lo < 0) return NULL;
        bytes[n] = (BYTE)(hi * 16 + lo); mask[n++] = 1; p += 2;
    }
    BYTE *hit = NULL; int found = 0;
    for (DWORD i = 0; i + n <= g_size; i++) {
        int k = 0;
        while (k < n && (!mask[k] || g_base[i + k] == bytes[k])) k++;
        if (k == n) {
            if (found == nth) hit = g_base + i;
            found++;
            if (!count && hit) break;
        }
    }
    if (count) *count = found;
    return hit;
}
static BYTE *find_pattern(const char *pat) { return find_pattern_n(pat, 0, NULL); }

/* ------------------------------------------------------------ script commands */
static DWORD  *g_pTableSize;
static DWORD **g_ppTable;
static BYTE   *g_pUserPause, *g_pScriptPause;

typedef struct {
    void  *ret;
    DWORD  argc;
    void  *args;
    DWORD  dataCount;
    void  *orig[4];
    float  temp[4][4];
    DWORD  stack[16];
} NCtx;
typedef void (__cdecl *NHandler)(NCtx *);
typedef void *(__stdcall *GetNativeAddressFn)(DWORD hash);
static GetNativeAddressFn g_getNativeAddress;

static NHandler get_handler(DWORD hash)
{
    if (g_getNativeAddress) return (NHandler)g_getNativeAddress(hash);
    if (!g_pTableSize || !g_ppTable) return NULL;
    DWORD size = *g_pTableSize;
    DWORD *tbl = *g_ppTable;
    if (!size || !tbl || !hash) return NULL;
    DWORD idx = hash % size, h = hash, e = tbl[2 * idx];
    if (e != hash) {
        while (e) {
            h = (h >> 1) + 1;
            idx = (h + idx) % size;
            e = tbl[2 * idx];
            if (e == hash) break;
        }
    }
    if (!e) return NULL;
    return (NHandler)tbl[2 * idx + 1];
}

static int g_missing;
static volatile DWORD g_lastHash;
static DWORD call(DWORD hash, int argc, DWORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5, DWORD a6)
{
    NHandler fn = get_handler(hash);
    if (!fn) { g_missing = 1; return 0; }
    NCtx c;
    ZeroMemory(&c, sizeof(c));
    c.args = c.stack; c.ret = c.stack; c.argc = (DWORD)argc;
    DWORD a[7] = { a0, a1, a2, a3, a4, a5, a6 };
    for (int i = 0; i < argc; i++) c.stack[i] = a[i];
    g_lastHash = hash;
    fn(&c);
    g_lastHash = 0;
    return c.stack[0];
}
#define D(x) ((DWORD)(x))
#define C0(h)                      call(h, 0, 0, 0, 0, 0, 0, 0, 0)
#define C1(h, a)                   call(h, 1, D(a), 0, 0, 0, 0, 0, 0)
#define C2(h, a, b)                call(h, 2, D(a), D(b), 0, 0, 0, 0, 0)
#define C3(h, a, b, c)             call(h, 3, D(a), D(b), D(c), 0, 0, 0, 0)
#define C4(h, a, b, c, d)          call(h, 4, D(a), D(b), D(c), D(d), 0, 0, 0)
#define C5(h, a, b, c, d, e)       call(h, 5, D(a), D(b), D(c), D(d), D(e), 0, 0)
#define C6(h, a, b, c, d, e, f)    call(h, 6, D(a), D(b), D(c), D(d), D(e), D(f), 0)
#define C7(h, a, b, c, d, e, f, g) call(h, 7, D(a), D(b), D(c), D(d), D(e), D(f), D(g))
#define F(x) fbits(x)

#define GET_PLAYER_ID                        0x62E319C6
#define CONVERT_INT_TO_PLAYERINDEX           0x5996315E
#define IS_PLAYER_PLAYING                    0x08274BA4
#define GET_PLAYER_CHAR                      0x511454A9
#define IS_CHAR_IN_ANY_CAR                   0x71184DA3
#define GET_CAR_CHAR_IS_USING                0x1B067237
#define GET_CHAR_COORDINATES                 0x2B5C06E6
#define GET_CHAR_HEADING                     0x057A3AC7
#define SET_CHAR_HEADING                     0x46B5523B
#define DOES_VEHICLE_EXIST                   0x67A42263
#define IS_CAR_DEAD                          0x2AAB340A
#define GET_CAR_MODEL                        0x5FF84497
#define GET_CAR_COLOURS                      0x6CAC3D62
#define GET_EXTRA_CAR_COLOURS                0x25B87BCA
#define CHANGE_CAR_COLOUR                    0x06441EAF
#define SET_EXTRA_CAR_COLOURS                0x6CB14354
#define GET_CAR_COORDINATES                  0x2D432EAB
#define GET_CAR_HEADING                      0x46803CFA
#define SET_CAR_HEADING                      0x75E40528
#define GET_CAR_SPEED                        0x16DD2D00
#define GET_CAR_LIVERY                       0x10237666
#define SET_CAR_LIVERY                       0x2E9E149D
#define IS_VEHICLE_EXTRA_TURNED_ON           0x4B920E81
#define TURN_OFF_VEHICLE_EXTRA               0x05966824
#define SET_VEHICLE_DIRT_LEVEL               0x02A57428
#define SET_CAR_ON_GROUND_PROPERLY           0x0E717E98
#define SET_CAR_ENGINE_ON                    0x0CAA42D0
#define GET_OFFSET_FROM_CAR_IN_WORLD_COORDS  0x7F8D3DD9
#define CLEAR_AREA_OF_CARS                   0x24367E48
#define IS_THIS_MODEL_A_VEHICLE              0x62BC0AEE
#define IS_THIS_MODEL_A_BIKE                 0x57F46B33
#define GET_DISPLAY_NAME_FROM_VEHICLE_MODEL  0x404E0056
#define GET_STRING_FROM_TEXT_FILE            0x332F0E9A
#define DOES_TEXT_LABEL_EXIST                0x2A611607
#define REQUEST_MODEL                        0x502B5185
#define HAS_MODEL_LOADED                     0x4E61480A
#define MARK_MODEL_AS_NO_LONGER_NEEDED       0x00FA0E33
#define CREATE_CAR                           0x2F1D6843
#define SET_CAR_AS_MISSION_CAR               0x210A33B2
#define DELETE_CAR                           0x7F71342D
#define MARK_CAR_AS_NO_LONGER_NEEDED         0x20C76FD1
#define WARP_CHAR_INTO_CAR                   0x73D3504A
#define WARP_CHAR_FROM_CAR_TO_COORD          0x6A77506A
#define IS_PLAYER_PRESSING_HORN              0x583A7A8B
#define DO_SCREEN_FADE_OUT                   0x65DE621C
#define DO_SCREEN_FADE_IN                    0x04D72200
#define IS_SCREEN_FADED_OUT                  0x59EE3A11
#define CREATE_CAM                           0x694A0DC1
#define DESTROY_CAM                          0x14334EEE
#define SET_CAM_POS                          0x152F6314
#define POINT_CAM_AT_COORD                   0x4496175C
#define SET_CAM_ACTIVE                       0x43E42686
#define SET_CAM_PROPAGATE                    0x44414E60
#define ACTIVATE_SCRIPTED_CAMS               0x3EBE11B9
#define PRINT_STRING_WITH_LITERAL_STRING_NOW 0x0CA539D6
#define CLEAR_PRINTS                         0x1D8C324A
#define SET_PLAYER_CONTROL                   0x1A6203EA
#define IS_PLAYER_CONTROL_ON                 0x30CD2F1F
/* LibertyCityPlates */
#define LCP_TEXT_0123                        0x09DED943
#define LCP_TEXT_4567                        0x09DE9587
#define LCP_TEXT_891011                      0x0E515A91
#define LCP_TEXT_TINT_FLOAT4                 0x3FFFB335
#define LCP_TEXT_LENGTH                      0x46F3540C
#define LCP_TEXT_TEXTURE                     0xF055490B
#define LCP_PLATE_TEXTURE                    0xC621B628
#define LCP_TEXT_SCALE                       0x6C688F74
#define LCP_TEXT_OFFSET                      0x04C26575

static const struct { DWORD h; const char *n; } g_names[] = {
    { GET_PLAYER_ID, "GET_PLAYER_ID" }, { CREATE_CAR, "CREATE_CAR" }, { DELETE_CAR, "DELETE_CAR" },
    { SET_CAR_AS_MISSION_CAR, "SET_CAR_AS_MISSION_CAR" }, { MARK_CAR_AS_NO_LONGER_NEEDED, "MARK_CAR_AS_NO_LONGER_NEEDED" },
    { REQUEST_MODEL, "REQUEST_MODEL" }, { WARP_CHAR_INTO_CAR, "WARP_CHAR_INTO_CAR" },
    { WARP_CHAR_FROM_CAR_TO_COORD, "WARP_CHAR_FROM_CAR_TO_COORD" }, { IS_PLAYER_PRESSING_HORN, "IS_PLAYER_PRESSING_HORN" },
    { CREATE_CAM, "CREATE_CAM" }, { ACTIVATE_SCRIPTED_CAMS, "ACTIVATE_SCRIPTED_CAMS" },
    { GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, "GET_DISPLAY_NAME_FROM_VEHICLE_MODEL" },
    { GET_STRING_FROM_TEXT_FILE, "GET_STRING_FROM_TEXT_FILE" }, { LCP_TEXT_0123, "LibertyCityPlates TEXT" },
    { LCP_PLATE_TEXTURE, "LibertyCityPlates PLATE_TEXTURE" }, { SET_CAR_LIVERY, "SET_CAR_LIVERY" },
};
static const char *cmd_name(DWORD h)
{
    for (unsigned i = 0; i < sizeof(g_names) / sizeof(g_names[0]); i++) if (g_names[i].h == h) return g_names[i].n;
    return "?";
}

static char g_msg[400];
static char g_gxtString[] = "STRING";
static void show_for(const char *text, int ms)
{
    lstrcpynA(g_msg, text, sizeof(g_msg));
    C4(PRINT_STRING_WITH_LITERAL_STRING_NOW, g_gxtString, g_msg, ms, 1);
}
static void show(const char *text) { show_for(text, 5000); }

static int game_paused(void)
{
    return (g_pUserPause && *g_pUserPause) || (g_pScriptPause && *g_pScriptPause);
}

/* ------------------------------------------------------------ crash guard
 * A crash inside a game command jumps back to run_guarded() instead of
 * closing the game; it's logged and the current action is called off. */
typedef void (*StepFn)(void);
static volatile DWORD g_crashCode, g_crashHash;
static void *volatile g_crashAt;
static int g_quiet;

#ifndef LCG_TEST
static void *g_jb[5];
static volatile int g_guard;
static volatile DWORD g_guardTid;
static volatile DWORD g_savedSeh;

static void __attribute__((noreturn, noinline)) recover(void) { __builtin_longjmp(g_jb, 1); }

static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    if (!g_guard || GetCurrentThreadId() != g_guardTid) return EXCEPTION_CONTINUE_SEARCH;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        c != EXCEPTION_ILLEGAL_INSTRUCTION && c != EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    g_guard = 0;
    g_crashCode = c;
    g_crashAt = e->ExceptionRecord->ExceptionAddress;
    g_crashHash = g_lastHash;
    e->ContextRecord->Eip = (DWORD)(void *)recover;
    e->ContextRecord->Esp = (e->ContextRecord->Esp - 64) & ~15u;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static int run_guarded(StepFn fn)
{
    DWORD seh;
    __asm__ volatile("movl %%fs:0, %0" : "=r"(seh));
    g_savedSeh = seh;
    if (__builtin_setjmp(g_jb) == 0) {
        g_guardTid = GetCurrentThreadId();
        g_guard = 1;
        fn();
        g_guard = 0;
        return 1;
    }
    seh = g_savedSeh;
    __asm__ volatile("movl %0, %%fs:0" : : "r"(seh) : "memory");
    g_guard = 0;
    g_lastHash = 0;
    if (!g_quiet) plog("!! crash caught: code %08lX at %p inside %08lX (%s)", g_crashCode, g_crashAt, g_crashHash, cmd_name(g_crashHash));
    return 0;
}
#else
static int run_guarded(StepFn fn) { fn(); return 1; }
#endif

/* ------------------------------------------------------------ settings */
#define MAX_GARAGES 8
#define MAX_SLOTS   64
typedef struct { float x, y, z, h; } Point;
static Point g_garage[MAX_GARAGES];
static int g_ngarage;
static UINT g_keyOut = VK_F1 + 5;     /* F6 */
static int g_slots = 20;
static float g_storeDist = 8, g_outDist = 12, g_exitTurn = 180;
static char g_ideSetting[MAX_PATH];

static UINT parse_key(const char *k, UINT def)
{
    if ((k[0] == 'F' || k[0] == 'f') && k[1] >= '1' && k[1] <= '9') {
        int f = k[1] - '0';
        if (k[2] >= '0' && k[2] <= '9') f = f * 10 + (k[2] - '0');
        if (f >= 1 && f <= 24) return VK_F1 + f - 1;
    }
    if (k[0] && !k[1]) { char c = k[0]; if (c >= 'a' && c <= 'z') c -= 32; return (UINT)(BYTE)c; }
    return def;
}

static int parse_point(const char *v, Point *pt)
{
    float f[4]; int i = 0;
    const char *p = v;
    char t[32];
    while (i < 4 && next_token(&p, t, sizeof(t))) { if (!parse_float(t, &f[i])) return 0; i++; }
    if (i != 4) return 0;
    pt->x = f[0]; pt->y = f[1]; pt->z = f[2]; pt->h = f[3];
    return 1;
}

static float ini_float(const char *key, float def)
{
    char v[32]; float f;
    GetPrivateProfileStringA("LCGarage", key, "", v, sizeof(v), g_iniPath);
    return (v[0] && parse_float(v, &f)) ? f : def;
}

static void load_settings(void)
{
    char v[96], key[16];
    GetPrivateProfileStringA("LCGarage", "TakeOutKey", "F6", v, sizeof(v), g_iniPath);
    g_keyOut = parse_key(v, g_keyOut);
    g_slots = (int)GetPrivateProfileIntA("LCGarage", "Slots", 20, g_iniPath);
    if (g_slots < 1) g_slots = 1;
    if (g_slots > MAX_SLOTS) g_slots = MAX_SLOTS;
    g_storeDist = ini_float("StoreDistance", 8);
    g_outDist = ini_float("TakeOutDistance", 12);
    g_exitTurn = ini_float("ExitTurn", 180);
    GetPrivateProfileStringA("LCGarage", "PlatesIde", "", g_ideSetting, sizeof(g_ideSetting), g_iniPath);
    g_ngarage = 0;
    for (int i = 1; i <= MAX_GARAGES; i++) {
        wsprintfA(key, "Garage%d", i);
        GetPrivateProfileStringA("LCGarage", key, i == 1 ? "-1743.04 330.93 24.95 236.21" : "", v, sizeof(v), g_iniPath);
        if (v[0] && parse_point(v, &g_garage[g_ngarage])) {
            plog("garage %d: %s", g_ngarage + 1, v);
            g_ngarage++;
        }
    }
    plog("take-out key 0x%02X, %d slots, %d garage(s)", g_keyOut, g_slots, g_ngarage);
}

/* ------------------------------------------------------------ plates: vehicles.ide
 * Plate rules read the same way LibertyCityPlates reads them:
 *   license_plates: id parent chance platetxd font sx sy ox oy r g b a format numZones zones...
 *   used_plates:    model id [id ...]
 */
#define FMT_CHAR   100   /* & random letter */
#define FMT_NUM    101   /* ^ random digit */
#define FMT_ANY    102   /* % letter or digit */
#define FMT_AB     103   /* ( random small a/b */
#define PLATE_EMBLEM 0x24
#define PLATE_SPACE  0x25
#define PLATE_A      38
#define PLATE_B      39

typedef struct {
    BYTE have, parent, chance, pic, font, n, nrect;
    float sx, sy, ox, oy, r, g, b, a;
    BYTE fmt[12];
    float rect[4][4];  /* minX maxX minY maxY */
} PlateDef;
static PlateDef g_def[255];

#define MAX_USED 600
typedef struct { DWORD model; BYTE n; BYTE ids[16]; } UsedPlates;
static UsedPlates g_used[MAX_USED];
static int g_nused;

static BYTE g_picList[255], g_fontList[255];
static int g_npic, g_nfont;
static int g_ideLoaded;

static void parse_plate_line(const char *p, const char *first)
{
    char t[16][40]; int k = 0;
    lstrcpynA(t[k++], first, 40);
    while (k < 15 && next_token(&p, t[k], 40)) k++;
    int id, chance, pic, font, nz;
    PlateDef d;
    ZeroMemory(&d, sizeof(d));
    if (k < 15 || !parse_int(t[0], &id) || id < 0 || id > 254) return;
    if (!lstrcmpiA(t[1], "null")) d.parent = 0xFF;
    else { int par; if (!parse_int(t[1], &par) || par < 0 || par > 254) d.parent = 0xFF; else d.parent = (BYTE)par; }
    if (!parse_int(t[2], &chance)) chance = 0;
    d.chance = (BYTE)chance;
    if (!parse_int(t[3], &pic) || pic < 0 || pic > 254) pic = 0;
    if (!parse_int(t[4], &font) || font < 0 || font > 254) font = 0;
    d.pic = (BYTE)pic; d.font = (BYTE)font;
    float *fv[8] = { &d.sx, &d.sy, &d.ox, &d.oy, &d.r, &d.g, &d.b, &d.a };
    for (int i = 0; i < 8; i++) if (!parse_float(t[5 + i], fv[i])) *fv[i] = 0;
    const char *f = t[13];
    if (lstrcmpiA(f, "null")) {
        for (int i = 0; f[i] && d.n < 12; i++) {
            char c = f[i];
            BYTE v;
            if (c == '&') v = FMT_CHAR;
            else if (c == '^') v = FMT_NUM;
            else if (c == '%') v = FMT_ANY;
            else if (c == '(') v = FMT_AB;
            else if (c == '*') v = PLATE_EMBLEM;
            else if (c == '#') v = PLATE_SPACE;
            else {
                if (c >= 'a' && c <= 'z') c -= 32;
                if (c >= 'A' && c <= 'Z') v = (BYTE)(c - 'A' + 10);
                else if (c >= '0' && c <= '9') v = (BYTE)(c - '0');
                else v = PLATE_SPACE;
            }
            d.fmt[d.n++] = v;
        }
    }
    if (!parse_int(t[14], &nz) || nz < 0) nz = 0;
    for (int z = 0; z < nz; z++) {
        for (int j = 0; j < 4; j++) {
            char tt[40];
            if (!next_token(&p, tt, sizeof(tt)) || !parse_float(tt, &d.rect[z < 4 ? z : 3][j])) d.rect[z < 4 ? z : 3][j] = 0;
        }
    }
    d.nrect = (BYTE)(nz > 4 ? 4 : nz);
    d.have = 1;
    g_def[id] = d;     /* a later line with the same id wins, as in LibertyCityPlates */
}

static void parse_used_line(const char *p, const char *name)
{
    DWORD model;
    if (!lstrcmpiA(name, "hash:")) return;
    if (name[0] == 'h' && name[1] == 'a' && name[2] == 's' && name[3] == 'h' && name[4] == ':') {
        int v; if (!parse_int(name + 5, &v)) return; model = (DWORD)v;
    } else model = joaat(name);
    UsedPlates *u = NULL;
    for (int i = 0; i < g_nused; i++) if (g_used[i].model == model) { u = &g_used[i]; break; }
    if (!u) { if (g_nused >= MAX_USED) return; u = &g_used[g_nused++]; u->model = model; u->n = 0; }
    char t[16]; int id;
    while (next_token(&p, t, sizeof(t))) if (parse_int(t, &id) && id >= 0 && id <= 254 && u->n < 16) u->ids[u->n++] = (BYTE)id;
}

static void load_ide(void)
{
    char exeDir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, exeDir, MAX_PATH);
    char *slash = exeDir;
    for (char *s = exeDir; *s; s++) if (*s == '\\' || *s == '/') slash = s;
    *slash = 0;
    const char *tryPaths[3]; char p0[MAX_PATH], p1[MAX_PATH], p2[MAX_PATH];
    int n = 0;
    if (g_ideSetting[0]) {
        if (g_ideSetting[1] == ':' || g_ideSetting[0] == '\\') lstrcpynA(p0, g_ideSetting, MAX_PATH);
        else wsprintfA(p0, "%s\\%s", exeDir, g_ideSetting);
        tryPaths[n++] = p0;
    }
    wsprintfA(p1, "%s\\update\\common\\data\\vehicles.ide", exeDir); tryPaths[n++] = p1;
    wsprintfA(p2, "%s\\common\\data\\vehicles.ide", exeDir); tryPaths[n++] = p2;
    char *buf = NULL;
    for (int i = 0; i < n && !buf; i++) { buf = read_file(tryPaths[i]); if (buf) lstrcpynA(path, tryPaths[i], MAX_PATH); }
    ZeroMemory(g_def, sizeof(g_def));
    g_nused = 0;
    if (!buf) { plog("vehicles.ide not found: random plates will be plain"); return; }
    int section = 0, nplates = 0;   /* 1 = license_plates, 2 = used_plates */
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end; *end = 0;
        const char *p = line;
        char t[40];
        /* a line whose first word starts with # is a comment (inside a format, # means a space) */
        if (next_token(&p, t, sizeof(t)) && t[0] != '#') {
            if (!lstrcmpiA(t, "license_plates")) section = 1;
            else if (!lstrcmpiA(t, "used_plates")) section = 2;
            else if (!lstrcmpiA(t, "end")) section = 0;
            else if (section == 1) { parse_plate_line(p, t); nplates++; }
            else if (section == 2) parse_used_line(p, t);
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, buf);

    BYTE seenPic[255] = { 0 }, seenFont[255] = { 0 };
    g_npic = g_nfont = 0;
    for (int i = 0; i < 255; i++) if (g_def[i].have && g_def[i].n) { seenPic[g_def[i].pic] = 1; seenFont[g_def[i].font] = 1; }
    for (int i = 0; i < 255; i++) { if (seenPic[i]) g_picList[g_npic++] = (BYTE)i; if (seenFont[i]) g_fontList[g_nfont++] = (BYTE)i; }
    g_ideLoaded = 1;
    plog("vehicles.ide: %d plate lines, %d used_plates models, %d pictures, %d fonts (%s)", nplates, g_nused, g_npic, g_nfont, path);
}

/* ------------------------------------------------------------ plates */
typedef struct {
    int pic, font;
    float sx, sy, ox, oy, r, g, b, a;
    int len;
    BYTE code[12];
} Plate;

static void plate_style_from(Plate *pl, const PlateDef *d)
{
    pl->pic = d->pic; pl->font = d->font;
    pl->sx = d->sx; pl->sy = d->sy; pl->ox = d->ox; pl->oy = d->oy;
    pl->r = d->r; pl->g = d->g; pl->b = d->b; pl->a = d->a;
}

static void plate_text_from(Plate *pl, const PlateDef *d)
{
    pl->len = d->n;
    for (int i = 0; i < d->n; i++) {
        BYTE f = d->fmt[i];
        pl->code[i] = f == FMT_ANY ? (BYTE)rnd_range(0, 35) : f == FMT_NUM ? (BYTE)rnd_range(0, 9) :
                      f == FMT_CHAR ? (BYTE)rnd_range(10, 35) : f == FMT_AB ? (BYTE)rnd_range(PLATE_A, PLATE_B) : f;
    }
}

/* LibertyCityPlates' choice: follow parents by chance and by zone */
static int resolve_def(int index, float x, float y, int depth)
{
    const PlateDef *d = &g_def[index];
    if (!d->have || depth > 40) return index;
    if (d->parent != 0xFF) {
        if (d->chance != 0xFF && (int)d->chance < (int)(rnd() % 100)) return resolve_def(d->parent, x, y, depth + 1);
        for (int i = 0; i < d->nrect; i++) {
            const float *r = d->rect[i];
            if (!(x > r[0] && x < r[1] && y > r[2] && y < r[3])) return resolve_def(d->parent, x, y, depth + 1);
        }
    }
    return index;
}

static void plate_random(Plate *pl, DWORD model, int isBike, float x, float y)
{
    int start = isBike ? 1 : 0;
    for (int i = 0; i < g_nused; i++)
        if (g_used[i].model == model && g_used[i].n) { start = g_used[i].ids[rnd() % g_used[i].n]; break; }
    int id = resolve_def(start, x, y, 0);
    const PlateDef *d = &g_def[id];
    if (!d->have || !d->n) {              /* no usable rules: a plain plate */
        PlateDef plain;
        ZeroMemory(&plain, sizeof(plain));
        BYTE f[8] = { FMT_CHAR, FMT_CHAR, FMT_CHAR, PLATE_SPACE, FMT_NUM, FMT_NUM, FMT_NUM, FMT_NUM };
        memcpy(plain.fmt, f, 8); plain.n = 8;
        plain.sx = 1.25f; plain.sy = 0.43f; plain.oy = -0.07f; plain.a = 1;
        plate_style_from(pl, d->have ? d : &plain);
        plate_text_from(pl, &plain);
        return;
    }
    plate_style_from(pl, d);
    plate_text_from(pl, d);
}

/* the first rule that uses a picture: its layout and a text in its format */
static const PlateDef *def_for_pic(int pic)
{
    for (int i = 0; i < 255; i++) if (g_def[i].have && g_def[i].n && g_def[i].pic == pic) return &g_def[i];
    return NULL;
}

static char code_char(BYTE c)
{
    if (c < 10) return (char)('0' + c);
    if (c < 36) return (char)('A' + c - 10);
    if (c == PLATE_EMBLEM) return '*';
    if (c == PLATE_A) return 'a';
    if (c == PLATE_B) return 'b';
    return ' ';
}
static int char_code(char c, BYTE *out)
{
    if (c >= '0' && c <= '9') { *out = (BYTE)(c - '0'); return 1; }
    if (c >= 'A' && c <= 'Z') { *out = (BYTE)(c - 'A' + 10); return 1; }
    if (c == '*') { *out = PLATE_EMBLEM; return 1; }
    if (c == 'a') { *out = PLATE_A; return 1; }
    if (c == 'b') { *out = PLATE_B; return 1; }
    if (c == ' ' || c == '_') { *out = PLATE_SPACE; return 1; }
    return 0;
}

static void plate_text(const Plate *pl, char *out)
{
    int i;
    for (i = 0; i < pl->len && i < 12; i++) out[i] = code_char(pl->code[i]);
    out[i] = 0;
}

static void apply_plate(DWORD car, const Plate *pl)
{
    if (!get_handler(LCP_TEXT_0123)) return;
    BYTE ch[12];
    for (int i = 0; i < 12; i++) ch[i] = i < pl->len ? pl->code[i] : PLATE_SPACE;
    C2(LCP_PLATE_TEXTURE, car, pl->pic);
    C3(LCP_TEXT_SCALE, car, F(pl->sx), F(pl->sy));
    C3(LCP_TEXT_OFFSET, car, F(pl->ox), F(pl->oy));
    C5(LCP_TEXT_TINT_FLOAT4, car, F(pl->r), F(pl->g), F(pl->b), F(pl->a));
    C2(LCP_TEXT_TEXTURE, car, pl->font);
    C5(LCP_TEXT_0123, car, ch[0], ch[1], ch[2], ch[3]);
    C5(LCP_TEXT_4567, car, ch[4], ch[5], ch[6], ch[7]);
    C5(LCP_TEXT_891011, car, ch[8], ch[9], ch[10], ch[11]);
    C2(LCP_TEXT_LENGTH, car, pl->len);
}

/* ------------------------------------------------------------ the garage's cars */
typedef struct {
    int used;
    DWORD model;
    int col[4];
    BYTE extra[10];     /* 1..9 */
    int livery;
    int away;           /* taken out and not brought back */
    Plate plate;
    DWORD handle;       /* the car while it's out (this session only) */
} Slot;
static Slot g_slot[MAX_SLOTS];

static void save_cars(void)
{
    HANDLE h = CreateFileA(g_carsPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { plog("couldn't write %s", g_carsPath); return; }
    char line[400], f[8][24];
    DWORD w;
    lstrcpyA(line, "# LCGarage cars: model colours extras livery away picture font scale offset tint plate\r\n");
    WriteFile(h, line, lstrlenA(line), &w, NULL);
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot *s = &g_slot[i];
        if (!s->used) continue;
        char ex[10], text[13];
        for (int k = 0; k < 9; k++) ex[k] = s->extra[k + 1] ? '1' : '0';
        ex[9] = 0;
        int k;
        for (k = 0; k < s->plate.len && k < 12; k++) { char c = code_char(s->plate.code[k]); text[k] = c == ' ' ? '_' : c; }
        text[k] = 0;
        if (!k) lstrcpyA(text, "=");
        wsprintfA(line, "car %08lX %d %d %d %d %s %d %d %d %d %s %s %s %s %s %s %s %s %s\r\n",
                  s->model, s->col[0], s->col[1], s->col[2], s->col[3], ex, s->livery, s->away,
                  s->plate.pic, s->plate.font, ffn(f[0], s->plate.sx, 4), ffn(f[1], s->plate.sy, 4),
                  ffn(f[2], s->plate.ox, 4), ffn(f[3], s->plate.oy, 4), ffn(f[4], s->plate.r, 4),
                  ffn(f[5], s->plate.g, 4), ffn(f[6], s->plate.b, 4), ffn(f[7], s->plate.a, 4), text);
        WriteFile(h, line, lstrlenA(line), &w, NULL);
    }
    CloseHandle(h);
}

static void load_cars(void)
{
    ZeroMemory(g_slot, sizeof(g_slot));
    char *buf = read_file(g_carsPath);
    if (!buf) { plog("no LCGarage.cars yet: the garage is empty"); return; }
    int n = 0, bad = 0;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char save = *end; *end = 0;
        const char *p = line;
        char t[20][32]; int k = 0;
        while (k < 20 && next_token(&p, t[k], 32)) k++;
        if (k >= 1 && !lstrcmpA(t[0], "car")) {
            Slot s;
            ZeroMemory(&s, sizeof(s));
            int ok = k == 20 && parse_hex(t[1], &s.model);
            for (int i = 0; i < 4 && ok; i++) ok = parse_int(t[2 + i], &s.col[i]);
            if (ok && lstrlenA(t[6]) == 9) for (int i = 0; i < 9; i++) s.extra[i + 1] = t[6][i] == '1';
            else ok = 0;
            ok = ok && parse_int(t[7], &s.livery) && parse_int(t[8], &s.away) &&
                 parse_int(t[9], &s.plate.pic) && parse_int(t[10], &s.plate.font);
            float *fv[8] = { &s.plate.sx, &s.plate.sy, &s.plate.ox, &s.plate.oy, &s.plate.r, &s.plate.g, &s.plate.b, &s.plate.a };
            for (int i = 0; i < 8 && ok; i++) ok = parse_float(t[11 + i], fv[i]);
            if (ok && lstrcmpA(t[19], "=")) {
                for (int i = 0; t[19][i] && s.plate.len < 12; i++) {
                    BYTE c;
                    if (char_code(t[19][i], &c)) s.plate.code[s.plate.len++] = c;
                }
            }
            if (ok && n < MAX_SLOTS) { s.used = 1; g_slot[n++] = s; }
            else bad++;
        }
        *end = save;
        line = *end ? end + 1 : end;
    }
    HeapFree(GetProcessHeap(), 0, buf);
    plog("garage: %d car(s) loaded%s", n, bad ? ", some lines couldn't be read" : "");
}

static int count_cars(void) { int n = 0; for (int i = 0; i < MAX_SLOTS; i++) n += g_slot[i].used; return n; }

static int free_slot(void)
{
    if (count_cars() >= g_slots) return -1;
    for (int i = 0; i < MAX_SLOTS; i++) if (!g_slot[i].used) return i;
    return -1;
}

/* ------------------------------------------------------------ game helpers */
static DWORD g_player, g_ped;
static int get_player(void)
{
    g_missing = 0;
    g_player = C1(CONVERT_INT_TO_PLAYERINDEX, C0(GET_PLAYER_ID));
    if (g_missing || !C1(IS_PLAYER_PLAYING, g_player)) return 0;
    g_ped = 0;
    C2(GET_PLAYER_CHAR, g_player, &g_ped);
    return g_ped != 0;
}
static DWORD player_car(void)
{
    DWORD car = 0;
    if (C1(IS_CHAR_IN_ANY_CAR, g_ped)) C2(GET_CAR_CHAR_IS_USING, g_ped, &car);
    return car;
}
static int near_garage(float x, float y, float z, float max)
{
    int best = -1; float bd = max;
    for (int i = 0; i < g_ngarage; i++) {
        float d = dist3(x, y, z, g_garage[i].x, g_garage[i].y, g_garage[i].z);
        if (d <= bd) { bd = d; best = i; }
    }
    return best;
}

/* is this slot's car out in the world right now? */
static int slot_car_out(Slot *s)
{
    if (!s->handle) return 0;
    DWORD m = 0;
    if (!C1(DOES_VEHICLE_EXIST, s->handle) || C1(IS_CAR_DEAD, s->handle)) { s->handle = 0; return 0; }
    C2(GET_CAR_MODEL, s->handle, &m);
    if (m != s->model) { s->handle = 0; return 0; }
    return 1;
}

static void car_name(DWORD model, char *out, int max)
{
    out[0] = 0;
    char *key = (char *)C1(GET_DISPLAY_NAME_FROM_VEHICLE_MODEL, model);
    if (!key || IsBadStringPtrA(key, 32) || !key[0]) { wsprintfA(out, "car %08lX", model); return; }
    char *name = NULL;
    if (C1(DOES_TEXT_LABEL_EXIST, key)) name = (char *)C1(GET_STRING_FROM_TEXT_FILE, key);
    const char *src = (name && !IsBadStringPtrA(name, 64) && name[0]) ? name : key;
    int n = 0;
    for (; src[n] && n < max - 1; n++) out[n] = (src[n] == '~') ? '-' : src[n];
    out[n] = 0;
}

static int g_cam, g_controlWasOn;
static void controls_off(void)
{
    g_controlWasOn = C1(IS_PLAYER_CONTROL_ON, g_player) ? 1 : 0;
    if (g_controlWasOn) C2(SET_PLAYER_CONTROL, g_player, 0);
}
static void controls_back(void)
{
    if (g_controlWasOn) C2(SET_PLAYER_CONTROL, g_player, 1);
    g_controlWasOn = 0;
}

/* script camera at an offset from the car, looking at another offset
   (x = right, y = forward, z = up, in the car's own frame) */
static void camera_on_car(DWORD car, float cx, float cy, float cz, float lx, float ly, float lz)
{
    float x, y, z, tx, ty, tz;
    C7(GET_OFFSET_FROM_CAR_IN_WORLD_COORDS, car, F(cx), F(cy), F(cz), &x, &y, &z);
    C7(GET_OFFSET_FROM_CAR_IN_WORLD_COORDS, car, F(lx), F(ly), F(lz), &tx, &ty, &tz);
    if (!g_cam) C2(CREATE_CAM, 14, &g_cam);
    C4(SET_CAM_POS, g_cam, F(x), F(y), F(z));
    C4(POINT_CAM_AT_COORD, g_cam, F(tx), F(ty), F(tz));
    C2(SET_CAM_ACTIVE, g_cam, 1);
    C2(SET_CAM_PROPAGATE, g_cam, 1);
    C2(ACTIVATE_SCRIPTED_CAMS, 1, 1);
}
static void camera_off(void)
{
    if (!g_cam) return;
    C2(ACTIVATE_SCRIPTED_CAMS, 0, 0);
    C1(DESTROY_CAM, g_cam);
    g_cam = 0;
}

/* ------------------------------------------------------------ keys */
static BYTE g_prev[256];
static int pressed(int vk)
{
    int down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    int was = g_prev[vk & 0xFF];
    g_prev[vk & 0xFF] = (BYTE)down;
    return down && !was;
}
static void sync_keys(void) { for (int k = 0; k < 256; k++) g_prev[k] = (GetAsyncKeyState(k) & 0x8000) ? 1 : 0; }

static int step(int value, int dir, const BYTE *list, int n)
{
    if (!n) return value;
    int i = -1;
    for (int k = 0; k < n; k++) if (list[k] == value) { i = k; break; }
    if (i < 0) return dir > 0 ? list[0] : list[n - 1];
    return list[(i + dir + n) % n];
}

/* ------------------------------------------------------------ what's going on */
enum { M_IDLE, M_STORE_PLATE, M_STORE_IN, M_LIST, M_OUT };
static int g_mode, g_stage;
static DWORD g_t0, g_lastShown;
static DWORD g_car;            /* the car being stored / brought out */
static int g_slotIdx;          /* its slot */
static int g_known;            /* storing a car that came from the garage */
static int g_garageIdx;
static Plate g_pend;           /* plate being typed */
static BYTE g_randCode[12]; static int g_randLen;
static char g_typed[13]; static int g_typedLen;
static int g_fontPicked;
static int g_fading;
static int g_sel, g_delArmed;
static char g_names_[MAX_SLOTS][40];

static void finish(void) { g_mode = M_IDLE; g_stage = 0; }

static void abandon(const char *why)
{
    plog("%s", why);
    camera_off();
    C1(DO_SCREEN_FADE_IN, 300);
    controls_back();
    finish();
}

/* ---- storing */
static void show_typing(void)
{
    char msg[400], rnd_[13], pl[13];
    int i;
    for (i = 0; i < g_randLen; i++) rnd_[i] = code_char(g_randCode[i]);
    rnd_[i] = 0;
    for (i = 0; i < g_typedLen; i++) pl[i] = g_typed[i];
    pl[i] = 0;
    wsprintfA(msg, "Plate for this car: %s_  (leave blank for %s)~n~Type it, PgUp/PgDn picture, Home/End font. Enter = store, Delete = cancel.",
              pl, rnd_);
    show_for(msg, 30000);
    g_lastShown = GetTickCount();
}

static void refresh_pending_text(void)
{
    if (g_typedLen) {
        g_pend.len = 0;
        for (int i = 0; i < g_typedLen; i++) { BYTE c; if (char_code(g_typed[i], &c)) g_pend.code[g_pend.len++] = c; }
    } else {
        g_pend.len = g_randLen;
        memcpy(g_pend.code, g_randCode, 12);
    }
}

static void start_store(DWORD car, int garage)
{
    float sp = 0;
    C2(GET_CAR_SPEED, car, &sp);
    if (sp > 3.0f) return;      /* only once you've stopped */
    DWORD model = 0;
    C2(GET_CAR_MODEL, car, &model);
    int known = -1;
    for (int i = 0; i < MAX_SLOTS && known < 0; i++)
        if (g_slot[i].used && g_slot[i].handle == car && slot_car_out(&g_slot[i])) known = i;
    if (known < 0) {
        /* a car taken out earlier (e.g. before loading a save) that wasn't brought back:
           same model and colours means it's that car */
        int col[4];
        C3(GET_CAR_COLOURS, car, &col[0], &col[1]);
        C3(GET_EXTRA_CAR_COLOURS, car, &col[2], &col[3]);
        for (int i = 0; i < MAX_SLOTS && known < 0; i++) {
            Slot *s = &g_slot[i];
            if (s->used && s->away && !slot_car_out(s) && s->model == model &&
                s->col[0] == col[0] && s->col[1] == col[1] && s->col[2] == col[2] && s->col[3] == col[3]) known = i;
        }
    }
    int idx = known >= 0 ? known : free_slot();
    if (idx < 0) {
        char msg[160];
        wsprintfA(msg, "The garage is full (%d cars). Take one out and remove it from the list to make room.", g_slots);
        show(msg);
        return;
    }
    g_car = car; g_slotIdx = idx; g_known = known >= 0; g_garageIdx = garage;
    controls_off();
    plog("store: car %lu model %08lX into slot %d (%s)", car, model, idx + 1, g_known ? "back from outside, keeps its plate" : "new");
    if (g_known) {
        camera_on_car(car, 3.5f, -6.0f, 2.0f, 0.0f, 3.5f, 0.8f);
        g_mode = M_STORE_IN; g_stage = 0; g_t0 = GetTickCount(); g_fading = 0;
        return;
    }
    /* new car: type its plate while looking at the back of it */
    float x = 0, y = 0, z = 0;
    C4(GET_CAR_COORDINATES, car, &x, &y, &z);
    plate_random(&g_pend, model, C1(IS_THIS_MODEL_A_BIKE, model) != 0, x, y);
    g_randLen = g_pend.len;
    memcpy(g_randCode, g_pend.code, 12);
    g_typedLen = 0; g_typed[0] = 0; g_fontPicked = 0;
    apply_plate(car, &g_pend);
    camera_on_car(car, 1.3f, -4.6f, 1.1f, 0.0f, -2.0f, 0.4f);
    sync_keys();
    g_mode = M_STORE_PLATE;
    show_typing();
}

static void poll_typing(void)
{
    if (!C1(DOES_VEHICLE_EXIST, g_car)) { abandon("store: the car is gone"); return; }
    int changed = GetTickCount() - g_lastShown > 25000;
    if (pressed(VK_RETURN)) {
        refresh_pending_text();
        char t[13]; plate_text(&g_pend, t);
        plog("store: plate \"%s\", picture %d, font %d", t, g_pend.pic, g_pend.font);
        C0(CLEAR_PRINTS);
        camera_on_car(g_car, 3.5f, -6.0f, 2.0f, 0.0f, 3.5f, 0.8f);
        g_mode = M_STORE_IN; g_stage = 0; g_t0 = GetTickCount(); g_fading = 0;
        return;
    }
    if (pressed(VK_DELETE)) {
        C0(CLEAR_PRINTS);
        abandon("store: cancelled");
        show("Not stored.");
        return;
    }
    int textChanged = 0;
    if (pressed(VK_BACK) && g_typedLen > 0) { g_typed[--g_typedLen] = 0; textChanged = 1; }
    char add = 0;
    for (int k = 'A'; k <= 'Z' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = '0'; k <= '9' && !add; k++) if (pressed(k)) add = (char)k;
    for (int k = 0; k <= 9 && !add; k++) if (pressed(VK_NUMPAD0 + k)) add = (char)('0' + k);
    if (!add && pressed(VK_SPACE)) add = ' ';
    if (!add && pressed(VK_MULTIPLY)) add = '*';
    if (add && g_typedLen < 12) { g_typed[g_typedLen++] = add; g_typed[g_typedLen] = 0; textChanged = 1; }
    int picDir = pressed(VK_PRIOR) ? 1 : pressed(VK_NEXT) ? -1 : 0;
    if (picDir && g_npic) {
        int font = g_pend.font;
        int pic = step(g_pend.pic, picDir, g_picList, g_npic);
        const PlateDef *d = def_for_pic(pic);
        if (d) {
            plate_style_from(&g_pend, d);
            Plate r = g_pend; plate_text_from(&r, d);         /* blank now means a plate in this picture's format */
            g_randLen = r.len; memcpy(g_randCode, r.code, 12);
        } else g_pend.pic = pic;
        if (g_fontPicked) g_pend.font = font;
        textChanged = 1;
    }
    int fontDir = pressed(VK_HOME) ? 1 : pressed(VK_END) ? -1 : 0;
    if (fontDir && g_nfont) { g_pend.font = step(g_pend.font, fontDir, g_fontList, g_nfont); g_fontPicked = 1; textChanged = 1; }
    if (textChanged) { refresh_pending_text(); apply_plate(g_car, &g_pend); changed = 1; }
    if (changed) show_typing();
}

static void step_store_in(void)
{
    DWORD t = GetTickCount() - g_t0;
    if (g_stage == 0) {
        if (t >= 900 && !g_fading) { C1(DO_SCREEN_FADE_OUT, 800); g_fading = 1; }
        if (t < 1700) return;
        if (!C0(IS_SCREEN_FADED_OUT) && t < 3500) return;
        DWORD car = g_car;
        if (!C1(DOES_VEHICLE_EXIST, car)) { abandon("store: the car is gone"); return; }
        Slot *s = &g_slot[g_slotIdx];
        Slot n;
        ZeroMemory(&n, sizeof(n));
        C2(GET_CAR_MODEL, car, &n.model);
        C3(GET_CAR_COLOURS, car, &n.col[0], &n.col[1]);
        C3(GET_EXTRA_CAR_COLOURS, car, &n.col[2], &n.col[3]);
        for (int i = 1; i <= 9; i++) n.extra[i] = C2(IS_VEHICLE_EXTRA_TURNED_ON, car, i) ? 1 : 0;
        n.livery = -1;
        C2(GET_CAR_LIVERY, car, &n.livery);
        n.plate = g_known ? s->plate : g_pend;
        n.used = 1;
        *s = n;
        save_cars();
        float h = 0, ox = 0, oy = 0, oz = 0;
        C2(GET_CAR_HEADING, car, &h);
        C7(GET_OFFSET_FROM_CAR_IN_WORLD_COORDS, car, F(-2.2f), F(-3.0f), F(0), &ox, &oy, &oz);
        C4(WARP_CHAR_FROM_CAR_TO_COORD, g_ped, F(ox), F(oy), F(oz));
        C2(SET_CHAR_HEADING, g_ped, F(wrap360(h + 180.0f)));
        g_t0 = GetTickCount(); g_stage = 1;
    } else if (g_stage == 1) {
        if (t < 300) return;
        DWORD car = g_car;
        if (C1(DOES_VEHICLE_EXIST, car)) {
            C1(SET_CAR_AS_MISSION_CAR, car);
            C1(DELETE_CAR, &car);
        }
        camera_off();
        C1(DO_SCREEN_FADE_IN, 600);
        controls_back();
        char msg[160], pl[13], name[40];
        plate_text(&g_slot[g_slotIdx].plate, pl);
        car_name(g_slot[g_slotIdx].model, name, sizeof(name));
        wsprintfA(msg, "%s (%s) is in the garage. %d of %d spaces used.", name, pl, count_cars(), g_slots);
        show(msg);
        plog("store: done, %s %s in slot %d", name, pl, g_slotIdx + 1);
        finish();
    }
}

/* ---- the list */
static int g_order[MAX_SLOTS], g_norder;

static void show_list(void)
{
    char msg[400], pl[13];
    if (!g_norder) { show("The garage is empty."); return; }
    Slot *s = &g_slot[g_order[g_sel]];
    plate_text(&s->plate, pl);
    const char *state = slot_car_out(s) ? "  (out)" : "";
    if (g_delArmed)
        wsprintfA(msg, "Remove %s (%s) from the garage for good?~n~Delete again = remove, anything else = keep it.", g_names_[g_order[g_sel]], pl);
    else
        wsprintfA(msg, "Garage %d/%d:  %s  -  %s%s~n~Up/Down choose, Enter take out, Delete remove, Backspace close.",
                  g_sel + 1, g_norder, g_names_[g_order[g_sel]], pl, state);
    show_for(msg, 30000);
    g_lastShown = GetTickCount();
}

static void open_list(int garage)
{
    g_norder = 0;
    for (int i = 0; i < MAX_SLOTS; i++) if (g_slot[i].used) {
        g_order[g_norder++] = i;
        if (C1(IS_THIS_MODEL_A_VEHICLE, g_slot[i].model)) car_name(g_slot[i].model, g_names_[i], sizeof(g_names_[i]));
        else wsprintfA(g_names_[i], "(not in this episode)");
    }
    if (!g_norder) { show("The garage is empty. Honk at the door in a car to store it."); return; }
    if (g_sel >= g_norder) g_sel = 0;
    g_garageIdx = garage;
    g_delArmed = 0;
    controls_off();
    sync_keys();
    g_mode = M_LIST;
    plog("list opened: %d car(s)", g_norder);
    show_list();
}

static void close_list(void)
{
    C0(CLEAR_PRINTS);
    controls_back();
    finish();
}

static void start_out(int slot);

static void poll_list(void)
{
    int changed = GetTickCount() - g_lastShown > 25000;
    int up = pressed(VK_UP) | pressed(VK_PRIOR), down = pressed(VK_DOWN) | pressed(VK_NEXT);
    int enter = pressed(VK_RETURN), del = pressed(VK_DELETE), back = pressed(VK_BACK), key = pressed((int)g_keyOut);
    if (g_delArmed) {
        if (del) {
            int i = g_order[g_sel];
            char pl[13]; plate_text(&g_slot[i].plate, pl);
            plog("removed slot %d (%s %s)", i + 1, g_names_[i], pl);
            ZeroMemory(&g_slot[i], sizeof(g_slot[i]));
            save_cars();
            for (int k = g_sel; k < g_norder - 1; k++) g_order[k] = g_order[k + 1];
            g_norder--;
            if (g_sel >= g_norder) g_sel = g_norder ? g_norder - 1 : 0;
            g_delArmed = 0;
            if (!g_norder) { close_list(); show("Removed. The garage is empty now."); return; }
            show_list();
        } else if (up || down || enter || back || key) { g_delArmed = 0; show_list(); }
        else if (changed) show_list();
        return;
    }
    if (back || key) { close_list(); return; }
    if (up) { g_sel = (g_sel + g_norder - 1) % g_norder; changed = 1; }
    if (down) { g_sel = (g_sel + 1) % g_norder; changed = 1; }
    if (del) { g_delArmed = 1; changed = 1; }
    if (enter) {
        Slot *s = &g_slot[g_order[g_sel]];
        if (slot_car_out(s)) { show("That car is already out. Bring it back first (or lose it: then it's back here)."); g_lastShown = GetTickCount() - 20000; return; }
        if (!C1(IS_THIS_MODEL_A_VEHICLE, s->model)) { show("That car doesn't exist in this episode."); g_lastShown = GetTickCount() - 20000; return; }
        C0(CLEAR_PRINTS);
        start_out(g_order[g_sel]);
        return;
    }
    if (changed) show_list();
}

/* ---- taking a car out */
static void start_out(int slot)
{
    g_slotIdx = slot;
    C1(REQUEST_MODEL, g_slot[slot].model);
    C1(DO_SCREEN_FADE_OUT, 500);
    g_mode = M_OUT; g_stage = 0; g_t0 = GetTickCount();
    plog("out: slot %d, model %08lX", slot + 1, g_slot[slot].model);
}

static void step_out(void)
{
    Slot *s = &g_slot[g_slotIdx];
    DWORD t = GetTickCount() - g_t0;
    if (g_stage == 0) {
        int loaded = C1(HAS_MODEL_LOADED, s->model) != 0;
        int faded = C0(IS_SCREEN_FADED_OUT) || t > 2000;
        if (!(loaded && faded)) {
            if (t > 10000) { abandon("out: the model didn't load"); show("The car couldn't be loaded."); }
            return;
        }
        Point *g = &g_garage[g_garageIdx];
        C4(CLEAR_AREA_OF_CARS, F(g->x), F(g->y), F(g->z), F(4.0f));
        DWORD car = 0;
        C6(CREATE_CAR, s->model, F(g->x), F(g->y), F(g->z), &car, 1);
        if (!car || !C1(DOES_VEHICLE_EXIST, car)) { C1(MARK_MODEL_AS_NO_LONGER_NEEDED, s->model); abandon("out: CREATE_CAR made no car"); return; }
        C2(SET_CAR_HEADING, car, F(wrap360(g->h + g_exitTurn)));
        C1(SET_CAR_ON_GROUND_PROPERLY, car);
        if (s->col[0] >= 0) {
            C3(CHANGE_CAR_COLOUR, car, s->col[0], s->col[1]);
            C3(SET_EXTRA_CAR_COLOURS, car, s->col[2], s->col[3]);
        }
        for (int i = 1; i <= 9; i++) C3(TURN_OFF_VEHICLE_EXTRA, car, i, s->extra[i] ? 0 : 1);
        if (s->livery >= 0) C2(SET_CAR_LIVERY, car, s->livery);
        C2(SET_VEHICLE_DIRT_LEVEL, car, F(0));
        apply_plate(car, &s->plate);
        C1(MARK_MODEL_AS_NO_LONGER_NEEDED, s->model);
        C2(WARP_CHAR_INTO_CAR, g_ped, car);
        C3(SET_CAR_ENGINE_ON, car, 1, 1);
        camera_on_car(car, -3.5f, 7.5f, 1.8f, 0.0f, -1.0f, 0.6f);
        C1(DO_SCREEN_FADE_IN, 500);
        g_car = car;
        s->handle = car;
        s->away = 1;
        save_cars();
        g_t0 = GetTickCount(); g_stage = 1;
    } else if (g_stage == 1) {
        if (t < 2200) return;
        camera_off();
        controls_back();
        if (C1(DOES_VEHICLE_EXIST, g_car)) apply_plate(g_car, &s->plate);   /* once more, in case it was set up late */
        char msg[120], pl[13], name[40];
        plate_text(&s->plate, pl);
        car_name(s->model, name, sizeof(name));
        wsprintfA(msg, "%s (%s)", name, pl);
        show_for(msg, 3000);
        g_t0 = GetTickCount(); g_stage = 2;
    } else if (g_stage == 2) {
        if (t < 1500) return;
        /* from now on it's a normal car: if it's wrecked or lost, it's back in the garage */
        if (C1(DOES_VEHICLE_EXIST, g_car)) { DWORD car = g_car; C1(MARK_CAR_AS_NO_LONGER_NEEDED, &car); }
        plog("out: done");
        finish();
    }
}

/* ---- horn and key */
static int g_hornWas;
static DWORD g_lastCar;

/* a garage car that's out but we lost track of (it came back with a saved game,
   and LibertyCityPlates gave it a new plate): when you get in, it gets its plate back */
static void claim_car(DWORD car)
{
    for (int i = 0; i < MAX_SLOTS; i++) if (g_slot[i].used && g_slot[i].handle == car) return;
    DWORD model = 0; int col[4];
    C2(GET_CAR_MODEL, car, &model);
    C3(GET_CAR_COLOURS, car, &col[0], &col[1]);
    C3(GET_EXTRA_CAR_COLOURS, car, &col[2], &col[3]);
    for (int i = 0; i < MAX_SLOTS; i++) {
        Slot *s = &g_slot[i];
        if (s->used && s->away && s->model == model && !slot_car_out(s) &&
            s->col[0] == col[0] && s->col[1] == col[1] && s->col[2] == col[2] && s->col[3] == col[3]) {
            s->handle = car;
            apply_plate(car, &s->plate);
            char pl[13]; plate_text(&s->plate, pl);
            plog("recognised garage car (slot %d) and put its plate %s back", i + 1, pl);
            return;
        }
    }
}

static void check_world(void)
{
    if (!get_player()) { g_hornWas = 0; return; }
    DWORD car = player_car();
    if (car && car != g_lastCar) claim_car(car);
    g_lastCar = car;
    int horn = car && C1(IS_PLAYER_PRESSING_HORN, g_player);
    if (horn && !g_hornWas) {
        float x = 0, y = 0, z = 0;
        C4(GET_CAR_COORDINATES, car, &x, &y, &z);
        int g = near_garage(x, y, z, g_storeDist);
        if (g >= 0) start_store(car, g);
    }
    g_hornWas = horn;
}

static void try_open_list(void)
{
    if (!get_player()) return;
    if (player_car()) return;
    float x = 0, y = 0, z = 0;
    C4(GET_CHAR_COORDINATES, g_ped, &x, &y, &z);
    int g = near_garage(x, y, z, g_outDist);
    if (g < 0) return;
    open_list(g);
}

static void step_mode(void)
{
    switch (g_mode) {
    case M_STORE_PLATE: poll_typing(); break;
    case M_STORE_IN: step_store_in(); break;
    case M_LIST: poll_list(); break;
    case M_OUT: step_out(); break;
    }
}

static void recover_screen(void)
{
    camera_off();
    C1(DO_SCREEN_FADE_IN, 300);
    controls_back();
}

/* ------------------------------------------------------------ window hook */
#ifndef LCG_TEST
static HWND g_game;
static WNDPROC g_origProc;
static int g_unicode, g_timerSet, g_tick, g_inWorld, g_worldCrashes;
static DWORD g_worldPauseUntil;
static volatile int g_ready;
#define TIMER_ID 0x4C47

static void probe_world(void) { g_inWorld = get_player(); }

static void on_timer(HWND h)
{
    int fg = GetForegroundWindow() == h;
    int keyOut = pressed((int)g_keyOut);
    if (!g_ready || game_paused()) return;
    ++g_tick;
    if (!g_inWorld) {
        if (g_tick % 33) return;
        static int probeCrashes;
        g_quiet = probeCrashes > 0;
        if (!run_guarded(probe_world)) { g_inWorld = 0; if (!probeCrashes++) plog("(waiting for the game to finish loading)"); }
        g_quiet = 0;
        if (g_inWorld) plog("game world is running");
        return;
    }
    if (g_mode != M_IDLE) {
        if (!run_guarded(step_mode)) {
            plog("stopped after a crash in %s", cmd_name(g_crashHash));
            finish();
            run_guarded(recover_screen);
        }
        return;
    }
    if (!fg) return;
    if (keyOut) { if (!run_guarded(try_open_list)) { finish(); run_guarded(recover_screen); } return; }
    if (g_tick % 3 || GetTickCount() < g_worldPauseUntil || g_worldCrashes >= 20) return;
    if (!run_guarded(check_world)) {
        g_worldCrashes++;
        g_worldPauseUntil = GetTickCount() + 5000;
        if (g_mode != M_IDLE) { finish(); run_guarded(recover_screen); }
    }
}

static LRESULT CALLBACK HookProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (!g_timerSet) {
        g_timerSet = 1;
        if (SetTimer(h, TIMER_ID, 30, NULL)) plog("timer started");
        else plog("couldn't start the timer (%lu)", GetLastError());
    }
    if (m == WM_TIMER && w == TIMER_ID) { on_timer(h); return 0; }
    /* keep typed keys away from other key handlers while typing a plate or in the list */
    if ((g_mode == M_STORE_PLATE || g_mode == M_LIST) &&
        (m == WM_KEYDOWN || m == WM_SYSKEYDOWN || m == WM_CHAR || m == WM_KEYUP)) return 0;
    return g_unicode ? CallWindowProcW(g_origProc, h, m, w, l) : CallWindowProcA(g_origProc, h, m, w, l);
}

static BOOL CALLBACK find_window(HWND h, LPARAM lp)
{
    DWORD pid;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    char cls[64];
    GetClassNameA(h, cls, sizeof(cls));
    RECT r; GetClientRect(h, &r);
    if (r.right - r.left < 320 || r.bottom - r.top < 200) return TRUE;
    *(HWND *)lp = h;
    return lstrcmpA(cls, "grcWindow") != 0;
}

static DWORD WINAPI init_thread(LPVOID unused)
{
    (void)unused;
    g_rng ^= GetTickCount() * 2654435761u;
    if (!g_rng) g_rng = 1;
    load_settings();
    load_ide();
    load_cars();

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)dos + dos->e_lfanew);
    g_base = (BYTE *)dos;
    g_size = nt->OptionalHeader.SizeOfImage;
    BYTE *p;
    int cnt = 0;
    p = find_pattern_n("56 8B 35 ? ? ? ? 85 F6 75 06", 1, &cnt);
    if (cnt == 2 && p) g_getNativeAddress = (GetNativeAddressFn)p;
    if ((p = find_pattern("8B 35 ? ? ? ? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10")) ||
        (p = find_pattern("8B 3D ? ? ? ? 85 FF 75 04 33 C0 5F C3")))
        g_pTableSize = *(DWORD **)(p + 2);
    if ((p = find_pattern("8B 1D ? ? ? ? 8B CF 8B 04 D3 3B C7 74 19 8D 64 24 00 85 C0")) ||
        (p = find_pattern("8B 1D ? ? ? ? 8B CE 8B 04 D3 3B C6 74 17 85 C0")))
        g_ppTable = *(DWORD ***)(p + 2);
    if ((p = find_pattern("0F B6 0D ? ? ? ? 0F B6 C0 0B C1"))) {
        g_pUserPause = *(BYTE **)(p + 3); g_pScriptPause = *(BYTE **)(p + 15);
    } else if ((p = find_pattern("0F B6 0D ? ? ? ? 0F B6 15 ? ? ? ? 33 C0 0B C1"))) {
        g_pUserPause = *(BYTE **)(p + 3); g_pScriptPause = *(BYTE **)(p + 10);
    }
    plog("lookup %p, table %p/%p, pause %p %p", g_getNativeAddress, g_pTableSize, g_ppTable, g_pUserPause, g_pScriptPause);
    if (!g_getNativeAddress && (!g_pTableSize || !g_ppTable)) { plog("ERROR: game version not supported (command table not found)"); return 0; }
    AddVectoredExceptionHandler(1, veh);

    HWND w = NULL;
    for (int tries = 0; tries < 1200 && !w; tries++) {
        EnumWindows(find_window, (LPARAM)&w);
        if (!w) Sleep(500);
    }
    if (!w) { plog("ERROR: game window not found"); return 0; }
    g_game = w;
    g_unicode = IsWindowUnicode(w);
    g_origProc = (WNDPROC)(g_unicode ? SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)HookProc)
                                     : SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)HookProc));
    plog("hooked the game window");
    for (int tries = 0; tries < 600; tries++) {
        if (get_handler(GET_PLAYER_ID)) break;
        Sleep(500);
    }
    plog("self-test: game commands %s, LibertyCityPlates %s", get_handler(CREATE_CAR) && get_handler(IS_PLAYER_PRESSING_HORN) ? "found" : "MISSING",
         get_handler(LCP_TEXT_0123) ? "found" : "not loaded (plates won't be set)");
    plog("ready");
    g_ready = 1;
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(inst, g_dir, MAX_PATH);
        char *slash = g_dir;
        for (char *s = g_dir; *s; s++) if (*s == '\\' || *s == '/') slash = s;
        *slash = 0;
        wsprintfA(g_logPath, "%s\\LCGarage.log", g_dir);
        wsprintfA(g_iniPath, "%s\\LCGarage.ini", g_dir);
        wsprintfA(g_carsPath, "%s\\LCGarage.cars", g_dir);
        DeleteFileA(g_logPath);
        plog("LCGarage " VERSION " loaded");
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
#endif
