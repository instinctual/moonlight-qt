// Exercise the production common-c audio callback boundary, not a replica.
#include "Limelight-internal.h"
#include <stdio.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

STREAM_CONFIGURATION StreamConfig;
AUDIO_RENDERER_CALLBACKS AudioCallbacks;
bool HighQualitySurroundSupported;
bool HighQualitySurroundEnabled;
OPUS_MULTISTREAM_CONFIGURATION NormalQualityOpusConfig;
OPUS_MULTISTREAM_CONFIGURATION HighQualityOpusConfig;
int AudioPacketDuration = 5;
static unsigned calls, stage;
static uint64_t expectedPts;
static char* expectedData;
static int expectedLength;

static void sample(char* data, int length, uint64_t pts) {
    CHECK(data == expectedData && length == expectedLength);
    CHECK(pts == expectedPts);
    calls++;
}
static int init(int config, POPUS_MULTISTREAM_CONFIGURATION opus, void* context, int flags) {
    CHECK(stage++ == 0 && opus->samplesPerFrame == 240);
    (void)config; (void)context; (void)flags;
    return 0;
}
static void start(void) { CHECK(stage++ == 1); }
static void stop(void) { CHECK(stage++ == 2); }
static void cleanup(void) { CHECK(stage++ == 3); }

int main(void) {
    const unsigned char payload[] = { 1, 2, 3 };
    const uint64_t stamps[] = { 0, 1000000000000ULL, 1000000000000ULL,
                               999999999999ULL, (UINT64_C(1) << 61) - 1 };
    AudioCallbacks.init = init;
    AudioCallbacks.start = start;
    AudioCallbacks.stop = stop;
    AudioCallbacks.cleanup = cleanup;
    AudioCallbacks.decodeAndPlaySample = sample;
    NormalQualityOpusConfig.channelCount = 2;
    NormalQualityOpusConfig.streams = 1;
    CHECK(initializeAudioStream() == 0);
    CHECK(startAudioStream(NULL, 0) == 0);
    expectedData = (char*)payload;
    expectedLength = sizeof(payload);
    for (unsigned i = 0; i < sizeof(stamps) / sizeof(stamps[0]); ++i) {
        expectedPts = stamps[i];
        CHECK(LiSubmitPlankAudioPacket(payload, sizeof(payload), 240, 0, stamps[i]) == 0);
    }
    CHECK(calls == 5);
    // Lost blocks must not manufacture PTS=0 or reuse the previous block's PTS.
    expectedData = NULL;
    expectedLength = 0;
    expectedPts = PLANK_AUDIO_PTS_UNKNOWN;
    CHECK(LiSubmitPlankAudioPacket(NULL, 0, 240, 481, 0) == 0);
    CHECK(calls == 8);
    CHECK(LiSubmitPlankAudioPacket(payload, 3, 240, 0, UINT64_C(1) << 61) == -1);
    CHECK(LiSubmitPlankAudioPacket(payload, 3, 240, 0, UINT64_MAX) == -1);
    CHECK(LiSubmitPlankAudioPacket(NULL, 3, 240, 0, 1) == -1);
    CHECK(LiSubmitPlankAudioPacket(payload, -1, 240, 0, 1) == -1);
    CHECK(LiSubmitPlankAudioPacket(payload, 3, 0, 0, 1) == -1);
    CHECK(LiSubmitPlankAudioPacket(payload, 3, 240, 240, 1) == -1);
    CHECK(calls == 8);
    stopAudioStream();
    destroyAudioStream();
    CHECK(stage == 4);
    puts("audio PTS preservation, PLC unknown-time, invalid input and lifecycle: pass");
    return 0;
}
