/* RetroAchievements for The Minish Cap 3DS (retro_ach.h), adapted from
 * ../sm-3ds/source/retro_ach.c: the same rc_client glue, HTTP client and badge loader;
 * the game's memory is read through retro_ach_map_3ds.c and the game is identified by
 * the MD5 of the ROM file. */
#define _GNU_SOURCE
#include "retro_ach.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "md5.h"
#include "rc_client.h"
#include "retro_ach_map_3ds.h"
#include "tmc3ds_version.h"

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"

#define RA_INI "retroachievements.ini"
#define RA_LOG "debug/retroachievements.log"
#define RA_BADGES "badges"

/* A response grows as needed: a set with its conditions can be well over 64 KB. */
enum { kMaxPending = 8, kRequestMax = 1024, kResponseMax = 4 << 20, kMaxAchievements = 256, kToastMs = 3000 };

extern const char* Port_GetLoadedRomPath(void);
extern unsigned char gEwram[0x40000];
extern unsigned char gIwram[0x8000];

static rc_client_t* g_client;
static bool g_enabled;
static char g_user[64], g_token[64];
static volatile RaStatus g_status = kRaOff;
static char g_message[96];
static volatile uint32_t g_version;

/* What the bottom screen reads from its own thread, under g_data_lock. */
static LightLock g_data_lock;
static volatile bool g_locks_ready; /* the bottom screen and the presenter may ask before Init */
static RaAchievement g_list[kMaxAchievements];
static int g_count;
static uint16_t g_view[kMaxAchievements]; /* g_list in the chosen order */
static RaAchievement g_toast;
static u64 g_toast_until;

static bool g_notify_top, g_sound = true, g_descending;
static RaSort g_sort;
/* Fixed point into the sound, kSoundFrac fraction bits; -1 = silent. */
enum { kSoundFrac = 12 };
static volatile int g_sound_pos = -1;

static void Changed(void) { g_version++; }
static void LoadSound(void);

static void LogLine(const char* fmt, ...) {
    mkdir("debug", 0777);
    FILE* f = fopen(RA_LOG, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void SetMessage(const char* fmt, ...) {
    LightLock_Lock(&g_data_lock);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_message, sizeof(g_message), fmt, ap);
    va_end(ap);
    LightLock_Unlock(&g_data_lock);
    Changed();
}

/* ---- The ROM's hash ----------------------------------------------------------------------
 * The RA hash of a GBA game is the MD5 of the ROM file. The port patches its copy in
 * memory (gRomData), so the file is read again, on the worker, once: the result is kept
 * in the ini with the file's path and size. */

enum { kHashNone, kHashWanted, kHashReady, kHashFailed };
static volatile int g_hash_state;
static char g_hash[33];
static char g_hash_key[96]; /* "<size>:<path>" the cached hash belongs to */

/* The hash RetroAchievements is told. RA lists the clean European ROM as "needs a patch" and
 * supports "European Backport Edition" (Prof9's patch, RAPatches GBA/Fix/559). The port has that
 * patch's three fixes natively (docs/eu-backport.md), so for the clean European ROM it reports
 * the patched edition's hash. Checked 2026-10-15: RA's BPS applied to this ROM (md5 below) gives
 * exactly the hash below. Every other ROM is reported as it is. */
static const char kRomMd5Europe[] = "2af78edbe244b5de44471368ae2b6f0b";
static const char kRomMd5EuropeBackport[] = "626a6f1134455c879dd8a5cbb84b116a";

static const char* ReportedHash(void) {
    return strcmp(g_hash, kRomMd5Europe) == 0 ? kRomMd5EuropeBackport : g_hash;
}

static bool RomKey(char* out, size_t size) {
    const char* path = Port_GetLoadedRomPath();
    struct stat st;
    if (!path || !path[0] || stat(path, &st) != 0) return false;
    snprintf(out, size, "%ld:%s", (long)st.st_size, path);
    return true;
}

/* ---- Settings file ---------------------------------------------------------------------- */

static void SaveIni(void) {
    FILE* f = fopen(RA_INI, "w");
    if (!f) return;
    /* The token logs in as the player: keep this file to yourself. */
    fprintf(f, "# RetroAchievements login (written by the bottom screen)\nenabled=%d\nuser=%s\ntoken=%s\n", g_enabled,
            g_user, g_token);
    fprintf(f, "notify_top=%d\nsound=%d\nsort=%d\ndescending=%d\n", g_notify_top, g_sound, g_sort, g_descending);
    if (g_hash_state == kHashReady) fprintf(f, "rom_md5=%s\nrom_key=%s\n", g_hash, g_hash_key);
    fclose(f);
}

static void LoadIni(void) {
    FILE* f = fopen(RA_INI, "r");
    if (!f) return;
    char line[200], md5[40] = "", key[96] = "";
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        const char* v = eq + 1;
        if (!strcmp(line, "enabled")) g_enabled = atoi(v) != 0;
        else if (!strcmp(line, "user")) snprintf(g_user, sizeof(g_user), "%s", v);
        else if (!strcmp(line, "token")) snprintf(g_token, sizeof(g_token), "%s", v);
        else if (!strcmp(line, "notify_top")) g_notify_top = atoi(v) != 0;
        else if (!strcmp(line, "sound")) g_sound = atoi(v) != 0;
        else if (!strcmp(line, "sort") && atoi(v) >= 0 && atoi(v) < kRaSortCount) g_sort = (RaSort)atoi(v);
        else if (!strcmp(line, "descending")) g_descending = atoi(v) != 0;
        else if (!strcmp(line, "rom_md5")) snprintf(md5, sizeof(md5), "%s", v);
        else if (!strcmp(line, "rom_key")) snprintf(key, sizeof(key), "%s", v);
    }
    fclose(f);
    /* The cached hash only if it is of the ROM loaded now. */
    if (strlen(md5) == 32 && RomKey(g_hash_key, sizeof(g_hash_key)) && !strcmp(key, g_hash_key)) {
        memcpy(g_hash, md5, 33);
        g_hash_state = kHashReady;
    }
}

