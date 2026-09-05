#pragma once

#include <QJsonObject>
#include <QString>

enum class PlankDesktopStage { Unknown, Greeter, User };

// UI-only evidence from a successful PAM response, not launch authority.
inline PlankDesktopStage plankAuthenticatedDesktopStage(const QJsonObject& response)
{
    if (response.value(QStringLiteral("state")).toString() != QStringLiteral("authenticated") ||
            response.value(QStringLiteral("session_token")).toString().isEmpty()) {
        return PlankDesktopStage::Unknown;
    }
    const QString stage = response.value(QStringLiteral("desktop_stage")).toString();
    if (stage == QStringLiteral("greeter")) return PlankDesktopStage::Greeter;
    if (stage == QStringLiteral("user")) return PlankDesktopStage::User;
    return PlankDesktopStage::Unknown;
}

inline const char* plankReconnectDesktopStatus(PlankDesktopStage stage,
                                              bool reconnecting, bool cancelled,
                                              quint64 now, quint64 decisionDeadline)
{
    // Never replace a timeout prompt or resurrect progress after cancellation.
    if (!reconnecting || cancelled || decisionDeadline == 0 || now >= decisionDeadline) {
        return nullptr;
    }
    switch (stage) {
    case PlankDesktopStage::Greeter: return "Returning to the sign-in screen...";
    case PlankDesktopStage::User: return "Opening your desktop...";
    default: return nullptr;
    }
}
