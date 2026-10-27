#define _POSIX_C_SOURCE 200809L
#include "llm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LLM_RESPONSE_MAX  4096
#define LLM_PROMPT_MAX    8192
#define LLM_URL_MAX        256

/* Extract value of first "content":"..." JSON pair, unescaping common escapes.
 * \uXXXX is replaced with '?' to keep this dependency-free. */
static int json_extract_content(const char *src, char *out, size_t cap)
{
    if (!src || !out || cap == 0) return 0;
    out[0] = 0;
    const char *p = strstr(src, "\"content\"");
    if (!p) return 0;
    p += 9;
    while (*p && *p != ':') p++;
    if (*p != ':') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return 0;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        char c;
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'u':
                    for (int k = 0; k < 4 && *(p + 1); k++) p++;
                    c = '?';
                    break;
                default:   c = *p;   break;
            }
            p++;
        } else {
            c = *p++;
        }
        if (i + 1 < cap) out[i++] = c;
    }
    out[i] = 0;
    return 1;
}

static size_t json_escape(const char *src, char *out, size_t cap)
{
    size_t i = 0;
    for (const char *p = src; *p; p++) {
        char buf[8];
        int n = 0;
        switch (*p) {
            case '"':  buf[n++] = '\\'; buf[n++] = '"';  break;
            case '\\': buf[n++] = '\\'; buf[n++] = '\\'; break;
            case '\n': buf[n++] = '\\'; buf[n++] = 'n';  break;
            case '\r': buf[n++] = '\\'; buf[n++] = 'r';  break;
            case '\t': buf[n++] = '\\'; buf[n++] = 't';  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    n = snprintf(buf, sizeof(buf), "\\u%04x",
                                 (unsigned)(unsigned char)*p);
                } else {
                    buf[n++] = *p;
                }
                break;
        }
        for (int k = 0; k < n; k++) {
            if (i + 1 < cap) out[i++] = buf[k];
        }
    }
    if (cap) out[i] = 0;
    return i;
}

static void trim_inplace(char *s)
{
    size_t L = strlen(s);
    while (L > 0 && (s[L-1] == ' ' || s[L-1] == '\n' ||
                     s[L-1] == '\r' || s[L-1] == '\t')) {
        s[--L] = 0;
    }
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') i++;
    if (i > 0) memmove(s, s + i, L - i + 1);
}

/* ----------------------------------------------------------------------
 * NATIVE client: a local llama-server reached over libcurl + raw POSIX
 * sockets + fork()/exec(), driven by a background SDL worker thread.
 * The browser client (emscripten_fetch) is the #else branch at EOF.
 * -------------------------------------------------------------------- */
#ifndef __EMSCRIPTEN__

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <SDL2/SDL.h>
#include <curl/curl.h>

struct LLMSystem {
    pid_t        server_pid;
    char         url[LLM_URL_MAX];

    SDL_Thread  *worker;
    SDL_mutex   *mtx;
    SDL_atomic_t status;
    SDL_atomic_t shutdown_req;
    SDL_atomic_t callback_pending;

    char         prompt[LLM_PROMPT_MAX];
    int          n_predict;
    float        temperature;
    LLMCallback  callback;
    void        *userdata;

    char         response[LLM_RESPONSE_MAX];
};

struct curl_buf {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud);

static int tcp_can_connect(const char *host, int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) { close(s); return 0; }
    int ok = (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    close(s);
    return ok;
}

static int health_is_ok(const char *url_base)
{
    char url[LLM_URL_MAX + 16];
    snprintf(url, sizeof(url), "%s/health", url_base);
    CURL *c = curl_easy_init();
    if (!c) return 0;
    long code = 0;
    struct curl_buf b = {0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    free(b.data);
    return (rc == CURLE_OK && code == 200);
}

static pid_t spawn_server(const char *bin, const char *model,
                           const char *host, int port)
{
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open("llama_server.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); close(fd); }

#ifdef __linux__
        /* die together with the game: a force-quit / crash that skips
         * llm_shutdown won't leave a zombie llama-server squatting on the port */
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(0);   /* parent vanished before prctl */
#else
        (void)parent;
#endif

        char port_s[16];
        snprintf(port_s, sizeof(port_s), "%d", port);
        execl(bin, bin,
              "--model",        model,
              "--host",         host,
              "--port",         port_s,
              "--ctx-size",     "2048",
              "--threads",      "3",
              "--n-gpu-layers", "0",
              (char*)NULL);
        fprintf(stderr, "[llm] exec '%s' failed: %s\n", bin, strerror(errno));
        _exit(127);
    }
    return pid;
}

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *ud)
{
    struct curl_buf *b = ud;
    size_t add = size * nmemb;
    if (b->len + add + 1 > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;
        while (new_cap < b->len + add + 1) new_cap *= 2;
        char *p = realloc(b->data, new_cap);
        if (!p) return 0;
        b->data = p;
        b->cap  = new_cap;
    }
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = 0;
    return add;
}


