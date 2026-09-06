#include "sdl.h"
#include <plank_audio_resampler.h>
#include <algorithm>
#include <array>

namespace {
constexpr int SampleRate = 48000;
constexpr int Channels = 2;
constexpr int FrameBytes = Channels * sizeof(float);
bool compensate(void* context, int32_t delta, uint32_t distance)
{
    return plank_audio_swr_set(context, delta, static_cast<int>(distance)) >= 0;
}
int32_t filter(void* context, uint8_t* output, uint32_t capacity,
               const uint8_t* input, uint32_t count)
{
    return plank_audio_swr_filter(context, output, static_cast<int>(capacity),
                                  input, static_cast<int>(count));
}
}

SdlAudioRenderer::SdlAudioRenderer(bool telemetry) : m_Telemetry(telemetry)
{
    SDL_assert(!SDL_WasInit(SDL_INIT_AUDIO));
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, "pipewire", SDL_HINT_OVERRIDE);
    m_Initialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!m_Initialized) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio initialization failed: %s", SDL_GetError());
    }
}

bool SdlAudioRenderer::prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* config)
{
    if (!m_Initialized || config->sampleRate != SampleRate ||
            config->channelCount != Channels || config->samplesPerFrame <= 0 ||
            config->samplesPerFrame > 4096) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Native audio requires 48 kHz stereo float PCM");
        return false;
    }
    m_Filter = plank_audio_swr_new();
    if (!m_Filter) return false;
    const PlankAudioCompensator callbacks{m_Filter, compensate, filter};
    m_Regulator = plank_audio_new(SampleRate, Channels, &callbacks);
    if (!m_Regulator) return false;
    m_AudioBuffer.resize(config->samplesPerFrame * Channels);
    const SDL_AudioSpec want{SDL_AUDIO_F32, Channels, SampleRate};
    // A callback must be smaller than the native 15 ms regulator target.
    // SDL's throughput-oriented 1024-frame default is 21 ms at 48 kHz.
    // Request 256 frames (~5 ms); verify the actual device below because SDL
    // backends may round or ignore the hint. This is not an extra PCM queue.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
    m_AudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                              &want, pullAudio, this);
    if (!m_AudioStream) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Audio device open failed: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec deviceSpec{};
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(m_AudioStream), &deviceSpec, &frames) ||
            deviceSpec.freq <= 0) return false;
    if (frames <= 0 || uint64_t(frames) * 1000 >= uint64_t(deviceSpec.freq) * 15) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Audio device callback (%d frames at %d Hz) exceeds native 15 ms buffering target",
                     frames, deviceSpec.freq);
        return false;
    }
    m_DeviceBufferDurationMs = frames * 1000 / deviceSpec.freq;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PLANK audio: native Kyber regulator, filtered FFmpeg compensation, target=15ms, driver=%s, device_buffer=%dms",
                SDL_GetCurrentAudioDriver(), m_DeviceBufferDurationMs);
    return SDL_ResumeAudioStreamDevice(m_AudioStream);
}

