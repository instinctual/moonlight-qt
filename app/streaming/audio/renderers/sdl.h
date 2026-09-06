#pragma once
#include "renderer.h"
#include <SDL3/SDL.h>
#include <plank_audio.h>
#include <vector>

class SdlAudioRenderer : public IAudioRenderer
{
public:
    explicit SdlAudioRenderer(bool telemetry = false);
    ~SdlAudioRenderer() override;
    bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION*) override;
    void* getAudioBuffer(int* size) override;
    bool submitAudio(int bytesWritten) override;
    int getCapabilities() override;
    int getQueuedAudioDurationMs() override;
    int getDeviceBufferDurationMs() override;
    int getAudioClockCorrectionPpm() override;
    quint64 getSkippedAudioBlockCount() override;
    AudioFormat getAudioBufferFormat() override;
private:
    static void SDLCALL pullAudio(void*, SDL_AudioStream*, int additionalBytes, int totalBytes);
    PlankAudioStats readStats();
    SDL_AudioStream* m_AudioStream = nullptr;
    PlankAudioRegulator* m_Regulator = nullptr;
    void* m_Filter = nullptr;
    std::vector<float> m_AudioBuffer;
    int m_DeviceBufferDurationMs = 0;
    bool m_Initialized = false;
    bool m_Failed = false; // SDL stream lock once callbacks start.
    bool m_Telemetry = false;
    Uint64 m_LastTelemetryTime = 0;
    Uint64 m_LastPushNs = 0;
    Uint64 m_LastPullNs = 0;
    Uint64 m_MaxPushGapNs = 0;
    Uint64 m_MaxPullGapNs = 0;
    int m_MaxPullFrames = 0;
};
