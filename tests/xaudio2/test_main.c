/* Windows SDK only; no audio device required.
 * cmake -S tests/xaudio2 -B build/xaudio2-test
 * cmake --build build/xaudio2-test --config Debug
 * ctest --test-dir build/xaudio2-test -C Debug --output-on-failure
 */
#define COBJMACROS
#include <windows.h>
#include <xaudio2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HRESULT com_result, create_result, master_result, source_result;
static HRESULT start_result, submit_result;
static HRESULT volume_result;
static int volume_calls;
static int com_refs, releases, master_destroys, source_destroys, failures;
static IXAudio2 engine;
static IXAudio2MasteringVoice master;
static IXAudio2SourceVoice source;
static IXAudio2MasteringVoiceVtbl master_vtable;
static IXAudio2SourceVoiceVtbl source_vtable;
static const BYTE *queued[8];
static BYTE snapshots[8][4096 * 4];
static UINT32 queued_bytes[8], queue_count;
static UINT32 max_queue = 3;   /* the buffer count the backend was given */
static LONGLONG fake_qpc;
static UINT64 fake_played;
static UINT32 fake_partial;

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); failures++; \
} } while (0)

static HRESULT fake_com_init(void)
{
    if (SUCCEEDED(com_result)) com_refs++;
    return com_result;
}

static HRESULT fake_create(IXAudio2 **out)
{
    if (SUCCEEDED(create_result)) *out = &engine;
    return create_result;
}

static HRESULT fake_master(IXAudio2MasteringVoice **out)
{
    if (SUCCEEDED(master_result)) *out = &master;
    return master_result;
}

static HRESULT fake_source(IXAudio2SourceVoice **out)
{
    if (SUCCEEDED(source_result)) *out = &source;
    return source_result;
}

static void STDMETHODCALLTYPE destroy_master(IXAudio2MasteringVoice *voice)
{
    CHECK(voice == &master);
    master_destroys++;
}

static HRESULT STDMETHODCALLTYPE master_volume(IXAudio2MasteringVoice *voice,float value,UINT32 operation)
{
    CHECK(voice==&master && value==0.0f && operation==XAUDIO2_COMMIT_NOW);
    volume_calls++;
    return volume_result;
}

static void STDMETHODCALLTYPE destroy_source(IXAudio2SourceVoice *voice)
{
    CHECK(voice == &source);
    source_destroys++;
    queue_count = 0;
}

static void check_queued(void)
{
    for (UINT32 i = 0; i < queue_count; i++)
        CHECK(memcmp(queued[i], snapshots[i], queued_bytes[i]) == 0);
}

static void fake_state(XAUDIO2_VOICE_STATE *state)
{
    memset(state, 0, sizeof(*state));
    state->BuffersQueued = queue_count;
    state->SamplesPlayed = fake_played;
}

/* A 48 MHz clock gives exactly 1000 QPC ticks per 48 kHz sample. Consume
 * real submitted buffers at the device rate; once empty, time still runs. */
static void advance_device(UINT32 samples)
{
    fake_qpc += (LONGLONG)samples * 1000;
    while (samples && queue_count) {
        UINT32 remaining = queued_bytes[0] / 4 - fake_partial;
        UINT32 n = samples < remaining ? samples : remaining;
        samples -= n;
        fake_played += n;
        fake_partial += n;
        if (n == remaining) {
            for (UINT32 i = 0; i + 1 < queue_count; i++) {
                queued[i] = queued[i + 1];
                queued_bytes[i] = queued_bytes[i + 1];
                memcpy(snapshots[i], snapshots[i + 1], queued_bytes[i]);
            }
            queue_count--;
            fake_partial = 0;
        }
    }
}

static BOOL fake_counter(LARGE_INTEGER *v) { v->QuadPart = fake_qpc; return TRUE; }
static BOOL fake_frequency(LARGE_INTEGER *v) { v->QuadPart = 48000000; return TRUE; }

static HRESULT fake_submit(const XAUDIO2_BUFFER *buffer)
{
    check_queued();
    if (FAILED(submit_result)) return submit_result;
    CHECK(queue_count < max_queue);
    if (queue_count >= max_queue) return E_FAIL;
    queued[queue_count] = buffer->pAudioData;
    queued_bytes[queue_count] = buffer->AudioBytes;
    memcpy(snapshots[queue_count], buffer->pAudioData, buffer->AudioBytes);
    queue_count++;
    return S_OK;
}

