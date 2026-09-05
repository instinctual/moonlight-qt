#pragma once

#include <QByteArray>
#include <QString>
#include <QUuid>

namespace PlankHostRecovery {
constexpr quint64 SilenceMs = 1000;
constexpr quint64 ProbeIntervalMs = 1000;

inline bool terminalReconnectResponse(int status)
{
    // Invalid topology, denied ownership, missing resources and protocol
    // mismatch cannot be repaired by repeatedly opening new PAM sessions.
    // Worker startup/cleanup and replacement-token failures remain retryable.
    return status == 400 || status == 403 || status == 404 || status == 426;
}

inline QString canonicalInstance(const QString& value)
{
    const QUuid id(value);
    if (id.isNull() || value.size() != 36) return {};
    const QString canonical = id.toString(QUuid::WithoutBraces);
    return value.compare(canonical, Qt::CaseInsensitive) == 0 ? canonical : QString();
}

inline bool videoSilent(quint64 now, quint64 lastVideo)
{
    return lastVideo != 0 && now >= lastVideo && now - lastVideo >= SilenceMs;
}

inline bool replacementConfirmed(const QString& before, const QString& after,
                                 const QByteArray& expectedCertificate,
                                 const QByteArray& actualCertificate)
{
    const QString oldId = canonicalInstance(before);
    const QString newId = canonicalInstance(after);
    return !oldId.isEmpty() && !newId.isEmpty() && oldId != newId &&
            expectedCertificate.size() == 32 && expectedCertificate == actualCertificate;
}
}