/* ---- GBA memory ----------------------------------------------------------------------------
 * rcheevos' GBA map: IWRAM 0x00000-0x07FFF, EWRAM 0x08000-0x47FFF. Each byte comes from the
 * variable the original game kept there (gRaMap); a byte no variable covers comes from
 * gIwram/gEwram, where the port keeps what it still addresses the GBA way (the BG buffers). */

static const RaMapEntry* FindMapped(uint32_t address) {
    unsigned lo = 0, hi = gRaMapCount;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (gRaMap[mid].address <= address) lo = mid + 1;
        else hi = mid;
    }
    if (!lo) return NULL;
    const RaMapEntry* e = &gRaMap[lo - 1];
    return address - e->address < e->length ? e : NULL;
}

static uint32_t ReadMemory(uint32_t address, uint8_t* buffer, uint32_t num_bytes, rc_client_t* client) {
    (void)client;
    uint32_t done = 0;
    while (done < num_bytes) {
        const uint32_t at = address + done;
        if (at >= 0x48000) break; /* a short read tells rcheevos the address is not backed */
        const RaMapEntry* e = FindMapped(at);
        if (e) {
            uint32_t n = e->length - (at - e->address);
            if (n > num_bytes - done) n = num_bytes - done;
            memcpy(buffer + done, e->storage + (at - e->address), n);
            done += n;
        } else {
            buffer[done++] = at < 0x8000 ? gIwram[at] : gEwram[at - 0x8000];
        }
    }
    return done;
}

/* ---- HTTP ------------------------------------------------------------------------------------
 * mzm's and sm-3ds's client: httpc (http:C in cia/tmc3ds.rsf), with a fallback to plain HTTP
 * when the console's old TLS stack cannot talk to the server. */

static bool g_http_ready;
static bool g_tls_unusable;

static void EnsureHttp(void) {
    if (g_http_ready) return;
    const Result r = httpcInit(0x20000);
    if (R_SUCCEEDED(r)) g_http_ready = true;
    else LogLine("httpcInit failed: 0x%08lX", (unsigned long)r);
}

static void UserAgent(char* out, size_t size) {
    /* Numeric version only (the server's check), then rcheevos' own clause. */
    char ver[16];
    const char* src = TMC_PORT_VERSION;
    size_t n = 0;
    while (*src && !(*src >= '0' && *src <= '9')) src++;
    while (*src && n + 1 < sizeof(ver) && ((*src >= '0' && *src <= '9') || *src == '.')) ver[n++] = *src++;
    while (n > 0 && ver[n - 1] == '.') n--;
    ver[n] = 0;
    if (!ver[0]) snprintf(ver, sizeof(ver), "0.0");
    int len = snprintf(out, size, "MinishCap3DS/%s (Nintendo 3DS) ", ver);
    if (len > 0 && (size_t)len < size && g_client) rc_client_get_user_agent_clause(g_client, out + len, size - len);
}

