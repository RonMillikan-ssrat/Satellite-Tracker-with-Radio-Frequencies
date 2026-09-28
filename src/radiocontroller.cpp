#include "radiocontroller.h"
#include "sgp4wrapper.h"
#include <cmath>

namespace {
const double kSpeedOfLightKmS = 299792.458;
const int kTrackIntervalMs = 1000;       // SSB needs ~1 s updates at 435 MHz
const int kReconnectIntervalMs = 5000;
const int kReplyTimeoutMs = 5000;
}

RadioController::RadioController(SatelliteTracker* tracker, QObject *parent)
    : QObject(parent)
    , m_tracker(tracker)
    , m_socket(new QTcpSocket(this))
    , m_timer(new QTimer(this))
{
    m_status.host = "127.0.0.1";
    m_status.port = 7356;

    m_timer->setInterval(kTrackIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &RadioController::onTick);
    connect(m_socket, &QTcpSocket::connected, this, &RadioController::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RadioController::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RadioController::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead, this, &RadioController::onReadyRead);
}

void RadioController::setServer(const QString& host, quint16 port) {
    if (host == m_status.host && port == m_status.port) return;
    m_status.host = host;
    m_status.port = port;
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();  // reconnect to the new server on next connect/tick
    }
    emit statusChanged();
}

void RadioController::connectToRadio() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) return;
    m_lastConnectAttempt = QDateTime::currentDateTimeUtc();
    setMessage(QString("Connecting to %1:%2...").arg(m_status.host).arg(m_status.port));
    m_socket->connectToHost(m_status.host, m_status.port);
}

void RadioController::disconnectFromRadio() {
    stopTracking();
    m_socket->abort();
    m_status.connected = false;
    setMessage("Disconnected");
}

int RadioController::defaultTransponder(const Satellite& sat) const {
    for (int i = 0; i < sat.transponders.size(); ++i) {
        if (sat.transponders[i].downlinkFreq > 0 && inTunerRange(sat.transponders[i].downlinkFreq)) {
            return i;
        }
    }
    return -1;
}

QString RadioController::startTracking(const Satellite& sat, int transponderIndex) {
    if (transponderIndex < 0 || transponderIndex >= sat.transponders.size()) {
        return QString("%1 has no transponder #%2").arg(sat.name).arg(transponderIndex);
    }
    const Transponder& t = sat.transponders[transponderIndex];
    if (t.downlinkFreq <= 0) {
        return QString("%1 '%2' has no downlink frequency").arg(sat.name, t.name);
    }
    if (!inTunerRange(t.downlinkFreq)) {
        return QString("%1 MHz is outside the receiver's %2-%3 MHz range")
            .arg(t.downlinkFreq, 0, 'f', 3).arg(m_minMHz).arg(m_maxMHz);
    }

    m_satellite = sat;
    m_status.tracking = true;
    m_status.satelliteName = sat.name;
    m_status.catalogNumber = sat.catalogNumber;
    m_status.transponder = t;
    m_status.transponderIndex = transponderIndex;
    m_status.nominalHz = std::llround(t.downlinkFreq * 1e6);
    m_status.offsetHz = 0;
    m_status.tunedHz = 0;
    int passband = 0;
    if (!rigctlMode(t.mode, &m_status.rigMode, &passband)) m_status.rigMode.clear();
    m_needMode = true;

    setMessage(QString("Tracking %1 %2").arg(sat.name, t.name));
    m_timer->start();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        onTick();
    } else {
        connectToRadio();  // onConnected() runs the first tick
    }
    return QString();
}

void RadioController::stopTracking() {
    if (!m_status.tracking) return;
    m_timer->stop();
    m_status.tracking = false;
    setMessage("Tracking stopped");
}

bool RadioController::rigctlMode(const QString& transponderMode, QString* mode, int* passbandHz) {
    QString m = transponderMode.toUpper();
    if (m.contains("LSB")) {
        *mode = "LSB"; *passbandHz = 2800;
    } else if (m.contains("SSB") || m.contains("USB") || m.contains("LINEAR") || m.contains("BPSK")) {
        // Amateur satellite SSB and FUNcube-style BPSK telemetry use USB
        *mode = "USB"; *passbandHz = 2800;
    } else if (m == "CW") {
        *mode = "CW"; *passbandHz = 500;
    } else if (m.contains("APT")) {
        *mode = "FM"; *passbandHz = 40000;
    } else if (m.contains("FSK") || m.contains("GMSK")) {
        *mode = "FM"; *passbandHz = 20000;  // AFSK/GFSK/GMSK packet
    } else if (m.contains("FM") || m.contains("DUV") || m.contains("APRS") || m.contains("SSTV")) {
        *mode = "FM"; *passbandHz = 15000;
    } else {
        return false;
    }
    return true;
}

