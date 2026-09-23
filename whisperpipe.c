#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include "whisper.h"

#define SAMPLE_RATE   16000
#define MAX_CHUNK_SEC 10

#define DEF_MODEL_PATH    "whisper.cpp/models/ggml-small.bin"
#define DEF_MONITOR_SRC   "alsa_output.pci-0000_09_00.4.analog-stereo.monitor"
#define DEF_N_THREADS     6
#define DEF_SILENCE_RMS   100.0
#define DEF_CHUNK_SEC     3
#define NOTES_FILE        ".whisper-notes.tmp"

static const char *g_model_path  = DEF_MODEL_PATH;
static const char *g_monitor_src = DEF_MONITOR_SRC;
static int         g_n_threads   = DEF_N_THREADS;
static double      g_silence_rms = DEF_SILENCE_RMS;
static int         g_chunk_sec   = DEF_CHUNK_SEC;
static int         g_chunk_samples = 0;  /* set in main */
static int         g_model_given   = 0;   /* set if -m/--model passed */

/* buffers sized for the max chunk */
static int16_t g_pcm[SAMPLE_RATE * MAX_CHUNK_SEC];
static float   g_flt[SAMPLE_RATE * MAX_CHUNK_SEC];

static struct whisper_context *g_ctx   = NULL;
static FILE                    *g_notes = NULL;
static volatile sig_atomic_t g_running = 1;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;
static int g_have_chunk  = 0;
static int g_worker_done = 1;
static struct timespec g_chunk_start;

/* ---------- helpers ---------- */

static void on_signal(int sig) { (void)sig; g_running = 0; }

/* join `rel` onto the executable's own directory (/proc/self/exe), so paths
 * work regardless of the cwd. Returns 1 on success. */
static int bin_dir_join(char *out, size_t out_sz, const char *rel) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    if (!slash) return 0;
    *slash = '\0';                          /* exe now holds the binary's dir */
    int len = snprintf(out, out_sz, "%s/%s", exe, rel);
    return (len > 0 && (size_t)len < out_sz) ? 1 : 0;
}

/* silence whisper/ggml log chatter */
static void cb_log_disable(enum ggml_log_level, const char *, void *) { }

static double chunk_rms(const int16_t *p, int n) {
    double acc = 0;
    for (int i = 0; i < n; i++) acc += (double)p[i] * p[i];
    return sqrt(acc / n);
}

static const char *trim(const char *t) {
    while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
    return t;
}

static void print_segment(int i, const char *lang) {
    const char *text = trim(whisper_full_get_segment_text(g_ctx, i));
    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\n' ||
        text[len-1] == '\r' || text[len-1] == '\t'))
        len--;
    if (len == 0) return;

    int64_t t0_ms = whisper_full_get_segment_t0(g_ctx, i) / 10;
    time_t sec = g_chunk_start.tv_sec + (time_t)(t0_ms / 1000);
    struct tm tmv;
    localtime_r(&sec, &tmv);

    char wall[16], line[2048];
    strftime(wall, sizeof wall, "%H:%M:%S", &tmv);
    int n = snprintf(line, sizeof line, "[%s] [%s] %.*s\n",
                     wall, lang, (int)len, text);
    if (n < 0) return;
    if ((size_t)n >= sizeof line) n = (int)sizeof(line) - 1; /* snprintf returns the
        *would-be* length, not what fit — clamp before writing */
        fwrite(line, 1, n, stdout);
    fflush(stdout);
    if (g_notes) { fwrite(line, 1, n, g_notes); fflush(g_notes); }
}