/* Downloads into *buf (grown with realloc up to kResponseMax; *cap is its size). */
static int HttpOnce(const char* url, const char* post, const char* content_type, char** buf, size_t* cap,
                    int* status_out, char* why, size_t why_size) {
    EnsureHttp();
    if (!g_http_ready) {
        snprintf(why, why_size, "httpc not available");
        return -1;
    }
    httpcContext ctx;
    Result r = httpcOpenContext(&ctx, post ? HTTPC_METHOD_POST : HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(r)) {
        snprintf(why, why_size, "open 0x%08lX", (unsigned long)r);
        return -2;
    }
    httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
    char ua[96];
    UserAgent(ua, sizeof(ua));
    httpcAddRequestHeaderField(&ctx, "User-Agent", ua);
    httpcAddRequestHeaderField(&ctx, "Accept", "*/*");
    httpcAddRequestHeaderField(&ctx, "Connection", "close");
    if (post) {
        httpcAddRequestHeaderField(&ctx, "Content-Type",
                                   content_type ? content_type : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(&ctx, (u32*)(void*)post, (u32)strlen(post));
    }
    r = httpcBeginRequest(&ctx);
    if (R_FAILED(r)) {
        snprintf(why, why_size, "request 0x%08lX", (unsigned long)r);
        httpcCloseContext(&ctx);
        return -3;
    }
    u32 status = 0;
    r = httpcGetResponseStatusCodeTimeout(&ctx, &status, 15000000000ULL);
    if (R_FAILED(r) && status == 0) {
        snprintf(why, why_size, "response 0x%08lX", (unsigned long)r);
        httpcCloseContext(&ctx);
        return -4;
    }
    *status_out = (int)status;
    /* Drain the whole body: httpcCloseContext hangs on a context with data pending. */
    u32 total = 0;
    bool too_big = false;
    do {
        if (total + 1 >= *cap) {
            char* bigger = *cap * 2 <= kResponseMax ? realloc(*buf, *cap * 2) : NULL;
            if (bigger) *buf = bigger, *cap *= 2;
        }
        u32 got = 0;
        const u32 space = (u32)(*cap - 1) - total;
        if (!space) { /* still pending, but no more room: drain it and fail */
            static u8 sink[4096];
            too_big = true;
            r = httpcDownloadData(&ctx, sink, sizeof(sink), &got);
            continue;
        }
        r = httpcDownloadData(&ctx, (u8*)*buf + total, space, &got);
        total += got;
    } while (r == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);
    (*buf)[total] = 0;
    httpcCloseContext(&ctx);
    if (too_big) {
        snprintf(why, why_size, "response over %d KB", kResponseMax >> 10);
        return -6;
    }
    /* RA always answers with a body: an empty one is a failed transfer. */
    if (!total) {
        snprintf(why, why_size, "empty (http %lu)", (unsigned long)status);
        return -5;
    }
    return (int)total;
}

static int Http(const char* url, const char* post, const char* content_type, char** out, size_t* size, int* status) {
    static const char kHttps[] = "https://";
    char why[48] = "";
    char plain[kRequestMax + 8];
    const bool https = !strncmp(url, kHttps, sizeof(kHttps) - 1);
    if (https) snprintf(plain, sizeof(plain), "http://%s", url + sizeof(kHttps) - 1);
    if (https && g_tls_unusable) return HttpOnce(plain, post, content_type, out, size, status, why, sizeof(why));
    int n = HttpOnce(url, post, content_type, out, size, status, why, sizeof(why));
    if (n >= 0 || !https) {
        if (n < 0) LogLine("HTTP failed: %s", why);
        return n;
    }
    LogLine("HTTPS failed (%s); retrying over HTTP", why);
    n = HttpOnce(plain, post, content_type, out, size, status, why, sizeof(why));
    if (n >= 0) {
        LogLine("HTTP works; using it for the rest of this session");
        g_tls_unusable = true;
    } else {
        LogLine("HTTP failed too: %s", why);
    }
    return n;
}

/* ---- Worker thread: server calls, the ROM's hash, badges ---------------------------------- */

typedef struct {
    char url[kRequestMax];
    char post[kRequestMax];
    char content_type[64];
    bool has_post;
    rc_client_server_callback_t callback;
    void* callback_data;
    char* body;
    int body_length;
    int status;
} RaCall;

static RaCall g_pending[kMaxPending], g_done[kMaxPending];
static int g_pending_count, g_done_count;
static char* g_response; /* the worker's buffer, one call at a time */
static size_t g_response_cap;
static LightLock g_lock;
static Thread g_worker;
static volatile bool g_worker_stop;

static bool LoadNextBadge(void);

static void ComputeHash(void) {
    char key[96];
    const char* path = Port_GetLoadedRomPath();
    FILE* f = path && RomKey(key, sizeof(key)) ? fopen(path, "rb") : NULL;
    uint8_t* chunk = f ? malloc(64 * 1024) : NULL;
    if (!chunk) {
        if (f) fclose(f);
        LogLine("ROM hash: cannot read %s", path ? path : "(no ROM path)");
        g_hash_state = kHashFailed;
        return;
    }
    md5_state_t md5;
    md5_init(&md5);
    size_t n;
    while (!g_worker_stop && (n = fread(chunk, 1, 64 * 1024, f)) > 0) md5_append(&md5, chunk, (int)n);
    fclose(f);
    free(chunk);
    if (g_worker_stop) return;
    md5_byte_t digest[16];
    md5_finish(&md5, digest);
    for (int i = 0; i < 16; i++) snprintf(g_hash + i * 2, 3, "%02x", digest[i]);
    snprintf(g_hash_key, sizeof(g_hash_key), "%s", key);
    LogLine("ROM hash %s (%s)", g_hash, path);
    __sync_synchronize(); /* the hash before the state that publishes it */
    g_hash_state = kHashReady;
}

static void Worker(void* arg) {
    (void)arg;
    while (!g_worker_stop) {
        RaCall call;
        bool have = false;
        LightLock_Lock(&g_lock);
        if (g_pending_count > 0 && g_done_count < kMaxPending) {
            call = g_pending[0];
            memmove(&g_pending[0], &g_pending[1], (g_pending_count - 1) * sizeof(RaCall));
            g_pending_count--;
            have = true;
        }
        LightLock_Unlock(&g_lock);
        if (!g_response && (g_response = malloc(64 * 1024))) g_response_cap = 64 * 1024;
        if (!have) {
            if (g_hash_state == kHashWanted) {
                ComputeHash();
                continue;
            }
            /* Server calls first; badges fill the gaps between them. */
            if (!g_response || !LoadNextBadge()) svcSleepThread(20000000ULL);
            continue;
        }
        int status = 0;
        if (!g_response) continue;
        const int n = Http(call.url, call.has_post ? call.post : NULL, call.has_post ? call.content_type : NULL,
                           &g_response, &g_response_cap, &status);
        call.body = n < 0 ? NULL : g_response;
        call.body_length = n < 0 ? 0 : n;
        call.status = n < 0 ? RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR : status;
        /* Never the query string: it carries the token, or the password on a login. */
        LogLine("%.*s -> http %d, %d bytes", (int)strcspn(call.url, "?"), call.url, call.status, n);
        if (n > 0 && (status < 200 || status >= 300)) LogLine("  body: %.200s", g_response);
        /* The set itself (achievements and their conditions, nothing of the account), kept
         * for reading what an achievement that does not unlock is waiting for. */
        if (n > 4000 && status == 200 && memmem(g_response, (size_t)n, "\"Achievements\"", 14)) {
            FILE* f = fopen("debug/ra-set.json", "wb");
            if (f) {
                fwrite(g_response, 1, (size_t)n, f);
                fclose(f);
                LogLine("  set saved to debug/ra-set.json");
            }
        }
        LightLock_Lock(&g_lock);
        g_done[g_done_count++] = call;
        LightLock_Unlock(&g_lock);
        /* The body lives in g_response until the main thread has copied it. */
        for (;;) {
            LightLock_Lock(&g_lock);
            const bool drained = g_done_count == 0;
            LightLock_Unlock(&g_lock);
            if (drained || g_worker_stop) break;
            svcSleepThread(5000000ULL);
        }
    }
}

static void StartWorker(void) {
    if (g_worker) return;
    g_worker_stop = false;
    /* Low priority: below the game, the audio and the bottom screen. */
    g_worker = threadCreate(Worker, NULL, 32 * 1024, 0x3E, -2, false);
    if (!g_worker) LogLine("worker thread not created");
}

static void ServerCall(const rc_api_request_t* request, rc_client_server_callback_t callback, void* callback_data,
                       rc_client_t* client) {
    (void)client;
    StartWorker();
    RaCall call;
    memset(&call, 0, sizeof(call));
    snprintf(call.url, sizeof(call.url), "%s", request->url ? request->url : "");
    if (request->post_data && request->post_data[0]) {
        call.has_post = true;
        snprintf(call.post, sizeof(call.post), "%s", request->post_data);
        snprintf(call.content_type, sizeof(call.content_type), "%s",
                 request->content_type ? request->content_type : "application/x-www-form-urlencoded");
    }
    call.callback = callback;
    call.callback_data = callback_data;
    bool queued = false;
    LightLock_Lock(&g_lock);
    if (g_worker && g_pending_count < kMaxPending) {
        g_pending[g_pending_count++] = call;
        queued = true;
    }
    LightLock_Unlock(&g_lock);
    if (!queued) { /* rcheevos would otherwise wait for an answer forever */
        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
        callback(&response, callback_data);
    }
}

static void DrainResponses(void) {
    for (;;) {
        char* body = NULL;
        RaCall call;
        bool have = false;
        LightLock_Lock(&g_lock);
        if (g_done_count > 0) {
            call = g_done[0];
            /* Copied out: once the queue is empty the worker reuses its buffer. */
            if (call.body && (body = malloc(call.body_length + 1))) {
                memcpy(body, call.body, call.body_length);
                body[call.body_length] = 0;
            }
            call.body = body;
            if (!body) call.body_length = 0, call.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            memmove(&g_done[0], &g_done[1], (g_done_count - 1) * sizeof(RaCall));
            g_done_count--;
            have = true;
        }
        LightLock_Unlock(&g_lock);
        if (!have) return;
        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.body = call.body;
        response.body_length = call.body_length;
        response.http_status_code = call.status;
        call.callback(&response, call.callback_data);
        free(body);
    }
}

/* ---- Badges ----------------------------------------------------------------------------------
 * Each achievement's badge (a 64x64 PNG on RA's media server), loaded on the worker between
 * server calls: from badges/<name>.png when it is there, downloaded and saved there
 * otherwise, then scaled to the two sizes the UI draws. The main thread only adds entries
 * (under g_lock); the worker fills one in and publishes it by setting `state` last. */

enum { kBadgeQueued, kBadgeLoading, kBadgeReady, kBadgeFailed, kBadgeTries = 2 };

typedef struct {
    char name[16];
    char url[160];
    volatile int state;
    volatile u64 wanted; /* when the UI last asked for it: the newest goes first */
    int tries;
    uint32_t *small, *big;
} RaBadge;

static RaBadge g_badges[kMaxAchievements];
static int g_badge_count;
static volatile bool g_badge_arrived; /* the UI should redraw (read in RetroAch_Update) */

static RaBadge* FindBadge(const char* name) {
    for (int i = 0; i < g_badge_count; i++)
        if (!strcmp(g_badges[i].name, name)) return &g_badges[i];
    return NULL;
}

static void QueueBadge(const char* name, const char* url) {
    LightLock_Lock(&g_lock);
    if (name[0] && !FindBadge(name) && g_badge_count < kMaxAchievements) {
        RaBadge* b = &g_badges[g_badge_count];
        memset(b, 0, sizeof(*b));
        b->state = kBadgeQueued;
        snprintf(b->name, sizeof(b->name), "%s", name);
        if (url && url[0]) snprintf(b->url, sizeof(b->url), "%s", url);
        else snprintf(b->url, sizeof(b->url), "https://media.retroachievements.org/Badge/%s.png", name);
        g_badge_count++;
    }
    LightLock_Unlock(&g_lock);
}

/* `src` (sw x sh RGBA) scaled to size x size by averaging the source pixels each target
 * pixel covers, over a dark background so a transparent edge does not come out white. */
static uint32_t* ScaleBadge(const uint8_t* src, int sw, int sh, int size) {
    uint32_t* out = malloc(size * size * sizeof(uint32_t));
    if (!out) return NULL;
    for (int y = 0; y < size; y++) {
        const int y0 = y * sh / size, y1 = (y + 1) * sh / size > y0 ? (y + 1) * sh / size : y0 + 1;
        for (int x = 0; x < size; x++) {
            const int x0 = x * sw / size, x1 = (x + 1) * sw / size > x0 ? (x + 1) * sw / size : x0 + 1;
            uint32_t sum[3] = { 0 }, n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++, n++) {
                    const uint8_t* p = src + (yy * sw + xx) * 4;
                    static const uint8_t kBack[3] = { 12, 16, 24 };
                    for (int c = 0; c < 3; c++) sum[c] += (p[c] * p[3] + kBack[c] * (255 - p[3])) / 255;
                }
            /* RGB() layout: A B G R from the top byte down. */
            out[y * size + x] = 0xFF000000u | (sum[2] / n) << 16 | (sum[1] / n) << 8 | (sum[0] / n);
        }
    }
    return out;
}