SdlAudioRenderer::~SdlAudioRenderer()
{
    // Session joins its producer before deleting us. Unregistering the callback
    // takes SDL's stream lock and waits for any in-flight callback to return.
    if (m_AudioStream) {
        SDL_PauseAudioStreamDevice(m_AudioStream);
        SDL_SetAudioStreamGetCallback(m_AudioStream, nullptr, nullptr);
        SDL_DestroyAudioStream(m_AudioStream);
    }
    plank_audio_free(m_Regulator);
    plank_audio_swr_free(m_Filter);
    if (m_Initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void* SdlAudioRenderer::getAudioBuffer(int* size)
{
    if (size) *size = static_cast<int>(m_AudioBuffer.size() * sizeof(float));
    return m_AudioBuffer.data();
}

void SDLCALL SdlAudioRenderer::pullAudio(void* opaque, SDL_AudioStream* stream,
                                         int additionalBytes, int)
{
    auto& self = *static_cast<SdlAudioRenderer*>(opaque);
    // SDL holds its recursive stream lock for the callback. Producer and
    // telemetry use this SAME lock; no extra worker, queue or outer mutex.
    std::array<float, 4096 * Channels> output{};
    while (additionalBytes > 0 && !self.m_Failed) {
        const int count = std::min(4096, additionalBytes / FrameBytes + (additionalBytes % FrameBytes != 0));
        if (!plank_audio_pull(self.m_Regulator, reinterpret_cast<uint8_t*>(output.data()), count) ||
                !SDL_PutAudioStreamData(stream, output.data(), count * FrameBytes)) {
            self.m_Failed = true;
            return;
        }
        additionalBytes -= count * FrameBytes;
    }
}

bool SdlAudioRenderer::submitAudio(int bytesWritten)
{
    if (!m_AudioStream || SDL_GetAudioStreamDevice(m_AudioStream) == 0 ||
            bytesWritten < 0 || bytesWritten % FrameBytes != 0 ||
            static_cast<size_t>(bytesWritten) > m_AudioBuffer.size() * sizeof(float)) return false;
    if (!SDL_LockAudioStream(m_AudioStream)) return false;
    bool okay = !m_Failed;
    if (okay && bytesWritten) {
        okay = plank_audio_push(m_Regulator, reinterpret_cast<const uint8_t*>(m_AudioBuffer.data()),
                                 bytesWritten / FrameBytes, 0);
        if (!okay) m_Failed = true;
    }
    const Uint64 now = SDL_GetTicks();
    PlankAudioStats stats{};
    bool report = false;
    int64_t filterDelay = 0;
    int queuedBytes = 0;
    if (okay && m_Telemetry && (!m_LastTelemetryTime || now - m_LastTelemetryTime >= 1000)) {
        report = plank_audio_stats(m_Regulator, &stats);
        filterDelay = plank_audio_swr_delay(m_Filter);
        queuedBytes = SDL_GetAudioStreamQueued(m_AudioStream);
        m_LastTelemetryTime = now;
    }
    SDL_UnlockAudioStream(m_AudioStream);
    // Logging can block on disk; never keep the device callback waiting for it.
    if (report) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "PLANK native audio regulator: ticks=%llu queued_frames=%u correction_ppm=%d input_frames=%llu pulled_frames=%llu underflow_frames=%llu skipped_frames=%llu filter_delay_frames=%lld sdl_queued_bytes=%d",
                static_cast<unsigned long long>(now), stats.queued_frames, stats.correction_ppm,
                static_cast<unsigned long long>(stats.input_frames),
                static_cast<unsigned long long>(stats.pulled_frames),
                static_cast<unsigned long long>(stats.underflow_frames),
                static_cast<unsigned long long>(stats.skipped_frames),
                static_cast<long long>(filterDelay), queuedBytes);
    }
    return okay;
}

PlankAudioStats SdlAudioRenderer::readStats()
{
    PlankAudioStats stats{};
    if (m_AudioStream && SDL_LockAudioStream(m_AudioStream)) {
        plank_audio_stats(m_Regulator, &stats);
        SDL_UnlockAudioStream(m_AudioStream);
    }
    return stats;
}
int SdlAudioRenderer::getCapabilities() { return CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION; }
int SdlAudioRenderer::getQueuedAudioDurationMs()
{
    if (!m_AudioStream || !SDL_LockAudioStream(m_AudioStream)) return -1;
    PlankAudioStats stats{};
    plank_audio_stats(m_Regulator, &stats);
    const int sdlBytes = SDL_GetAudioStreamQueued(m_AudioStream);
    const int duration = sdlBytes < 0 ? -1 :
        static_cast<int>((uint64_t(stats.queued_frames) * FrameBytes + sdlBytes) * 1000 / (SampleRate * FrameBytes));
    SDL_UnlockAudioStream(m_AudioStream);
    return duration;
}
int SdlAudioRenderer::getDeviceBufferDurationMs() { return m_DeviceBufferDurationMs; }
// Retained diagnostic field uses positive=compression; native stats above use
// positive=expansion. This is a sign translation, not another controller.
int SdlAudioRenderer::getAudioClockCorrectionPpm() { return -readStats().correction_ppm; }
quint64 SdlAudioRenderer::getSkippedAudioBlockCount() { return (readStats().skipped_frames + 239) / 240; }
IAudioRenderer::AudioFormat SdlAudioRenderer::getAudioBufferFormat() { return AudioFormat::Float32NE; }