static void *worker(void *arg) {
    (void)arg;
    while (g_running) {
        pthread_mutex_lock(&g_lock);
        while (!g_have_chunk && g_running)
            pthread_cond_wait(&g_cond, &g_lock);
        if (!g_running) { pthread_mutex_unlock(&g_lock); break; }
        g_have_chunk = 0;
        pthread_mutex_unlock(&g_lock);

        /* silence gate */
        if (chunk_rms(g_pcm, g_chunk_samples) < g_silence_rms) {
            pthread_mutex_lock(&g_lock);
            g_worker_done = 1;
            pthread_cond_signal(&g_cond);
            pthread_mutex_unlock(&g_lock);
            continue;
        }

        for (int i = 0; i < g_chunk_samples; i++)
            g_flt[i] = (float)g_pcm[i] / 32768.0f;

        struct whisper_full_params params =
        whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.n_threads   = g_n_threads;
        params.language    = "auto";
        params.suppress_nst = true;

        if (whisper_full(g_ctx, params, g_flt, g_chunk_samples) == 0) {
            const char *lang = whisper_lang_str(
                whisper_full_lang_id(g_ctx));
            if (!lang) lang = "??";
            for (int i = 0; i < whisper_full_n_segments(g_ctx); i++)
                print_segment(i, lang);
        }

        pthread_mutex_lock(&g_lock);
        g_worker_done = 1;
        pthread_cond_signal(&g_cond);
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

/* ---------- CLI ---------- */

static void usage(const char *prog) {
    fprintf(stderr,
            "whisperpipe — real-time local STT (PipeWire + whisper.cpp)\n\n"
            "Usage: %s [options]\n\n"
            "  -m, --model PATH     GGML model file       [%s]\n"
            "  -s, --source NAME    PipeWire monitor source [%s]\n"
            "  -t, --threads N      Inference threads     [%d]\n"
            "  -r, --rms THRESHOLD  Silence RMS gate      [%.0f]\n"
            "  -c, --chunk SECS     Chunk duration (1-%d) [%d]\n"
            "  -h, --help           Show this help\n",
            prog, DEF_MODEL_PATH, DEF_MONITOR_SRC,
            DEF_N_THREADS, DEF_SILENCE_RMS, MAX_CHUNK_SEC, DEF_CHUNK_SEC);
}

/* ---------- main ---------- */

int main(int argc, char *argv[]) {
    static struct option long_opts[] = {
        {"model",   required_argument, 0, 'm'},
        {"source",  required_argument, 0, 's'},
        {"threads", required_argument, 0, 't'},
        {"rms",     required_argument, 0, 'r'},
        {"chunk",   required_argument, 0, 'c'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:s:t:r:c:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'm': g_model_path  = optarg; g_model_given = 1; break;
            case 's': g_monitor_src = optarg; break;
            case 't': g_n_threads   = atoi(optarg); break;
            case 'r': g_silence_rms = atof(optarg); break;
            case 'c': g_chunk_sec   = atoi(optarg); break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
        }
    }

    if (g_chunk_sec < 1) g_chunk_sec = 1;
    if (g_chunk_sec > MAX_CHUNK_SEC) {
        fprintf(stderr, "chunk clamped to %d s\n", MAX_CHUNK_SEC);
        g_chunk_sec = MAX_CHUNK_SEC;
    }
    g_chunk_samples = SAMPLE_RATE * g_chunk_sec;

    /* resolve the default model path next to the binary */
    static char resolved_model[4096 + 64];
    if (!g_model_given && bin_dir_join(resolved_model, sizeof resolved_model, DEF_MODEL_PATH))
        g_model_path = resolved_model;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    whisper_log_set(cb_log_disable, NULL);

    struct whisper_context_params cparams = whisper_context_default_params();
    g_ctx = whisper_init_from_file_with_params(g_model_path, cparams);
    if (!g_ctx) {
        fprintf(stderr, "could not load model: %s\n", g_model_path);
        return 1;
    }

    g_notes = fopen(NOTES_FILE, "w");
    if (!g_notes) {
        fprintf(stderr, "could not open %s\n", NOTES_FILE);
        whisper_free(g_ctx);
        return 1;
    }

    pa_sample_spec spec;
    spec.format   = PA_SAMPLE_S16LE;
    spec.rate     = SAMPLE_RATE;
    spec.channels = 1;

    pa_buffer_attr attr;
    attr.fragsize  = (uint32_t)(g_chunk_samples * sizeof(int16_t));
    attr.maxlength = (uint32_t)(g_chunk_samples * sizeof(int16_t) * 2);
    attr.tlength   = (uint32_t)-1;
    attr.prebuf    = (uint32_t)-1;
    attr.minreq    = (uint32_t)-1;

    int pa_err = 0;
    pa_simple *s = pa_simple_new(
        NULL, "whisperpipe", PA_STREAM_RECORD,
        g_monitor_src, "capture",
        &spec, NULL, &attr, &pa_err);
    if (!s) {
        fprintf(stderr, "pa_simple_new failed: %s\n", pa_strerror(pa_err));
        fprintf(stderr, "  is pipewire-pulse running?\n");
        fprintf(stderr, "  source: %s\n", g_monitor_src);
        fclose(g_notes);
        remove(NOTES_FILE);
        whisper_free(g_ctx);
        return 1;
    }

    printf("whisperpipe\n");
    printf("  model:   %s\n", g_model_path);
    printf("  source:  %s\n", g_monitor_src);
    printf("  threads: %d | chunk: %ds | rms gate: %.0f\n",
           g_n_threads, g_chunk_sec, g_silence_rms);
    printf("\nCtrl+C to stop\n\n");

    pthread_t tid;
    pthread_create(&tid, NULL, worker, NULL);

    int chunk_bytes = g_chunk_samples * (int)sizeof(int16_t);

    while (g_running) {
        pthread_mutex_lock(&g_lock);
        while (!g_worker_done && g_running)
            pthread_cond_wait(&g_cond, &g_lock);
        g_worker_done = 0;
        pthread_mutex_unlock(&g_lock);
        if (!g_running) break;

        int rc;
        do {
            rc = pa_simple_read(s, g_pcm, chunk_bytes, NULL);
        } while (rc < 0 && errno == EINTR && g_running);
        if (rc < 0) break;

        clock_gettime(CLOCK_REALTIME, &g_chunk_start);

        pthread_mutex_lock(&g_lock);
        g_have_chunk = 1;
        pthread_cond_signal(&g_cond);
        pthread_mutex_unlock(&g_lock);
    }

    pthread_mutex_lock(&g_lock);
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);

    pthread_join(tid, NULL);
    pa_simple_flush(s, 0);

    fclose(g_notes);
    remove(NOTES_FILE);
    printf("\ndone — %s erased\n", NOTES_FILE);

    whisper_free(g_ctx);
    return 0;
}