/* The PNG from the cache, or downloaded into g_response and saved; NULL when neither. */
static uint8_t* BadgePng(RaBadge* b, int* len) {
    char path[64];
    snprintf(path, sizeof(path), RA_BADGES "/%s.png", b->name);
    FILE* f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* buf = n > 8 && n < 256 * 1024 ? malloc(n) : NULL;
        const bool ok = buf && fread(buf, 1, n, f) == (size_t)n;
        fclose(f);
        if (ok) {
            *len = (int)n;
            return buf;
        }
        free(buf);
    }
    int status = 0;
    const int n = Http(b->url, NULL, NULL, &g_response, &g_response_cap, &status);
    if (n < 8 || status != 200 || memcmp(g_response, "\x89PNG", 4)) {
        LogLine("badge %s: http %d, %d bytes", b->name, status, n);
        return NULL;
    }
    uint8_t* buf = malloc(n);
    if (!buf) return NULL;
    memcpy(buf, g_response, n);
    mkdir(RA_BADGES, 0777);
    if ((f = fopen(path, "wb"))) {
        const bool ok = fwrite(buf, 1, n, f) == (size_t)n;
        fclose(f);
        if (!ok) remove(path); /* a cut file would be read back as a broken badge */
    }
    *len = n;
    return buf;
}