qint64 RadioController::dopplerShiftHz(double freqHz, double rangeRateKmS) {
    return std::llround(-freqHz * rangeRateKmS / kSpeedOfLightKmS);
}

void RadioController::onTick() {
    if (!m_status.tracking) return;

    QDateTime now = QDateTime::currentDateTimeUtc();

    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        if (!m_lastConnectAttempt.isValid() ||
            m_lastConnectAttempt.msecsTo(now) >= kReconnectIntervalMs) {
            connectToRadio();
        }
        return;
    }
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;

    // Previous round still waiting on the radio; drop a hung connection.
    if (!m_pending.isEmpty()) {
        if (m_lastSend.msecsTo(now) > kReplyTimeoutMs) {
            setMessage("Radio stopped responding; reconnecting");
            m_socket->abort();
        }
        return;
    }

    SatellitePosition pos = SGP4Wrapper::calculatePosition(m_satellite, now, m_tracker->getObserverLocation());
    m_status.elevation = pos.elevation;
    m_status.rangeRate = pos.rangerate;

    if (m_needMode) {
        m_needMode = false;
        QString mode;
        int passband = 0;
        if (rigctlMode(m_status.transponder.mode, &mode, &passband)) {
            send(QString("M %1 %2\n").arg(mode).arg(passband).toLatin1(), Pending::SetMode);
        }
    }

    if (m_status.tunedHz > 0) {
        send("f\n", Pending::GetFreq);  // applyFrequency() runs on the reply
    } else {
        applyFrequency(0);
    }
}

void RadioController::applyFrequency(qint64 radioHz) {
    // Anything the radio reports beyond our last setting was a manual retune.
    if (radioHz > 0 && m_status.tunedHz > 0 && std::llabs(radioHz - m_status.tunedHz) > 1) {
        m_status.offsetHz += radioHz - m_status.tunedHz;
    }

    double baseHz = double(m_status.nominalHz + m_status.offsetHz);
    m_status.dopplerHz = dopplerShiftHz(baseHz, m_status.rangeRate);
    qint64 targetHz = m_status.nominalHz + m_status.offsetHz + m_status.dopplerHz;

    if (targetHz != m_status.tunedHz) {
        send(QString("F %1\n").arg(targetHz).toLatin1(), Pending::SetFreq);
        m_status.tunedHz = targetHz;
    }

    if (m_status.elevation < 0) {
        m_status.message = QString("%1 below horizon, pre-tuned for AOS").arg(m_status.satelliteName);
    } else if (m_status.message.startsWith(m_status.satelliteName + " below horizon")) {
        m_status.message = QString("Tracking %1 %2").arg(m_status.satelliteName, m_status.transponder.name);
    }
    emit statusChanged();
}

void RadioController::send(const QByteArray& command, Pending kind) {
    m_pending.enqueue(kind);
    m_lastSend = QDateTime::currentDateTimeUtc();
    m_socket->write(command);
}

void RadioController::onConnected() {
    m_status.connected = true;
    m_pending.clear();
    m_readBuffer.clear();
    // The radio may have been retuned or restarted while we were away.
    m_status.tunedHz = 0;
    m_needMode = m_status.tracking;
    setMessage(QString("Connected to %1:%2").arg(m_status.host).arg(m_status.port));
    onTick();
}

void RadioController::onDisconnected() {
    m_status.connected = false;
    m_pending.clear();
    emit statusChanged();
}

void RadioController::onSocketError(QAbstractSocket::SocketError error) {
    m_status.connected = false;
    m_pending.clear();
    if (error == QAbstractSocket::ConnectionRefusedError) {
        setMessage(QString("Nothing listening on %1:%2 - enable rigctl in Gqrx/SDR++")
                       .arg(m_status.host).arg(m_status.port));
    } else {
        setMessage("Radio connection error: " + m_socket->errorString());
    }
}

void RadioController::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());

    int newline;
    while ((newline = m_readBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_readBuffer.left(newline).trimmed();
        m_readBuffer.remove(0, newline + 1);
        if (line.isEmpty() || m_pending.isEmpty()) continue;

        Pending kind = m_pending.dequeue();
        if (line.startsWith("RPRT")) {
            if (line != "RPRT 0") {
                setMessage(QString("Radio rejected command (%1)").arg(QString::fromLatin1(line)));
            }
            // A radio that can't report its frequency still gets tuned.
            if (kind == Pending::GetFreq && m_status.tracking) applyFrequency(0);
            continue;
        }
        if (kind == Pending::GetFreq && m_status.tracking) {
            bool ok = false;
            qint64 hz = qint64(line.toDouble(&ok));
            applyFrequency(ok ? hz : 0);
        }
    }
}

void RadioController::setMessage(const QString& message) {
    m_status.message = message;
    emit statusChanged();
}
