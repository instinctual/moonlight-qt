#include <QtTest>

#include "streaming/avsynccontroller.h"
#include "streaming/streamutils.h"
#include "streaming/video/ffmpeg-renderers/pacer/pacer.h"

// Keep the actual queue/render/clock path, replacing only display discovery.
int StreamUtils::getDisplayRefreshRate(SDL_Window*) { return 60; }

namespace {
class FrameRenderer final : public IFFmpegRenderer
{
public:
    explicit FrameRenderer(bool move) : moveFrame(move), retained(av_frame_alloc()) {}
    ~FrameRenderer() { av_frame_free(&retained); }
    bool initialize(PDECODER_PARAMETERS) override { return true; }
    bool prepareDecoderContext(AVCodecContext*, AVDictionary**) override { return true; }
    bool isRenderThreadSupported() override { return false; }
    void renderFrame(AVFrame* frame) override {
        if (moveFrame) {
            av_frame_unref(retained);
            av_frame_move_ref(retained, frame);
            inputWasCleared = frame->pts == AV_NOPTS_VALUE;
        }
        completedTicks = SDL_GetTicks();
    }
    bool moveFrame;
    AVFrame* retained;
    bool inputWasCleared = false;
    Uint64 completedTicks = 0;
};

QStringList messages;
void SDLCALL recordLog(void*, int, SDL_LogPriority, const char* message) {
    messages.append(QString::fromUtf8(message));
}
}

class PacerClockTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(SDL_Init(SDL_INIT_EVENTS));
        SDL_SetLogOutputFunction(recordLog, nullptr);
    }
    void cleanupTestCase() {
        SDL_SetLogOutputFunction(nullptr, nullptr);
        SDL_Quit();
    }
    void publishesOriginalTimestamp_data() {
        QTest::addColumn<bool>("moveFrame");
        QTest::addColumn<qint64>("pts");
        QTest::newRow("retained-reference") << false << qint64(1234);
        QTest::newRow("moved-reference") << true << qint64(1234);
        QTest::newRow("zero-is-valid") << true << qint64(0);
        QTest::newRow("untimed-is-not-a-clock") << true << qint64(AV_NOPTS_VALUE);
    }
    void publishesOriginalTimestamp() {
        QFETCH(bool, moveFrame);
        QFETCH(qint64, pts);
        qputenv("PLANK_AV_SYNC_TELEMETRY", "1");
        messages.clear();
        PlankAvSync::resetVideoClock();
        FrameRenderer renderer(moveFrame);
        VIDEO_STATS stats {};
        Pacer pacer(&renderer, &stats);
        QVERIFY(pacer.initialize(nullptr, 60, false));
        AVFrame* frame = av_frame_alloc();
        QVERIFY(frame != nullptr);
        frame->pts = pts;
        frame->pkt_dts = SDL_GetTicks();
        pacer.submitFrame(frame);
        pacer.renderOnMainThread();
        QCOMPARE(stats.renderedFrames, uint32_t(1));
        if (moveFrame) {
            QVERIFY(renderer.inputWasCleared);
            QCOMPARE(renderer.retained->pts, pts);
        }
        const auto clock = PlankAvSync::readVideoClock();
        QCOMPARE(clock.valid, pts >= 0);
        const auto logs = messages.filter("PLANK A/V video clock:");
        if (pts >= 0) {
            QCOMPARE(clock.mediaTimeMs, pts);
            QVERIFY(clock.presentationTicks >= renderer.completedTicks);
            QCOMPARE(logs.size(), qsizetype(1));
            QVERIFY(logs.first().contains(QString("media=%1 ").arg(pts)));
        } else {
            QVERIFY(logs.isEmpty());
        }
        qunsetenv("PLANK_AV_SYNC_TELEMETRY");
    }
};

QTEST_GUILESS_MAIN(PacerClockTest)
#include "test_pacerclock.moc"