/* Worker thread: loads one queued badge. False when there was none to load. */
static bool LoadNextBadge(void) {
    RaBadge* b = NULL;
    LightLock_Lock(&g_lock);
    for (int i = 0; i < g_badge_count; i++) {
        RaBadge* c = &g_badges[i];
        if (c->state == kBadgeQueued && (!b || c->wanted > b->wanted)) b = c;
    }
    if (b) b->state = kBadgeLoading;
    LightLock_Unlock(&g_lock);
    if (!b) return false;
    int len = 0, w = 0, h = 0, comp = 0;
    uint8_t* png = BadgePng(b, &len);
    uint8_t* rgba = png ? stbi_load_from_memory(png, len, &w, &h, &comp, 4) : NULL;
    free(png);
    if (rgba) {
        b->small = ScaleBadge(rgba, w, h, kRaBadgeSmall);
        b->big = ScaleBadge(rgba, w, h, kRaBadgeBig);
        stbi_image_free(rgba);
    }
    __sync_synchronize(); /* the pixels before the state that publishes them */
    if (b->small && b->big) b->state = kBadgeReady;
    else if (++b->tries < kBadgeTries) b->state = kBadgeQueued;
    else b->state = kBadgeFailed;
    g_badge_arrived = true;
    return true;
}

static void FreeBadges(void) {
    for (int i = 0; i < g_badge_count; i++) free(g_badges[i].small), free(g_badges[i].big);
    g_badge_count = 0;
}

/* ---- Achievement list --------------------------------------------------------------------- */

/* The comparators are ascending and apply the direction themselves; ties keep rcheevos'
 * order, so equal entries never swap places between rebuilds (qsort is not stable). */
static int Dir(int cmp) { return g_descending ? -cmp : cmp; }

static int ByTitle(const void* a, const void* b) {
    const int ia = *(const uint16_t*)a, ib = *(const uint16_t*)b;
    const int c = strcasecmp(g_list[ia].title, g_list[ib].title);
    return c ? Dir(c) : ia - ib;
}

static int ByPoints(const void* a, const void* b) {
    const int ia = *(const uint16_t*)a, ib = *(const uint16_t*)b;
    if (g_list[ia].points != g_list[ib].points) return Dir(g_list[ia].points < g_list[ib].points ? -1 : 1);
    return ia - ib;
}

/* Unlocked ones first whatever the direction: a locked one has no time to sort by. */
static int ByRecent(const void* a, const void* b) {
    const int ia = *(const uint16_t*)a, ib = *(const uint16_t*)b;
    if (g_list[ia].unlocked != g_list[ib].unlocked) return g_list[ia].unlocked ? -1 : 1;
    if (g_list[ia].unlock_time != g_list[ib].unlock_time)
        return Dir(g_list[ia].unlock_time > g_list[ib].unlock_time ? -1 : 1); /* newest first */
    return ia - ib;
}

/* Under g_data_lock. */
static void RebuildViewLocked(void) {
    for (int i = 0; i < g_count; i++) g_view[i] = (uint16_t)i;
    switch (g_sort) {
        case kRaSortTitle: qsort(g_view, g_count, sizeof(g_view[0]), ByTitle); break;
        case kRaSortPoints: qsort(g_view, g_count, sizeof(g_view[0]), ByPoints); break;
        case kRaSortRecent: qsort(g_view, g_count, sizeof(g_view[0]), ByRecent); break;
        default:
            if (g_descending)
                for (int i = 0; i < g_count / 2; i++) {
                    const uint16_t t = g_view[i];
                    g_view[i] = g_view[g_count - 1 - i], g_view[g_count - 1 - i] = t;
                }
            break;
    }
}

static void ClearList(void) {
    LightLock_Lock(&g_data_lock);
    g_count = 0;
    LightLock_Unlock(&g_data_lock);
    Changed();
}

static void RefreshList(void) {
    static RaAchievement fresh[kMaxAchievements];
    int count = 0;
    rc_client_achievement_list_t* list =
        g_client ? rc_client_create_achievement_list(g_client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
                                                     RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE)
                 : NULL;
    if (list) {
        for (uint32_t b = 0; b < list->num_buckets; b++) {
            for (uint32_t i = 0; i < list->buckets[b].num_achievements && count < kMaxAchievements; i++) {
                const rc_client_achievement_t* a = list->buckets[b].achievements[i];
                /* rcheevos adds a 0-point warning for clients it does not know: noise here. */
                if (a->points == 0 && a->title && !strcasecmp(a->title, "Warning: Unknown Emulator")) continue;
                RaAchievement* o = &fresh[count++];
                memset(o, 0, sizeof(*o));
                o->id = a->id;
                snprintf(o->title, sizeof(o->title), "%s", a->title ? a->title : "");
                snprintf(o->description, sizeof(o->description), "%s", a->description ? a->description : "");
                snprintf(o->badge, sizeof(o->badge), "%s", a->badge_name);
                o->points = a->points;
                o->type = a->type <= RC_CLIENT_ACHIEVEMENT_TYPE_WIN ? (RaType)a->type : kRaTypeStandard;
                o->unlocked = a->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
                o->unlock_time = o->unlocked ? (uint32_t)a->unlock_time : 0;
                QueueBadge(o->badge, a->badge_url);
            }
        }
        rc_client_destroy_achievement_list(list);
    }
    LightLock_Lock(&g_data_lock);
    memcpy(g_list, fresh, count * sizeof(fresh[0]));
    g_count = count;
    RebuildViewLocked();
    LightLock_Unlock(&g_data_lock);
    Changed();
}