static int worker_main(void *arg)
{
    LLMSystem *llm = arg;

    while (!SDL_AtomicGet(&llm->shutdown_req)) {
        if ((LLMStatus)SDL_AtomicGet(&llm->status) != LLM_BUSY) {
            SDL_Delay(20);
            continue;
        }

        SDL_LockMutex(llm->mtx);
        char  prompt[LLM_PROMPT_MAX];
        int   n_predict   = llm->n_predict;
        float temperature = llm->temperature;
        size_t pl = strlen(llm->prompt);
        if (pl >= sizeof(prompt)) pl = sizeof(prompt) - 1;
        memcpy(prompt, llm->prompt, pl);
        prompt[pl] = 0;
        SDL_UnlockMutex(llm->mtx);

        size_t esc_cap = pl * 6 + 1;
        char  *escaped = malloc(esc_cap);
        if (!escaped) {
            SDL_LockMutex(llm->mtx);
            snprintf(llm->response, sizeof(llm->response), "(out of memory)");
            SDL_UnlockMutex(llm->mtx);
            SDL_AtomicSet(&llm->status, LLM_ERROR);
            SDL_AtomicSet(&llm->callback_pending, 1);
            continue;
        }
        json_escape(prompt, escaped, esc_cap);
        size_t esc_len = strlen(escaped);

        size_t body_cap = esc_len + 256;
        char  *body     = malloc(body_cap);
        if (!body) {
            free(escaped);
            SDL_AtomicSet(&llm->status, LLM_ERROR);
            SDL_AtomicSet(&llm->callback_pending, 1);
            continue;
        }
        int written = snprintf(body, body_cap,
            "{\"prompt\":\"%s\","
            "\"n_predict\":%d,"
            "\"temperature\":%.3f,"
            "\"cache_prompt\":true,"
            "\"stop\":[\"<end_of_turn>\",\"<eos>\",\"<|user|>\",\"PLAYER:\"]}",
            escaped, n_predict, (double)temperature);
        free(escaped);

        char url[LLM_URL_MAX];
        snprintf(url, sizeof(url), "%s/completion", llm->url);

        CURL *curl = curl_easy_init();
        struct curl_buf rbuf = {0};
        long http_code = 0;
        CURLcode rc = CURLE_FAILED_INIT;
        if (curl) {
            struct curl_slist *hdr = NULL;
            hdr = curl_slist_append(hdr, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)written);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rbuf);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            rc = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_slist_free_all(hdr);
            curl_easy_cleanup(curl);
        }
        free(body);

        bool ok = (rc == CURLE_OK && http_code == 200 && rbuf.data);
        SDL_LockMutex(llm->mtx);
        if (ok) {
            char content[LLM_RESPONSE_MAX];
            if (json_extract_content(rbuf.data, content, sizeof(content))) {
                strncpy(llm->response, content, sizeof(llm->response) - 1);
                llm->response[sizeof(llm->response) - 1] = 0;
                trim_inplace(llm->response);
                if (llm->response[0] == 0) {
                    snprintf(llm->response, sizeof(llm->response),
                             "(empty response)");
                    ok = false;
                }
            } else {
                snprintf(llm->response, sizeof(llm->response),
                         "(could not parse JSON content field)");
                ok = false;
            }
        } else {
            if (rbuf.data && rbuf.len > 0) {
                snprintf(llm->response, sizeof(llm->response),
                         "(http %ld) %.300s", http_code, rbuf.data);
            } else {
                snprintf(llm->response, sizeof(llm->response),
                         "(curl error: %s)", curl_easy_strerror(rc));
            }
        }
        SDL_UnlockMutex(llm->mtx);
        free(rbuf.data);

        SDL_AtomicSet(&llm->status, ok ? LLM_DONE : LLM_ERROR);
        SDL_AtomicSet(&llm->callback_pending, 1);
    }
    return 0;
}

