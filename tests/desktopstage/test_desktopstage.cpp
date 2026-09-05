#include <QtTest>
#include "desktopstage.h"
#include "hostrecovery.h"

class TestDesktopStage : public QObject
{
    Q_OBJECT
private slots:
    void stopsTerminalReconnectFailures()
    {
        for (int status : {400, 403, 404, 426}) {
            QVERIFY(PlankHostRecovery::terminalReconnectResponse(status));
        }
        for (int status : {401, 409, 423, 425, 429, 500, 502, 503, 504}) {
            QVERIFY(!PlankHostRecovery::terminalReconnectResponse(status));
        }
    }

    void boundsSilenceTrigger()
    {
        using namespace PlankHostRecovery;
        QVERIFY(!videoSilent(10000, 0)); // No first frame yet.
        QVERIFY(!videoSilent(999, 1000));
        QVERIFY(!videoSilent(1999, 1000));
        QVERIFY(videoSilent(2000, 1000));
        QVERIFY(!videoSilent(2001, 2000)); // Incoming video cancels recovery.
    }

    void requiresPinnedCertificateAndReplacement()
    {
        using namespace PlankHostRecovery;
        const QString first = "11111111-1111-4111-8111-111111111111";
        const QString second = "22222222-2222-4222-8222-222222222222";
        const QByteArray certificate(32, 'a');
        QVERIFY(replacementConfirmed(first, second, certificate, certificate));
        QVERIFY(!replacementConfirmed(first, first, certificate, certificate));
        QVERIFY(!replacementConfirmed(first, second, certificate, QByteArray(32, 'b')));
        QVERIFY(!replacementConfirmed(first, second, {}, {}));
        for (const QString& invalid : {QString(), QStringLiteral("not-an-instance"),
                                       QStringLiteral("00000000-0000-0000-0000-000000000000"),
                                       "{" + second + "}"}) {
            QVERIFY(!replacementConfirmed(first, invalid, certificate, certificate));
            QVERIFY(!replacementConfirmed(invalid, second, certificate, certificate));
        }
    }

    void recognizesOnlyAuthenticatedDesktopStages()
    {
        QJsonObject response {{"state", "authenticated"},
                              {"session_token", "synthetic-test-token"},
                              {"desktop_stage", "greeter"}};
        QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::Greeter);
        response["desktop_stage"] = "user";
        QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::User);
        for (const QString& stage : {QStringLiteral("User"), QStringLiteral("unknown"),
                                     QStringLiteral("closing"), QStringLiteral("Greeter"), QString()}) {
            response["desktop_stage"] = stage;
            QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::Unknown);
        }
        for (const QString& stage : {QStringLiteral("greeter"), QStringLiteral("user")}) {
            response["desktop_stage"] = stage;
            for (const QString& state : {QStringLiteral("challenge"), QStringLiteral("denied"), QString()}) {
                response["state"] = state;
                QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::Unknown);
            }
            response["state"] = "authenticated";
            for (const QJsonValue& token : {QJsonValue(), QJsonValue(""), QJsonValue(123), QJsonValue(true)}) {
                response["session_token"] = token;
                QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::Unknown);
            }
            response.remove("session_token");
            QCOMPARE(plankAuthenticatedDesktopStage(response), PlankDesktopStage::Unknown);
            response["session_token"] = "synthetic-test-token";
        }
        QCOMPARE(plankAuthenticatedDesktopStage({}), PlankDesktopStage::Unknown);
    }

    void recoversLoginStatusWithoutOldWorkerNotice()
    {
        // Replay the observed loss of the greeter stream: no handoff notice
        // arrives, then the replacement worker authenticates the user desktop.
        QVERIFY(plankReconnectDesktopStatus(PlankDesktopStage::Unknown, true, false, 1000, 10000) == nullptr);
        const QJsonObject response {{"state", "authenticated"},
                                   {"session_token", "synthetic-test-token"},
                                   {"desktop_stage", "user"}};
        const auto stage = plankAuthenticatedDesktopStage(response);
        QCOMPARE(QString::fromUtf8(plankReconnectDesktopStatus(stage, true, false, 2000, 10000)),
                 QStringLiteral("Opening your desktop..."));
        QCOMPARE(QString::fromUtf8(plankReconnectDesktopStatus(PlankDesktopStage::Greeter, true, false, 2000, 10000)),
                 QStringLiteral("Returning to the sign-in screen..."));
    }

    void preservesTimeoutAndCancellation()
    {
        for (const auto stage : {PlankDesktopStage::Greeter, PlankDesktopStage::User}) {
            QVERIFY(plankReconnectDesktopStatus(stage, false, false, 1000, 10000) == nullptr);
            QVERIFY(plankReconnectDesktopStatus(stage, true, true, 1000, 10000) == nullptr);
            QVERIFY(plankReconnectDesktopStatus(stage, true, false, 1000, 0) == nullptr);
            QVERIFY(plankReconnectDesktopStatus(stage, true, false, 10000, 10000) == nullptr);
            QVERIFY(plankReconnectDesktopStatus(stage, true, false, 10001, 10000) == nullptr);
        }
    }
};

QTEST_GUILESS_MAIN(TestDesktopStage)
#include "test_desktopstage.moc"