/* ---- Events, login, game ------------------------------------------------------------------- */

static void ShowToast(const RaAchievement* a) {
    LightLock_Lock(&g_data_lock);
    g_toast = *a;
    g_toast_until = osGetTime() + kToastMs;
    LightLock_Unlock(&g_data_lock);
    if (g_sound) g_sound_pos = 0;
    Changed();
}

static void EventHandler(const rc_client_event_t* event, rc_client_t* client) {
    (void)client;
    switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
            const rc_client_achievement_t* a = event->achievement;
            LogLine("UNLOCKED %lu '%s' (+%lu)", (unsigned long)a->id, a->title ? a->title : "",
                    (unsigned long)a->points);
            RaAchievement t;
            memset(&t, 0, sizeof(t));
            t.id = a->id;
            snprintf(t.title, sizeof(t.title), "%s", a->title ? a->title : "");
            snprintf(t.badge, sizeof(t.badge), "%s", a->badge_name);
            t.points = a->points;
            t.unlocked = true;
            ShowToast(&t);
            RefreshList();
            break;
        }
        case RC_CLIENT_EVENT_GAME_COMPLETED: RefreshList(); break;
        case RC_CLIENT_EVENT_SERVER_ERROR:
            SetMessage("%s", event->server_error->error_message ? event->server_error->error_message : "server error");
            LogLine("server error (%s): %s", event->server_error->api, g_message);
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            g_status = kRaOffline;
            Changed();
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            g_status = kRaOnline;
            Changed();
            break;
        default: break;
    }
}

static bool g_load_in_flight;
static int g_load_attempts;
static u64 g_load_retry_at;

static void LoadGameDone(int result, const char* error, rc_client_t* client, void* userdata) {
    (void)client, (void)userdata;
    g_load_in_flight = false;
    g_load_retry_at = osGetTime() + 10000;
    if (result == RC_OK) {
        const rc_client_game_t* game = rc_client_get_game_info(g_client);
        RefreshList();
        SetMessage("");
        LogLine("game loaded: id %lu '%s', %d achievements, %d unlocked", (unsigned long)(game ? game->id : 0),
                game && game->title ? game->title : "", g_count, RetroAch_UnlockedCount());
    } else if (result == RC_NO_GAME_LOADED) {
        g_load_attempts = 99; /* an unknown hash stays unknown */
        SetMessage("ROM not recognised by RetroAchievements");
        LogLine("game load: hash %s not recognised", ReportedHash());
    } else {
        SetMessage("%s", error ? error : "game load failed");
        LogLine("game load failed (%d): %s", result, error ? error : "");
    }
}

static void BeginLoadGame(void) {
    if (!g_client || g_load_in_flight || rc_client_is_game_loaded(g_client)) return;
    if (g_hash_state == kHashNone) g_hash_state = kHashWanted;
    if (g_hash_state == kHashFailed && g_load_attempts < 99) {
        g_load_attempts = 99;
        SetMessage("Cannot read the ROM file");
    }
    if (g_hash_state != kHashReady) return;
    if (osGetTime() < g_load_retry_at || g_load_attempts >= 5) return;
    g_load_in_flight = true;
    g_load_attempts++;
    if (ReportedHash() != g_hash) LogLine("reporting the European Backport hash %s for the clean European ROM", ReportedHash());
    rc_client_begin_load_game(g_client, ReportedHash(), LoadGameDone, NULL);
}

static void LoginDone(int result, const char* error, rc_client_t* client, void* userdata) {
    (void)client, (void)userdata;
    if (result != RC_OK) {
        g_status = result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN ? kRaLoginError : kRaOffline;
        SetMessage("%s", error ? error : "login failed");
        LogLine("login failed (%d): %s", result, error ? error : "");
        return;
    }
    const rc_client_user_t* user = rc_client_get_user_info(g_client);
    if (user) {
        LightLock_Lock(&g_data_lock);
        snprintf(g_user, sizeof(g_user), "%s", user->username ? user->username : g_user);
        LightLock_Unlock(&g_data_lock);
        snprintf(g_token, sizeof(g_token), "%s", user->token ? user->token : "");
        SaveIni();
    }
    g_status = kRaOnline;
    SetMessage("");
    LogLine("login ok: %s", g_user);
    g_load_attempts = 0;
    g_load_retry_at = 0;
    BeginLoadGame();
}

static void BeginLogin(const char* password) {
    if (!g_client) return;
    g_status = kRaConnecting;
    Changed();
    if (password && password[0]) rc_client_begin_login_with_password(g_client, g_user, password, LoginDone, NULL);
    else if (g_user[0] && g_token[0]) rc_client_begin_login_with_token(g_client, g_user, g_token, LoginDone, NULL);
    else g_status = kRaNoAccount;
}

static void ClientLog(const char* message, const rc_client_t* client) {
    (void)client;
    LogLine("rcheevos: %s", message);
}

/* ---- Public ---------------------------------------------------------------------------------- */