LLMSystem *llm_init(const char *server_binary,
                    const char *model_path,
                    const char *host,
                    int port)
{
    if (!server_binary || !model_path || !host) return NULL;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return NULL;

    LLMSystem *llm = calloc(1, sizeof(*llm));
    if (!llm) { curl_global_cleanup(); return NULL; }

    snprintf(llm->url, sizeof(llm->url), "http://%s:%d", host, port);
    SDL_AtomicSet(&llm->status, LLM_IDLE);
    SDL_AtomicSet(&llm->shutdown_req, 0);
    SDL_AtomicSet(&llm->callback_pending, 0);
    llm->mtx = SDL_CreateMutex();
    if (!llm->mtx) { free(llm); curl_global_cleanup(); return NULL; }

    /* Reuse a healthy server that's already listening (e.g. left over from a
     * previous session): skips the model-reload wait and sidesteps any port
     * conflict.  We don't own it, so server_pid stays 0 and we won't kill it. */
    int reused = (tcp_can_connect(host, port) && health_is_ok(llm->url));
    if (reused) {
        llm->server_pid = 0;
        fprintf(stderr, "[llm] reusing server already listening on %s\n", llm->url);
    } else {
        llm->server_pid = spawn_server(server_binary, model_path, host, port);
        if (llm->server_pid <= 0) {
            SDL_DestroyMutex(llm->mtx);
            free(llm);
            curl_global_cleanup();
            return NULL;
        }
        fprintf(stderr, "[llm] launched server pid=%d, waiting for %s ...\n",
                (int)llm->server_pid, llm->url);

        int ready = 0;
        for (int i = 0; i < 1200; i++) {
            SDL_Delay(100);
            int status;
            pid_t r = waitpid(llm->server_pid, &status, WNOHANG);
            if (r == llm->server_pid) {
                fprintf(stderr, "[llm] server died during startup (status %d)\n",
                        status);
                SDL_DestroyMutex(llm->mtx);
                free(llm);
                curl_global_cleanup();
                return NULL;
            }
            if (!tcp_can_connect(host, port)) continue;
            if (health_is_ok(llm->url)) { ready = 1; break; }
        }
        if (!ready) {
            fprintf(stderr, "[llm] server not ready after 120s, killing\n");
            kill(llm->server_pid, SIGTERM);
            waitpid(llm->server_pid, NULL, 0);
            SDL_DestroyMutex(llm->mtx);
            free(llm);
            curl_global_cleanup();
            return NULL;
        }
        fprintf(stderr, "[llm] server ready\n");
    }

    llm->worker = SDL_CreateThread(worker_main, "llm-worker", llm);
    if (!llm->worker) {
        if (llm->server_pid > 0) {
            kill(llm->server_pid, SIGTERM);
            waitpid(llm->server_pid, NULL, 0);
        }
        SDL_DestroyMutex(llm->mtx);
        free(llm);
        curl_global_cleanup();
        return NULL;
    }
    return llm;
}

LLMStatus llm_status(const LLMSystem *llm)
{
    if (!llm) return LLM_ERROR;
    return (LLMStatus)SDL_AtomicGet((SDL_atomic_t*)&llm->status);
}

bool llm_send(LLMSystem *llm,
              const char *prompt,
              int n_predict,
              float temperature,
              LLMCallback on_done,
              void *userdata)
{
    if (!llm || !prompt) return false;
    LLMStatus s = (LLMStatus)SDL_AtomicGet(&llm->status);
    if (s == LLM_BUSY) return false;

    SDL_LockMutex(llm->mtx);
    size_t pl = strlen(prompt);
    if (pl >= sizeof(llm->prompt)) pl = sizeof(llm->prompt) - 1;
    memcpy(llm->prompt, prompt, pl);
    llm->prompt[pl] = 0;
    llm->n_predict   = (n_predict > 0) ? n_predict : 80;
    llm->temperature = temperature;
    llm->callback    = on_done;
    llm->userdata    = userdata;
    llm->response[0] = 0;
    SDL_UnlockMutex(llm->mtx);

    SDL_AtomicSet(&llm->status, LLM_BUSY);
    return true;
}