/* Replace only external APIs; compile the real backend, including its ring. */
#define CoInitializeEx(...) fake_com_init()
#define CoUninitialize() ((void)--com_refs)
#define XAudio2Create(out, ...) fake_create(out)
#undef IXAudio2_CreateMasteringVoice
#define IXAudio2_CreateMasteringVoice(engine, out, ...) fake_master(out)
#undef IXAudio2_CreateSourceVoice
#define IXAudio2_CreateSourceVoice(engine, out, ...) fake_source(out)
#undef IXAudio2_Release
#define IXAudio2_Release(...) ((void)++releases)
#undef IXAudio2SourceVoice_Start
#define IXAudio2SourceVoice_Start(...) start_result
#undef IXAudio2SourceVoice_Stop
#define IXAudio2SourceVoice_Stop(...) ((void)0)
#undef IXAudio2SourceVoice_FlushSourceBuffers
#define IXAudio2SourceVoice_FlushSourceBuffers(...) ((void)0)
#undef IXAudio2SourceVoice_GetState
#define IXAudio2SourceVoice_GetState(source, state, ...) fake_state(state)
#undef IXAudio2SourceVoice_SubmitSourceBuffer
#define IXAudio2SourceVoice_SubmitSourceBuffer(source, buffer, ...) fake_submit(buffer)
#define QueryPerformanceCounter(v) fake_counter(v)
#define QueryPerformanceFrequency(v) fake_frequency(v)
#define GetTickCount64() ((ULONGLONG)(fake_qpc / 48000))
#include "../../src/apu/apu_xaudio2.c"

/* xa2_init reads the buffer variables on each call. */
static void put_env(const char *assignment)
{
    _putenv(assignment);
}

static void reset(void)
{
    /* Fake resources are static, so even a broken cleanup can be reset. */
    g_xa2 = NULL;
    g_xa2_master = NULL;
    g_xa2_source = NULL;
    g_xa2_initialized = 0;
    g_xa2_fill = 0;
    g_xa2_underruns = 0;
    g_xa2_dry = 0;
    com_refs = releases = master_destroys = source_destroys = 0;
    queue_count = 0;
    fake_qpc = 0; fake_played = 0; fake_partial = 0;
    volume_result = S_OK; volume_calls = 0;
    com_result = create_result = master_result = source_result = S_OK;
    start_result = submit_result = S_OK;
}

static void check_init_failures(HRESULT com_hr)
{
    HRESULT *stages[] = { &create_result, &master_result, &source_result, &start_result };
    for (int stage = 0; stage < 4; stage++) {
        reset();
        com_result = com_hr;
        *stages[stage] = E_FAIL;
        CHECK(xa2_init() == 0);
        CHECK(!xa2_is_active());
        CHECK(com_refs == 0);
        CHECK(releases == (stage >= 1));
        CHECK(master_destroys == (stage >= 2));
        CHECK(source_destroys == (stage >= 3));
        CHECK(!g_xa2 && !g_xa2_master && !g_xa2_source);
        xa2_shutdown();
        CHECK(releases == (stage >= 1));
    }
}