void RetroAch_Init(void) {
    if (g_client) return;
    LightLock_Init(&g_lock);
    LightLock_Init(&g_data_lock);
    g_locks_ready = true;
    LoadIni();
    LoadSound();
    g_client = rc_client_create(ReadMemory, ServerCall);
    if (!g_client) {
        g_status = kRaLoginError;
        SetMessage("rcheevos failed to start");
        return;
    }
    rc_client_set_event_handler(g_client, EventHandler);
    rc_client_set_hardcore_enabled(g_client, 0);
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_WARN, ClientLog);
    if (!g_enabled) {
        g_status = kRaOff;
    } else if (g_user[0] && g_token[0]) {
        LogLine("--- Minish Cap 3DS %s ---", TMC_PORT_VERSION_LABEL);
        StartWorker();
        if (g_hash_state == kHashNone) g_hash_state = kHashWanted; /* while the login runs */
        BeginLogin(NULL);
    } else {
        g_status = kRaNoAccount;
    }
    Changed();
}

void RetroAch_Shutdown(void) {
    g_worker_stop = true;
    if (g_worker) {
        threadJoin(g_worker, U64_MAX);
        threadFree(g_worker);
        g_worker = NULL;
    }
    if (g_client) {
        rc_client_destroy(g_client);
        g_client = NULL;
    }
    FreeBadges();
    if (g_http_ready) {
        httpcExit();
        g_http_ready = false;
    }
}

void RetroAch_Update(void) {
    if (!g_client) return;
    DrainResponses();
    if (g_badge_arrived) {
        g_badge_arrived = false;
        Changed();
    }
    if (g_enabled && g_status == kRaOnline && !rc_client_is_game_loaded(g_client)) BeginLoadGame();
    /* Keeps the session alive and retries pending unlocks when no game frame runs. */
    rc_client_idle(g_client);
    if (g_toast_until && osGetTime() >= g_toast_until) {
        LightLock_Lock(&g_data_lock);
        g_toast_until = 0;
        LightLock_Unlock(&g_data_lock);
        Changed();
    }
    if (g_hash_state == kHashReady) {
        static bool saved;
        if (!saved) {
            saved = true;
            SaveIni(); /* the hash, so the next start does not read the ROM again */
        }
    }
}

void RetroAch_DoFrame(void) {
    if (!g_client || !g_enabled) return;
    rc_client_do_frame(g_client);
}

void RetroAch_GameReset(void) {
    if (g_client) rc_client_reset(g_client);
}

static void ProgressPath(uint32_t id, char* out, size_t size) {
    snprintf(out, size, "states/tmc-state%u.rap", (unsigned)id);
}

void RetroAch_StateSaved(uint32_t id) {
    if (!g_client || !rc_client_is_game_loaded(g_client)) return;
    const size_t size = rc_client_progress_size(g_client);
    uint8_t* buf = size ? malloc(size) : NULL;
    if (!buf) return;
    char path[48];
    ProgressPath(id, path, sizeof(path));
    if (rc_client_serialize_progress_sized(g_client, buf, size) == RC_OK) {
        FILE* f = fopen(path, "wb");
        if (f) {
            fwrite(buf, 1, size, f);
            fclose(f);
        }
    }
    free(buf);
}

void RetroAch_StateLoaded(uint32_t id) {
    if (!g_client || !rc_client_is_game_loaded(g_client)) return;
    char path[48];
    ProgressPath(id, path, sizeof(path));
    FILE* f = fopen(path, "rb");
    if (!f) { /* a state saved before achievements were on: start every one afresh */
        rc_client_deserialize_progress_sized(g_client, NULL, 0);
        return;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = size > 0 ? malloc(size) : NULL;
    if (buf && fread(buf, 1, size, f) == (size_t)size) rc_client_deserialize_progress_sized(g_client, buf, size);
    else rc_client_deserialize_progress_sized(g_client, NULL, 0);
    free(buf);
    fclose(f);
}

void RetroAch_StateDeleted(uint32_t id) {
    char path[48];
    ProgressPath(id, path, sizeof(path));
    remove(path);
}

bool RetroAch_Enabled(void) { return g_enabled; }

void RetroAch_SetEnabled(bool on) {
    if (on == g_enabled) return;
    g_enabled = on;
    SaveIni();
    if (!g_client) return;
    if (!on) {
        rc_client_unload_game(g_client);
        g_load_attempts = 0;
        ClearList();
        g_status = kRaOff;
    } else if (rc_client_get_user_info(g_client)) {
        g_status = kRaOnline;
    } else if (g_user[0] && g_token[0]) {
        StartWorker();
        BeginLogin(NULL);
    } else {
        g_status = kRaNoAccount;
    }
    Changed();
}

void RetroAch_PromptLogin(void) {
    SwkbdState kb;
    char user[64] = "", pass[64] = "";
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof(user) - 1);
    swkbdSetHintText(&kb, "RetroAchievements user name");
    if (g_user[0]) swkbdSetInitialText(&kb, g_user);
    if (swkbdInputText(&kb, user, sizeof(user)) != SWKBD_BUTTON_CONFIRM || !user[0]) return;
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof(pass) - 1);
    swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
    swkbdSetHintText(&kb, "RetroAchievements password");
    if (swkbdInputText(&kb, pass, sizeof(pass)) != SWKBD_BUTTON_CONFIRM || !pass[0]) return;
    LightLock_Lock(&g_data_lock);
    snprintf(g_user, sizeof(g_user), "%s", user);
    LightLock_Unlock(&g_data_lock);
    g_token[0] = 0;
    g_enabled = true;
    SaveIni();
    if (g_client && rc_client_get_user_info(g_client)) rc_client_logout(g_client);
    StartWorker();
    BeginLogin(pass); /* the password is used once and never stored */
    memset(pass, 0, sizeof(pass));
}

void RetroAch_Logout(void) {
    if (g_client) rc_client_logout(g_client);
    g_token[0] = 0;
    ClearList();
    g_load_attempts = 0;
    g_status = g_enabled ? kRaNoAccount : kRaOff;
    SaveIni();
    SetMessage("");
}

