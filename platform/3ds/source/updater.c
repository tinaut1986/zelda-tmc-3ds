#include "updater.h"
#include "tmc3ds_version.h"
#include "platform_3ds.h"
#include <3ds.h>
#include <curl/curl.h>
#include <mbedtls/sha256.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>

static void UpdateLog(const char *format, ...) {
  char line[256]; va_list args; va_start(args, format);
  vsnprintf(line, sizeof(line), format, args); va_end(args);
  Platform3DS_Debug(line);
}

#include "app_paths_3ds.h"
#include "net_3ds.h"

#define UPDATE_DIR TMC3DS_APP_DIR "/update"
#define UPDATE_PART UPDATE_DIR "/download.part"
#define CHANNEL_FILE UPDATE_DIR "/channel.txt"
#define UPDATE_IO_SIZE (128u * 1024u)
#define MANIFEST_LIMIT (2u * 1024u * 1024u)
static LightLock lock;
static UpdateStatus status;
static UpdateRelease release;
static Thread worker;
static bool initialized, homebrew, busy, cancel, download_job;
static char launch_file[768];
// boot_check: the check started by Updater_Init(autoCheck) has not finished. auto_active: it found a
// newer build and the player has not answered (or dismissed) the prompt yet.
static volatile bool boot_check, auto_active;

static bool cancelled(void) { return __atomic_load_n(&cancel, __ATOMIC_ACQUIRE) || aptShouldClose(); }
static void publish(UpdateState state, const char *message) {
  LightLock_Lock(&lock);
  status.state = state; status.progress = 0; status.revision++;
  snprintf(status.message, sizeof(status.message), "%s", message ? message : "");
  LightLock_Unlock(&lock);
}
void Updater_GetStatus(UpdateStatus *out) {
  if (!initialized) { memset(out, 0, sizeof(*out)); return; }
  LightLock_Lock(&lock); *out = status; LightLock_Unlock(&lock);
}
static void progress(unsigned percent) {
  if (percent > 100) percent = 100;
  LightLock_Lock(&lock);
  if (status.progress != percent) { status.progress = percent; status.revision++; }
  LightLock_Unlock(&lock);
}
bool Updater_Busy(void) { return __atomic_load_n(&busy, __ATOMIC_ACQUIRE); }
// A 3DSX update leaves the game to the Homebrew Launcher at once. A CIA one is installed over the running
// title: the game starts again into it 10 s later (Platform3DS_RequestRelaunch, as after SITE INSTALL), so
// the state DONE does not close it.
bool Updater_ShouldClose(void) { UpdateStatus s; Updater_GetStatus(&s); return s.state == UPDATE_DONE && homebrew; }
void Updater_Cancel(void) { __atomic_store_n(&cancel, true, __ATOMIC_RELEASE); }