void llm_pump(LLMSystem *llm)
{
    if (!llm) return;
    if (!SDL_AtomicGet(&llm->callback_pending)) return;

    LLMStatus s = (LLMStatus)SDL_AtomicGet(&llm->status);
    if (s != LLM_DONE && s != LLM_ERROR) return;

    SDL_AtomicSet(&llm->callback_pending, 0);

    char         snapshot[LLM_RESPONSE_MAX];
    LLMCallback  cb;
    void        *ud;
    SDL_LockMutex(llm->mtx);
    strncpy(snapshot, llm->response, sizeof(snapshot) - 1);
    snapshot[sizeof(snapshot) - 1] = 0;
    cb = llm->callback;
    ud = llm->userdata;
    llm->callback = NULL;
    SDL_UnlockMutex(llm->mtx);

    if (cb) cb(s == LLM_DONE, snapshot, ud);

    SDL_AtomicSet(&llm->status, LLM_IDLE);
}

void llm_shutdown(LLMSystem *llm)
{
    if (!llm) return;
    SDL_AtomicSet(&llm->shutdown_req, 1);
    if (llm->worker) {
        SDL_WaitThread(llm->worker, NULL);
        llm->worker = NULL;
    }
    if (llm->server_pid > 0) {
        kill(llm->server_pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            int status;
            pid_t r = waitpid(llm->server_pid, &status, WNOHANG);
            if (r == llm->server_pid) { llm->server_pid = -1; break; }
            SDL_Delay(100);
        }
        if (llm->server_pid > 0) {
            kill(llm->server_pid, SIGKILL);
            waitpid(llm->server_pid, NULL, 0);
        }
    }
    if (llm->mtx) SDL_DestroyMutex(llm->mtx);
    free(llm);
    curl_global_cleanup();
}

#else  /* __EMSCRIPTEN__ : browser client via the async Fetch API --------- */

#include <emscripten/fetch.h>

/* ----------------------------------------------------------------------
 * Browser LLM client.
 *
 * There is no llama-server, libcurl, socket or thread in the browser, so we
 * POST the same llama.cpp-style JSON body to a PROXY over the async Fetch
 * API.  The proxy holds any API key and talks to the real model server:
 * NEVER put a key in this client-side build.
 *
 * Proxy contract:
 *   POST <LLM_PROXY_URL>   Content-Type: application/json
 *   body : {"prompt":"...","n_predict":N,"temperature":T, ...}
 *   200  : JSON containing a  "content":"..."  string (llama.cpp shape)
 *
 * If LLM_PROXY_URL is the empty placeholder below, llm_init() returns NULL
 * and the whole dialogue system uses its built-in static fallback lines.
 * -------------------------------------------------------------------- */

/* TODO: point this at your deployed proxy, e.g.
 *   "https://your-proxy.example.com/completion"
 * Leave it empty to disable the web LLM and use fallback dialogue only.   */
#define LLM_PROXY_URL ""

struct LLMSystem {
    char        url[LLM_URL_MAX];
    int         status;            /* LLMStatus, touched on main thread only */
    int         callback_pending;  /* the "text_ready" flag set by the fetch  */
    int         n_predict;
    float       temperature;
    LLMCallback callback;
    void       *userdata;
    char       *req_body;          /* request JSON, must outlive the fetch    */
    char        response[LLM_RESPONSE_MAX];
};

/* Fetch callbacks run on the main thread between frames, so they share the
 * status/response fields with llm_pump() without any locking. */
static void web_on_success(emscripten_fetch_t *fetch)
{
    LLMSystem *llm = (LLMSystem *)fetch->userData;
    char content[LLM_RESPONSE_MAX];
    int  ok = 0;
    if (fetch->data && fetch->numBytes > 0) {
        size_t n = (size_t)fetch->numBytes;          /* data is not NUL-term'd */
        char  *raw = (char *)malloc(n + 1);
        if (raw) {
            memcpy(raw, fetch->data, n);
            raw[n] = 0;
            if (json_extract_content(raw, content, sizeof(content))) {
                trim_inplace(content);
                ok = (content[0] != 0);
            }
            free(raw);
        }
    }
    if (ok) {
        strncpy(llm->response, content, sizeof(llm->response) - 1);
        llm->response[sizeof(llm->response) - 1] = 0;
        llm->status = LLM_DONE;
    } else {
        snprintf(llm->response, sizeof(llm->response),
                 "(proxy returned no usable content)");
        llm->status = LLM_ERROR;
    }
    free(llm->req_body); llm->req_body = NULL;
    llm->callback_pending = 1;                        /* picked up by llm_pump */
    emscripten_fetch_close(fetch);
}