bool RetroAch_LoggedIn(void) {
    const RaStatus st = g_status;
    return st == kRaOnline || st == kRaOffline || st == kRaConnecting;
}

RaStatus RetroAch_Status(void) { return g_status; }

void RetroAch_User(char* out, int cap) {
    if (!g_locks_ready) { if (cap > 0) out[0] = 0; return; }
    LightLock_Lock(&g_data_lock);
    snprintf(out, (size_t)cap, "%s", g_user);
    LightLock_Unlock(&g_data_lock);
}

void RetroAch_Message(char* out, int cap) {
    if (!g_locks_ready) { if (cap > 0) out[0] = 0; return; }
    LightLock_Lock(&g_data_lock);
    snprintf(out, (size_t)cap, "%s", g_message);
    LightLock_Unlock(&g_data_lock);
}

bool RetroAch_GameLoaded(void) { return g_client && rc_client_is_game_loaded(g_client); }

int RetroAch_Count(void) { return g_count; }

bool RetroAch_CopyAt(int i, RaAchievement* out) {
    if (!g_locks_ready) return false;
    LightLock_Lock(&g_data_lock);
    const bool ok = i >= 0 && i < g_count;
    if (ok) *out = g_list[g_view[i]];
    LightLock_Unlock(&g_data_lock);
    return ok;
}

int RetroAch_UnlockedCount(void) {
    if (!g_locks_ready) return 0;
    int n = 0;
    LightLock_Lock(&g_data_lock);
    for (int i = 0; i < g_count; i++) n += g_list[i].unlocked;
    LightLock_Unlock(&g_data_lock);
    return n;
}

uint32_t RetroAch_Points(bool unlocked_only) {
    if (!g_locks_ready) return 0;
    uint32_t n = 0;
    LightLock_Lock(&g_data_lock);
    for (int i = 0; i < g_count; i++)
        if (!unlocked_only || g_list[i].unlocked) n += g_list[i].points;
    LightLock_Unlock(&g_data_lock);
    return n;
}

const uint32_t* RetroAch_Badge(const char* badge, int size) {
    if (!g_locks_ready) return NULL;
    if (!badge || !badge[0]) return NULL;
    const uint32_t* px = NULL;
    LightLock_Lock(&g_lock);
    RaBadge* b = FindBadge(badge);
    if (b && b->state == kBadgeReady) px = size == kRaBadgeBig ? b->big : b->small;
    else if (b && b->state == kBadgeQueued) b->wanted = osGetTime();
    LightLock_Unlock(&g_lock);
    return px;
}

uint32_t RetroAch_Version(void) { return g_version; }

bool RetroAch_Toast(RaAchievement* out) {
    if (!g_locks_ready) return false;
    LightLock_Lock(&g_data_lock);
    const bool on = g_toast_until != 0;
    if (on && out) *out = g_toast;
    LightLock_Unlock(&g_data_lock);
    return on;
}

bool RetroAch_NotifyTop(void) { return g_notify_top; }

void RetroAch_SetNotifyTop(bool top) {
    g_notify_top = top;
    SaveIni();
    Changed();
}

bool RetroAch_Sound(void) { return g_sound; }

void RetroAch_SetSound(bool on) {
    g_sound = on;
    if (!on) g_sound_pos = -1;
    SaveIni();
    Changed();
}

RaSort RetroAch_Sort(void) { return g_sort; }
bool RetroAch_Descending(void) { return g_descending; }

void RetroAch_SetSort(RaSort sort, bool descending) {
    LightLock_Lock(&g_data_lock);
    g_sort = (unsigned)sort < kRaSortCount ? sort : kRaSortDefault;
    g_descending = descending;
    RebuildViewLocked();
    LightLock_Unlock(&g_data_lock);
    SaveIni();
    Changed();
}

void RetroAch_ShowPreview(void) {
    RaAchievement t;
    memset(&t, 0, sizeof(t));
    snprintf(t.title, sizeof(t.title), "%s", "RetroAchievements");
    t.points = 5;
    t.unlocked = true;
    ShowToast(&t);
}

/* The unlock sound: mzm's and sm-3ds's, mono s16le at 32000 Hz in romfs:/ra-unlock.pcm,
 * read at Init (outside the pinned read-only segment, 3dsx.ld) and resampled to the output
 * rate by nearest sample. */
enum { kSoundRate = 32000 };
static int16_t* g_sound_pcm;
static uint32_t g_sound_frames;

static void LoadSound(void) {
    FILE* f = fopen("romfs:/ra-unlock.pcm", "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t* pcm = size > 0 ? malloc((size_t)size) : NULL;
    if (pcm && fread(pcm, 1, (size_t)size, f) == (size_t)size) {
        g_sound_frames = (uint32_t)(size / 2);
        g_sound_pcm = pcm;
    } else {
        free(pcm);
    }
    fclose(f);
}

void RetroAch_MixAudio(int16_t* out, int frames, int rate) {
    int pos = g_sound_pos;
    if (pos < 0) return;
    if (!g_sound_pcm) {
        g_sound_pos = -1;
        return;
    }
    const uint32_t step = (uint32_t)(((uint64_t)kSoundRate << kSoundFrac) / (uint32_t)rate);
    for (int i = 0; i < frames; i++, pos += (int)step) {
        const uint32_t k = (uint32_t)pos >> kSoundFrac;
        if (k >= g_sound_frames) {
            g_sound_pos = -1;
            return;
        }
        const int v = g_sound_pcm[k] * 3 / 4; /* under the game, not over it */
        for (int c = 0; c < 2; c++) {
            const int m = out[i * 2 + c] + v;
            out[i * 2 + c] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
    g_sound_pos = pos;
}