typedef struct Transfer { char *data; size_t size, capacity; FILE *file; uint32_t expected; size_t pending; unsigned writes; bool io_error; } Transfer;
// Batch small TLS records before crossing into the SD service. Keep the buffer
// on the heap: the worker stack must also accommodate curl and TLS.
static bool open_download(Transfer *t) {
  t->data = malloc(UPDATE_IO_SIZE);
  if (!t->data) return false;
  t->capacity = UPDATE_IO_SIZE;
  t->file = fopen(UPDATE_PART, "wb");
  if (!t->file) return false;
  // Our explicit buffer sets the write size independently of newlib's BUFSIZ.
  return setvbuf(t->file, NULL, _IONBF, 0) == 0;
}
static bool flush_download(Transfer *t) {
  if (cancelled() || t->io_error) return false;
  if (!t->pending) return true;
  t->writes++;
  if (write(fileno(t->file), t->data, t->pending) != (ssize_t)t->pending) {
    t->io_error = true;
    return false;
  }
  t->pending = 0;
  return true;
}
static bool close_download(Transfer *t, bool fetched) {
  // Never accept an incomplete body, a failed final write, or a close error.
  bool ok = fetched && t->size == t->expected && flush_download(t);
  if (fclose(t->file)) { t->io_error = true; ok = false; }
  t->file = NULL;
  free(t->data); t->data = NULL; t->capacity = 0;
  if (t->io_error) publish(UPDATE_ERROR, "SD CARD WRITE FAILED");
  if (ok) progress(100);
  return ok;
}
static size_t receive(void *ptr, size_t a, size_t b, void *userdata) {
  Transfer *t = userdata;
  if (a && b > SIZE_MAX / a) return 0;
  size_t n = a * b, limit = t->file ? t->expected : MANIFEST_LIMIT;
  if (cancelled() || t->size > limit || n > limit - t->size) return 0;
  if (t->file) {
    if (!t->data || !t->capacity || t->io_error) return 0;
    const char *source = ptr;
    size_t left = n;
    while (left) {
      size_t take = t->capacity - t->pending;
      if (take > left) take = left;
      memcpy(t->data + t->pending, source, take);
      t->pending += take; source += take; left -= take;
      if (t->pending == t->capacity && !flush_download(t)) return 0;
    }
    unsigned percent = t->expected ? (unsigned)((t->size + n) * 100ull / t->expected) : 0;
    progress(percent < 100 ? percent : 99);
  } else {
    if (t->size + n + 1 > t->capacity) {
      size_t cap = t->size + n + 4096;
      char *p = realloc(t->data, cap); if (!p) return 0;
      t->data = p; t->capacity = cap;
    }
    memcpy(t->data + t->size, ptr, n); t->data[t->size + n] = 0;
  }
  t->size += n; return n;
}
static int transfer_progress(void *p, curl_off_t total, curl_off_t now, curl_off_t up, curl_off_t sent) {
  return cancelled() ? 1 : 0;
}
static bool fetch(const char *url, Transfer *t) {
  CURL *c = curl_easy_init(); if (!c) return false;
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "Minish-Cap-3DS-tinaut/" TMC_PORT_VERSION);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/update-ca.pem");
  curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
  curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, t->file ? 1800L : 90L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 128L);
  curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(c, CURLOPT_BUFFERSIZE, (long)UPDATE_IO_SIZE);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, t);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, transfer_progress);
  CURLcode code = curl_easy_perform(c);
  long http = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
  if (t->file) {
    double seconds = 0;
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &seconds);
    UpdateLog("Updater download: bytes=%lu seconds=%.2f KiB/s=%.1f SD-writes=%u",
      (unsigned long)t->size, seconds, seconds > 0 ? t->size / (1024.0 * seconds) : 0, t->writes);
  }
  curl_easy_cleanup(c);
  if (code != CURLE_OK || http != 200) {
    UpdateLog("Updater transfer failed: curl=%d http=%ld", code, http);
    return false;
  }
  return true;
}
static bool enough_space(uint64_t required) {
  FS_ArchiveResource r;
  return R_SUCCEEDED(FSUSER_GetSdmcArchiveResource(&r)) &&
    (uint64_t)r.freeClusters * r.clusterSize >= required;
}
static bool verify_file(void) {
  FILE *f = fopen(UPDATE_PART, "rb"); if (!f) return false;
  mbedtls_sha256_context sha; mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  unsigned char *data = malloc(UPDATE_IO_SIZE), digest[32]; size_t total = 0; ssize_t n = 0;
  ok = ok && data != NULL;
  while (ok && (n = read(fileno(f), data, UPDATE_IO_SIZE)) > 0) {
    total += n; ok = !cancelled() && mbedtls_sha256_update_ret(&sha, data, n) == 0;
  }
  ok = ok && n >= 0 && total == release.size && mbedtls_sha256_finish_ret(&sha, digest) == 0;
  fclose(f); free(data); mbedtls_sha256_free(&sha);
  char hex[65]; for (int i = 0; i < 32 && ok; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  return ok && !strcmp(hex, release.sha256);
}
static bool install_cia(void) {
  Result rc = amInit(); if (R_FAILED(rc)) return false;
  Handle input = 0, output = 0; bool started = false, ok = false;
  AM_TitleInfo info; u64 required = 0;
  unsigned char *data = malloc(UPDATE_IO_SIZE);
  if (!data) goto done;
  rc = FSUSER_OpenFileDirectly(&input, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
    fsMakePath(PATH_ASCII, TMC3DS_APP_DIR_FS "/update/download.part"), FS_OPEN_READ, 0);
  if (R_FAILED(rc)) goto done;
  rc = AM_GetCiaFileInfo(MEDIATYPE_SD, &info, input);
  if (R_FAILED(rc) || info.titleID != TMC3DS_TITLE_ID) goto done;
  rc = AM_GetCiaRequiredSpace(&required, MEDIATYPE_SD, input);
  if (R_FAILED(rc) || !enough_space(required + 4 * 1024 * 1024ull) || cancelled()) goto done;
  // Overwrite through AM; never delete the installed title first.
  rc = AM_StartCiaInstallOverwrite(&output, MEDIATYPE_SD);
  if (R_FAILED(rc)) goto done;
  started = true;
  u64 offset = 0;
  while (offset < release.size) {
    u32 read = 0, wrote = 0, want = release.size - offset;
    if (want > UPDATE_IO_SIZE) want = UPDATE_IO_SIZE;
    if (cancelled()) goto done;
    rc = FSFILE_Read(input, &read, offset, data, want);
    if (R_FAILED(rc) || read != want) goto done;
    rc = FSFILE_Write(output, &wrote, offset, data, read, 0);
    if (R_FAILED(rc) || wrote != read) goto done;
    offset += read; progress((unsigned)(offset * 100 / release.size));
  }
  rc = AM_FinishCiaInstall(output); started = false;
  ok = R_SUCCEEDED(rc);
done:
  if (started) AM_CancelCIAInstall(output);
  if (input) FSFILE_Close(input);
  free(data);
  UpdateLog("Updater CIA installation: result=%08lx success=%d", (unsigned long)rc, ok);
  amExit(); return ok;
}
static bool install_3dsx(void) {
  if (!launch_file[0]) return false;
  unsigned char magic[4]; FILE *f = fopen(UPDATE_PART, "rb");
  if (!f) return false;
  bool ok = fread(magic, 1, 4, f) == 4 && !memcmp(magic, "3DSX", 4); fclose(f);
  if (!ok || cancelled()) return false;
  char backup[800]; snprintf(backup, sizeof(backup), "%s.update-backup", launch_file);
  struct stat st;
  if (stat(backup, &st) == 0 || rename(launch_file, backup) != 0) return false;
  if (rename(UPDATE_PART, launch_file) != 0) { rename(backup, launch_file); return false; }
  remove(backup); return true;
}
static void run_job(void *arg) {
  bool net_ready = false, curl_ready = false, ssl_ready = false;
  bool ok = false; Transfer t = {0};
  UpdateStatus s; Updater_GetStatus(&s);
  /* Shared with the debug FTP server (net_3ds.c). */
  if (!Net3DS_Acquire()) goto done;
  net_ready = true;
  if (!Net3DS_WifiConnected()) { publish(UPDATE_ERROR, "NO WI-FI CONNECTION"); goto done; }
  // The linked mbedTLS entropy callback needs the SSL service on both models.
  if (R_FAILED(sslcInit(0))) { publish(UPDATE_ERROR, "TLS SERVICE FAILED"); goto done; }
  ssl_ready = true;
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) goto done;
  curl_ready = true;
  if (!download_job) {
    // The list, not /latest: with + BETAS the newest of betas and stable releases wins, and
    // the notes cover every release between this build and it (Update_ParseReleases).
    const char *url = "https://api.github.com/repos/" UPDATE_REPOSITORY "/releases?per_page=100";
    if (!fetch(url, &t)) goto done;
    UpdateRelease candidate;
    // A beta build is older than the stable release of its own number, but must never be
    // offered its own pre-release page: a pre-release is compared as "-beta" too.
    const char *installed = TMC_PORT_IS_BETA ? TMC_PORT_VERSION "-beta" : TMC_PORT_VERSION;
    int result = Update_ParseReleases(t.data, t.size, s.prerelease, homebrew, installed, &candidate);
    if (result < 0) { publish(UPDATE_ERROR, "INVALID RELEASE DATA"); goto done; }
    if (!result) { publish(UPDATE_EMPTY, s.prerelease ? "NO RELEASE AVAILABLE" : "NO STABLE RELEASE AVAILABLE"); ok = true; goto done; }
    LightLock_Lock(&lock); release = candidate;
    snprintf(status.version, sizeof(status.version), "%s%s", release.version, release.prerelease ? " BETA" : "");
    LightLock_Unlock(&lock);
    char candidate_version[64];
    snprintf(candidate_version, sizeof(candidate_version), "%s%s", release.version, release.prerelease ? "-beta" : "");
    bool newer = Update_IsNewer(candidate_version, installed);
    publish(newer ? UPDATE_AVAILABLE : UPDATE_CURRENT, newer ? "UPDATE AVAILABLE" : "YOU ARE UP TO DATE");
    if (newer && boot_check) auto_active = true;
    ok = true;
  } else {
    if (!Update_AllowedDownloadUrl(release.url) || !enough_space(release.size * 2ull + 16 * 1024 * 1024)) {
      publish(UPDATE_ERROR, "NOT ENOUGH SD SPACE"); goto done;
    }
    if (homebrew && !launch_file[0]) { publish(UPDATE_ERROR, "3DSX LAUNCH PATH UNKNOWN"); goto done; }
    t.expected = release.size;
    if (!open_download(&t)) { publish(UPDATE_ERROR, "CANNOT WRITE TO SD CARD"); goto done; }
    bool fetched = fetch(release.url, &t);
    if (!close_download(&t, fetched)) goto done;
    publish(UPDATE_VERIFYING, "VERIFYING DOWNLOAD");
    if (!verify_file()) { publish(UPDATE_ERROR, "DOWNLOAD CHECK FAILED"); goto done; }
    if (cancelled() || !aptIsActive()) goto done;
    bool home_allowed = aptIsHomeAllowed(), sleep_allowed = aptIsSleepAllowed();
    aptSetHomeAllowed(false); aptSetSleepAllowed(false);
    publish(UPDATE_INSTALLING, "INSTALLING UPDATE");
    ok = homebrew ? install_3dsx() : install_cia();
    aptSetSleepAllowed(sleep_allowed); aptSetHomeAllowed(home_allowed);
    publish(ok ? UPDATE_DONE : UPDATE_ERROR, ok ? "UPDATE INSTALLED" : "INSTALLATION FAILED");
    if (ok && !homebrew) Platform3DS_RequestRelaunch();
  }
done:
  if (!download_job) boot_check = false; // a failed boot check raises nothing
  if (t.file) fclose(t.file);
  free(t.data);
  if (download_job) remove(UPDATE_PART);
  if (curl_ready) curl_global_cleanup();
  if (ssl_ready) sslcExit();
  if (net_ready) Net3DS_Release();
  Updater_GetStatus(&s);
  if (!ok && s.state != UPDATE_ERROR)
    publish(UPDATE_ERROR, cancelled() ? "UPDATE CANCELLED" : "CONNECTION FAILED - RETRY");
  __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
}
static void start(bool downloading) {
  if (!initialized || Updater_Busy()) return;
  if (worker) { threadJoin(worker, UINT64_MAX); threadFree(worker); worker = NULL; }
  if (downloading) { UpdateStatus s; Updater_GetStatus(&s); if (s.state != UPDATE_AVAILABLE) return; }
  if (!downloading) { LightLock_Lock(&lock); status.version[0] = 0; LightLock_Unlock(&lock); }
  download_job = downloading;
  __atomic_store_n(&cancel, false, __ATOMIC_RELEASE);
  __atomic_store_n(&busy, true, __ATOMIC_RELEASE);
  publish(downloading ? UPDATE_DOWNLOADING : UPDATE_CHECKING,
          downloading ? "DOWNLOADING UPDATE" : "CHECKING FOR UPDATES");
  // Below the game thread (and so below the audio worker, one above it on the
  // same core): at the audio worker's priority the TLS handshake held core 0 for
  // whole buffers and the sound broke up on the Old 3DS while checking. It runs
  // in the time the game leaves each frame.
  s32 caller_priority = 0x30;
  svcGetThreadPriority(&caller_priority, CUR_THREAD_HANDLE);
  int network_priority = caller_priority < 0x3F ? caller_priority + 1 : caller_priority;
  worker = threadCreate(run_job, NULL, 96 * 1024, network_priority, 0, false);
  if (!worker) { __atomic_store_n(&busy, false, __ATOMIC_RELEASE); publish(UPDATE_ERROR, "CANNOT START UPDATE"); }
}
void Updater_Check(void) { start(false); }
void Updater_Download(void) { start(true); }
void Updater_SetChannel(bool pre) {
  if (!initialized || Updater_Busy()) return;
  const char *temp = CHANNEL_FILE ".tmp", *backup = CHANNEL_FILE ".bak";
  FILE *f = fopen(temp, "wb"); bool saved = false;
  if (f) { saved = fprintf(f, "%d\n", pre ? 1 : 0) > 0; if (fclose(f)) saved = false; }
  struct stat st; bool had_old = stat(CHANNEL_FILE, &st) == 0;
  if (saved && had_old) { remove(backup); saved = rename(CHANNEL_FILE, backup) == 0; }
  if (saved && rename(temp, CHANNEL_FILE) != 0) {
    if (had_old) rename(backup, CHANNEL_FILE);
    saved = false;
  }
  if (!saved) { remove(temp); publish(UPDATE_ERROR, "CANNOT SAVE UPDATE CHANNEL"); return; }
  remove(backup);
  LightLock_Lock(&lock); status.prerelease = pre; status.version[0] = 0; LightLock_Unlock(&lock);
  Updater_Check();
}
void Updater_Init(const char *path, bool autoCheck) {
  if (initialized) return;
  LightLock_Init(&lock); initialized = true; homebrew = envIsHomebrew();
  mkdir(UPDATE_DIR, 0777);
  if (homebrew && path && !strncmp(path, "sdmc:/", 6) && strlen(path) < sizeof(launch_file) &&
      strlen(path) > 5 && !strcmp(path + strlen(path) - 5, ".3dsx")) strcpy(launch_file, path);
  struct stat st;
  if (stat(CHANNEL_FILE, &st) != 0) rename(CHANNEL_FILE ".bak", CHANNEL_FILE);
  FILE *f = fopen(CHANNEL_FILE, "rb"); if (f) { status.prerelease = fgetc(f) == '1'; fclose(f); }
  boot_check = autoCheck;
  if (autoCheck) Updater_Check();
}
void Updater_Shutdown(void) {
  if (!initialized) return;
  Updater_Cancel();
  if (worker) { threadJoin(worker, UINT64_MAX); threadFree(worker); worker = NULL; }
  initialized = false;
}