int main(void)
{
    master_vtable.DestroyVoice = destroy_master;
    master_vtable.SetVolume = master_volume;
    source_vtable.DestroyVoice = destroy_source;
    master.lpVtbl = &master_vtable;
    source.lpVtbl = &source_vtable;
    /* The ring cases below were written for 3 x 1024; pin them there. The
     * defaults and the override are checked at the end. */
    put_env("RECOMP_AUDIO_BUF_SAMPLES=1024");
    put_env("RECOMP_AUDIO_BUF_COUNT=3");
    check_init_failures(S_OK);
    check_init_failures(S_FALSE);
    check_init_failures(RPC_E_CHANGED_MODE);
    reset();
    com_result = E_FAIL;
    CHECK(xa2_init() == 0 && !xa2_is_active() && com_refs == 0);

    reset();
    CHECK(xa2_init() == 1 && xa2_is_active());
    CHECK(xa2_init() == 1 && com_refs == 1);
    int16_t samples[1024][2];
    memset(samples, 1, sizeof(samples));
    CHECK(xa2_submit_samples(&samples[0][0], 1024) == 1);
    submit_result = E_FAIL;
    for (int i = 0; i < 3; i++) {
        memset(samples, 2 + i, sizeof(samples));
        CHECK(xa2_submit_samples(&samples[0][0], 1024) == 0);
        CHECK(g_xa2_next_buf == 1 && g_xa2_frames_written == 1);
        check_queued();
    }
    submit_result = S_OK;
    CHECK(xa2_submit_samples(&samples[0][0], 1024) == 1);
    CHECK(xa2_submit_samples(&samples[0][0], 1024) == 1);
    CHECK(xa2_submit_samples(&samples[0][0], 1024) == 0);
    CHECK(g_xa2_frames_written == 3);
    check_queued();
    /* Complete the oldest buffer, then wrap the ring into that free slot. */
    for (UINT32 i = 0; i < 2; i++) {
        queued[i] = queued[i + 1];
        queued_bytes[i] = queued_bytes[i + 1];
        memcpy(snapshots[i], snapshots[i + 1], queued_bytes[i]);
    }
    queue_count--;
    memset(samples, 9, sizeof(samples));
    CHECK(xa2_submit_samples(&samples[0][0], 1024) == 1);
    check_queued();
    xa2_shutdown();
    CHECK(!xa2_is_active() && releases == 1);
    CHECK(master_destroys == 1 && source_destroys == 1);
    xa2_shutdown();
    CHECK(releases == 1 && master_destroys == 1 && source_destroys == 1);

    /* The monitor hands over one 256-sample EP period at a time. Periods
     * accumulate in the device buffer: nothing is queued until it is full,
     * and then it holds the periods in order. */
    reset();
    CHECK(xa2_init() == 1);
    {
        int16_t period[256][2];
        for (int p = 0; p < 4; p++) {
            memset(period, 0x10 + p, sizeof(period));
            CHECK(xa2_submit_samples(&period[0][0], 256) == 1);
            CHECK(queue_count == (p == 3 ? 1u : 0u));
        }
        CHECK(queued_bytes[0] == 1024 * 4);
        for (int p = 0; p < 4; p++)
            CHECK(snapshots[0][p * 1024] == 0x10 + p
                  && snapshots[0][p * 1024 + 1023] == 0x10 + p);
        CHECK(g_xa2_next_buf == 1 && g_xa2_fill == 0);
    }
    xa2_shutdown();

    /* Buffer size and count: defaults 512 x 4, overrides reach the
     * submitted buffers and the queue limit, out-of-range values clamp. */
    {
        struct { const char *samples, *count; UINT32 want_s, want_n; } c[] = {
            { "RECOMP_AUDIO_BUF_SAMPLES=",     "RECOMP_AUDIO_BUF_COUNT=",   512, 4 },
            { "RECOMP_AUDIO_BUF_SAMPLES=256",  "RECOMP_AUDIO_BUF_COUNT=6",  256, 6 },
            { "RECOMP_AUDIO_BUF_SAMPLES=9999", "RECOMP_AUDIO_BUF_COUNT=1", 4096, 2 },
            { "RECOMP_AUDIO_BUF_SAMPLES=16",   "RECOMP_AUDIO_BUF_COUNT=99", 128, 8 },
        };
        static int16_t period[256][2];
        for (int k = 0; k < 4; k++) {
            put_env(c[k].samples);
            put_env(c[k].count);
            reset();
            max_queue = c[k].want_n;
            CHECK(xa2_init() == 1);
            CHECK(xa2_get_buffer_size() == (int)c[k].want_s);
            /* Fill every buffer and one more: exactly want_n get queued,
             * each want_s samples long. */
            for (UINT32 i = 0; i < (c[k].want_n + 1) * c[k].want_s / 128; i++)
                xa2_submit_samples(&period[0][0], 128);
            CHECK(queue_count == c[k].want_n);
            CHECK(queued_bytes[0] == c[k].want_s * 4);
            xa2_shutdown();
        }
        max_queue = 8;
    }

    /* Pacing contract (apu_core.c mcpx_apu_pace_step): submitting one
     * period only while xa2_queued_samples() < high (count * size) never
     * finds the voice full, so no staged buffer is ever dropped. The fake
     * voice completes its oldest buffer every third period. Then a voice
     * that runs dry after audio started is one underrun per dry spell. */
    {
        static int16_t period[256][2];
        int high, refused = 0;
        put_env("RECOMP_AUDIO_BUF_SAMPLES=");
        put_env("RECOMP_AUDIO_BUF_COUNT=");
        reset();
        max_queue = 4;
        CHECK(xa2_init() == 1);
        {
            int low;
            xa2_pacing_marks(xa2_get_buffer_size(), xa2_get_buffer_count(),
                             256, &low, &high);
            CHECK(high == 2048 && low == 1024);
        }
        for (int step = 0; step < 300; step++) {
            /* The voice starts playing once the queue has filled (a
             * pre-roll); before that, a fake that drains faster than the
             * first periods arrive runs dry by construction. */
            if (step >= 8 && step % 3 == 2 && queue_count > 0) { /* one played */
                for (UINT32 i = 0; i + 1 < queue_count; i++) {
                    queued[i] = queued[i + 1];
                    queued_bytes[i] = queued_bytes[i + 1];
                    memcpy(snapshots[i], snapshots[i + 1], queued_bytes[i]);
                }
                queue_count--;
            }
            if (xa2_queued_samples() < high
                    && !xa2_submit_samples(&period[0][0], 256))
                refused++;
        }
        CHECK(refused == 0);
        CHECK(xa2_underruns() == 0);
        check_queued();                     /* no queued buffer overwritten */
        queue_count = 0;                                     /* ran dry */
        xa2_queued_samples();
        xa2_queued_samples();
        CHECK(xa2_underruns() == 1);
        {
            int staged = xa2_queued_samples();    /* the partly filled buffer */
            xa2_submit_samples(&period[0][0], 256);
            xa2_submit_samples(&period[0][0], 256);          /* refilled */
            CHECK(xa2_queued_samples() == staged + 512);
        }
        queue_count = 0;
        xa2_queued_samples();
        CHECK(xa2_underruns() == 2);
        /* A pause drains the queue on purpose: not an underrun. Audio
         * after the pause runs dry as before. */
        xa2_submit_samples(&period[0][0], 256);
        xa2_submit_samples(&period[0][0], 256);
        xa2_queued_samples();
        xa2_expect_drain();
        queue_count = 0;
        xa2_queued_samples();
        CHECK(xa2_underruns() == 2);
        xa2_submit_samples(&period[0][0], 256);
        xa2_submit_samples(&period[0][0], 256);
        CHECK(xa2_queued_samples() > 0);
        queue_count = 0;
        xa2_queued_samples();
        CHECK(xa2_underruns() == 3);
        xa2_shutdown();
        max_queue = 8;
    }

    /* Buffers that are not multiples of the 256-sample period: the marks
     * are exact in samples, and pacing a voice that plays at the device
     * rate (in 32-sample steps, starting once the pacer first waits) never
     * drops or overwrites a buffer. 320 x 3 keeps the voice fed; 128 x 2
     * holds one period, so it refills only when empty (underruns, but no
     * loss). */
    {
        struct { const char *s, *n; int b, cnt, low, high, fed; } c[] = {
            { "RECOMP_AUDIO_BUF_SAMPLES=320", "RECOMP_AUDIO_BUF_COUNT=3", 320, 3, 480, 960, 1 },
            { "RECOMP_AUDIO_BUF_SAMPLES=128", "RECOMP_AUDIO_BUF_COUNT=2", 128, 2,  64, 128, 0 },
        };
        static int16_t period[256][2];
        for (int k = 0; k < 2; k++) {
            int low, high, refused = 0, started = 0, credit = 0, submits = 0;
            put_env(c[k].s);
            put_env(c[k].n);
            reset();
            max_queue = (UINT32)c[k].cnt;
            CHECK(xa2_init() == 1);
            xa2_pacing_marks(xa2_get_buffer_size(), xa2_get_buffer_count(),
                             256, &low, &high);
            if (low != c[k].low || high != c[k].high) {
                printf("FAIL: %d x %d marks %d..%d, want %d..%d\n",
                       c[k].cnt, c[k].b, low, high, c[k].low, c[k].high);
                failures++;
            }
            for (int step = 0; step < 20000; step++) {
                if (started) {
                    credit += 32;
                    while (credit >= c[k].b && queue_count > 0) {
                        for (UINT32 i = 0; i + 1 < queue_count; i++) {
                            queued[i] = queued[i + 1];
                            queued_bytes[i] = queued_bytes[i + 1];
                            memcpy(snapshots[i], snapshots[i + 1], queued_bytes[i]);
                        }
                        queue_count--;
                        credit -= c[k].b;
                    }
                    if (queue_count == 0) credit = 0;
                }
                if (xa2_queued_samples() < high) {
                    if (!xa2_submit_samples(&period[0][0], 256)) refused++;
                    submits++;
                } else {
                    started = 1;
                }
            }
            printf("pacing %d x %d: marks %d..%d samples, %d periods, "
                   "%d refused, %u underruns\n", c[k].cnt, c[k].b, low, high,
                   submits, refused, xa2_underruns());
            CHECK(refused == 0);
            check_queued();
            CHECK(submits > 20000 * 32 / 256 - 8);   /* kept the device rate */
            if (c[k].fed) CHECK(xa2_underruns() == 0);
            xa2_shutdown();
        }
        max_queue = 8;
    }

    put_env("RECOMP_AUDIO_BUF_SAMPLES=512");
    put_env("RECOMP_AUDIO_BUF_COUNT=4");
    max_queue = 4;
    /* A known 100 ms producer stall AFTER the submitted buffer drains.
     * Poll throughout: one dry spell, exactly 4800 missing device frames.
     * Also prove the count alone can rise at an instantaneous refill with
     * no lost playback time. The raw trace must retain both observations. */
    {
        UINT64 played0, played1;
        LONGLONG t0;
        reset(); _putenv_s("RECOMP_AUDIO_DEVICE_TRACE", "1");
        CHECK(xa2_init() == 1);
        CHECK(xa2_submit_samples(&samples[0][0], 512) == 1);
        t0 = fake_qpc; played0 = xa2_samples_played();
        advance_device(512);
        xa2_queued_samples();
        CHECK(xa2_underruns() == 1);
        for (int i = 0; i < 100; i++) {
            advance_device(48); /* one millisecond without a producer */
            xa2_queued_samples();
        }
        played1 = xa2_samples_played();
        CHECK(xa2_underruns() == 1);
        CHECK((fake_qpc - t0) / 1000 - (LONGLONG)(played1 - played0) == 4800);
        CHECK(xa2_submit_samples(&samples[0][0], 512) == 1);
        CHECK(xa2_queued_samples() == 512);
        t0 = fake_qpc; played0 = xa2_samples_played();
        advance_device(512);
        xa2_queued_samples();  /* instantaneous queue-empty observation */
        CHECK(xa2_underruns() == 2);
        CHECK(xa2_submit_samples(&samples[0][0], 512) == 1);
        CHECK((fake_qpc - t0) / 1000 - (LONGLONG)(xa2_samples_played() - played0) == 0);
        CHECK(g_xa2_submitted_samples == 1536);
        CHECK(g_xa2_qpc_hz == 48000000);
        xa2_shutdown();
        CHECK(xa2_init() == 1);
        CHECK(xa2_underruns() == 0 && g_xa2_submitted_samples == 0);
        xa2_shutdown(); _putenv_s("RECOMP_AUDIO_DEVICE_TRACE", "");
    }
    reset();_putenv_s("RECOMP_AUDIO_MUTE","1");
    volume_result=E_FAIL;
    CHECK(xa2_init()==0 && !xa2_is_active());
    CHECK(volume_calls==1 && master_destroys==1 && source_destroys==0 && releases==1 && com_refs==0);
    reset();_putenv_s("RECOMP_AUDIO_MUTE","1");
    CHECK(xa2_init()==1 && volume_calls==1);
    memset(samples,7,sizeof(samples));
    CHECK(xa2_submit_samples(&samples[0][0],512)==1);
    CHECK(memcmp(queued[0],samples,512*2*sizeof(int16_t))==0);
    check_queued();xa2_shutdown();_putenv_s("RECOMP_AUDIO_MUTE","");

    /* The init line names the sizes (last, since stderr is redirected). */
    {
        char line[2048] = "";
        FILE *f;
        put_env("RECOMP_AUDIO_BUF_SAMPLES=256");
        put_env("RECOMP_AUDIO_BUF_COUNT=5");
        reset();
        max_queue = 5;
        _putenv_s("RECOMP_AUDIO_DEVICE_TRACE", "1");
        fflush(stderr);
        if (freopen("xa2_init_line.txt", "w", stderr)) {
            xa2_init();
            CHECK(xa2_submit_samples(&samples[0][0], 256) == 1);
            advance_device(48000);
            xa2_queued_samples();
            fflush(stderr);
            xa2_shutdown();
            fclose(stderr);
            f = fopen("xa2_init_line.txt", "r");
            if (f) { size_t n = fread(line, 1, sizeof(line) - 1, f); line[n] = 0; fclose(f); }
            if (!strstr(line, "5 x 256-sample buffers")) {
                printf("FAIL: init line does not name 5 x 256: %s\n", line);
                failures++;
            }
            if (!strstr(line, "[XA2 DEVICE] qpc=48000000 qpc_hz=48000000 played=256 queued=0 submitted=256 staged=0 underruns=1")) {
                printf("FAIL: missing raw device accounting: %s\n", line);
                failures++;
            }
        }
        _putenv_s("RECOMP_AUDIO_DEVICE_TRACE", "");
    }

    printf("XAudio2 regression: %d failures\n", failures);
    return failures ? 1 : 0;
}