static void web_on_error(emscripten_fetch_t *fetch)
{
    LLMSystem *llm = (LLMSystem *)fetch->userData;
    snprintf(llm->response, sizeof(llm->response),
             "(fetch failed, http %d)", fetch->status);
    llm->status = LLM_ERROR;                          /* on_done(false) -> fallback */
    free(llm->req_body); llm->req_body = NULL;
    llm->callback_pending = 1;
    emscripten_fetch_close(fetch);
}

LLMSystem *llm_init(const char *server_binary, const char *model_path,
                    const char *host, int port)
{
    (void)server_binary; (void)model_path; (void)host; (void)port;

    if (LLM_PROXY_URL[0] == 0) {        /* no proxy -> static fallback dialogue */
        fprintf(stderr, "[llm] web: LLM_PROXY_URL unset; using fallback dialogue\n");
        return NULL;
    }
    LLMSystem *llm = (LLMSystem *)calloc(1, sizeof(*llm));
    if (!llm) return NULL;
    strncpy(llm->url, LLM_PROXY_URL, sizeof(llm->url) - 1);
    llm->status = LLM_IDLE;
    fprintf(stderr, "[llm] web: LLM proxy = %s\n", llm->url);
    return llm;
}

LLMStatus llm_status(const LLMSystem *llm)
{
    if (!llm) return LLM_ERROR;
    return (LLMStatus)llm->status;
}

bool llm_send(LLMSystem *llm, const char *prompt, int n_predict,
              float temperature, LLMCallback on_done, void *userdata)
{
    if (!llm || !prompt) return false;
    if (llm->status == LLM_BUSY) return false;        /* one request in flight */

    llm->n_predict   = (n_predict > 0) ? n_predict : 80;
    llm->temperature = temperature;
    llm->callback    = on_done;
    llm->userdata    = userdata;
    llm->response[0] = 0;

    /* Same JSON body the native worker sends to llama.cpp's /completion. */
    size_t pl      = strlen(prompt);
    size_t esc_cap = pl * 6 + 1;
    char  *escaped = (char *)malloc(esc_cap);
    if (!escaped) return false;
    json_escape(prompt, escaped, esc_cap);

    size_t body_cap = strlen(escaped) + 256;
    char  *body     = (char *)malloc(body_cap);
    if (!body) { free(escaped); return false; }
    int written = snprintf(body, body_cap,
        "{\"prompt\":\"%s\","
        "\"n_predict\":%d,"
        "\"temperature\":%.3f,"
        "\"cache_prompt\":true,"
        "\"stop\":[\"<end_of_turn>\",\"<eos>\",\"<|user|>\",\"PLAYER:\"]}",
        escaped, llm->n_predict, (double)llm->temperature);
    free(escaped);
    if (written < 0) { free(body); return false; }

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    const char *headers[] = { "Content-Type", "application/json", NULL };
    attr.requestHeaders  = headers;
    attr.requestData     = body;             /* not copied: must outlive fetch */
    attr.requestDataSize = (size_t)written;
    attr.onsuccess = web_on_success;
    attr.onerror   = web_on_error;
    attr.userData  = llm;

    llm->req_body = body;                    /* freed in the fetch callback */
    llm->status   = LLM_BUSY;
    if (!emscripten_fetch(&attr, llm->url)) {
        free(body); llm->req_body = NULL;
        llm->status = LLM_IDLE;
        return false;
    }
    return true;
}

void llm_pump(LLMSystem *llm)
{
    if (!llm) return;
    if (!llm->callback_pending) return;
    if (llm->status != LLM_DONE && llm->status != LLM_ERROR) return;

    llm->callback_pending = 0;
    LLMCallback cb = llm->callback;
    void       *ud = llm->userdata;
    bool        ok = (llm->status == LLM_DONE);
    llm->callback = NULL;
    llm->status   = LLM_IDLE;
    if (cb) cb(ok, llm->response, ud);
}

void llm_shutdown(LLMSystem *llm)
{
    if (!llm) return;
    free(llm->req_body);
    free(llm);
}

#endif /* __EMSCRIPTEN__ */