UpdatePrompt Updater_AutoPrompt(void) {
  if (!initialized || !auto_active) return UPDATE_PROMPT_NONE;
  UpdateStatus s; Updater_GetStatus(&s);
  switch (s.state) {
    case UPDATE_AVAILABLE: return UPDATE_PROMPT_ASK;
    case UPDATE_DOWNLOADING: case UPDATE_VERIFYING: case UPDATE_INSTALLING: return UPDATE_PROMPT_WORKING;
    case UPDATE_ERROR: return UPDATE_PROMPT_ERROR;
    default: return UPDATE_PROMPT_NONE; // done: the game is closing; anything else: a manual check took over
  }
}
void Updater_AutoAnswer(bool yes) {
  switch (Updater_AutoPrompt()) {
    case UPDATE_PROMPT_ASK:
      if (yes) Updater_Download(); else auto_active = false;
      break;
    case UPDATE_PROMPT_WORKING: {
      UpdateStatus s; Updater_GetStatus(&s);
      if (s.state == UPDATE_DOWNLOADING) Updater_Cancel(); // later steps are not interrupted
      break;
    }
    case UPDATE_PROMPT_ERROR: auto_active = false; break;
    default: break;
  }
}
unsigned Updater_GetNotes(char *out, unsigned capacity) {
  if (!initialized || !capacity) return 0;
  LightLock_Lock(&lock);
  snprintf(out, capacity, "%s", release.notes);
  unsigned revision = status.revision;
  LightLock_Unlock(&lock);
  return revision;
}
