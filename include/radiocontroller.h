#ifndef RADIOCONTROLLER_H
#define RADIOCONTROLLER_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include <QDateTime>
#include "satellitetracker.h"

// Keeps an SDR application tuned to a satellite downlink with Doppler
// correction, using the Hamlib rigctl TCP protocol. Works with the rigctl
// servers built into Gqrx ("Remote control", port 7356) and SDR++ ("Rigctl
// Server" module, port 4532), or with Hamlib's rigctld for a hardware radio.
//
// Each tick reads the radio's frequency back before setting a new one. If the
// user retuned by hand (e.g. onto a station inside a linear transponder's
// passband) the difference is kept as a manual offset on top of the
// Doppler-corrected frequency instead of being overwritten.
class RadioController : public QObject {
    Q_OBJECT

public:
    struct Status {
        bool connected = false;
        bool tracking = false;
        QString host;
        quint16 port = 0;
        QString satelliteName;
        int catalogNumber = 0;
        Transponder transponder;
        int transponderIndex = -1;
        QString rigMode;          // rigctl mode sent to the radio (empty = left unchanged)
        double elevation = 0.0;   // degrees
        double rangeRate = 0.0;   // km/s
        qint64 nominalHz = 0;     // published downlink frequency
        qint64 dopplerHz = 0;     // Doppler shift applied
        qint64 offsetHz = 0;      // manual offset picked up from the radio
        qint64 tunedHz = 0;       // last frequency sent to the radio
        QString message;          // last error or informational message
    };

    explicit RadioController(SatelliteTracker* tracker, QObject *parent = nullptr);

    void setServer(const QString& host, quint16 port);
    void connectToRadio();
    // Disconnects and stops tracking (otherwise tracking would reconnect).
    void disconnectFromRadio();

    // Start Doppler tracking of one transponder's downlink. Returns an empty
    // string on success, otherwise the reason it cannot be tuned.
    QString startTracking(const Satellite& sat, int transponderIndex);
    void stopTracking();

    Status status() const { return m_status; }
    bool isTracking() const { return m_status.tracking; }

    // Receiver tuning range in MHz. Defaults to the NooElec NESDR Smart v5
    // (RTL2832U + R860: 100 kHz - 1.75 GHz).
    void setTunerRange(double minMHz, double maxMHz) { m_minMHz = minMHz; m_maxMHz = maxMHz; }
    double tunerMinMHz() const { return m_minMHz; }
    double tunerMaxMHz() const { return m_maxMHz; }
    bool inTunerRange(double mhz) const { return mhz >= m_minMHz && mhz <= m_maxMHz; }

    // First transponder whose downlink the receiver can tune, or -1.
    int defaultTransponder(const Satellite& sat) const;

    // Map a transponder mode ("FM", "CW/SSB", "APT", ...) to a rigctl mode and
    // passband. Returns false for modes an SDR app cannot demodulate itself
    // (e.g. LRPT, HRPT), in which case only the frequency is set.
    static bool rigctlMode(const QString& transponderMode, QString* mode, int* passbandHz);

    // Doppler shift in Hz for a signal transmitted at freqHz by a source with
    // the given range rate (km/s, negative = approaching).
    static qint64 dopplerShiftHz(double freqHz, double rangeRateKmS);

signals:
    void statusChanged();

private slots:
    void onTick();
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onReadyRead();

private:
    enum class Pending { GetFreq, SetFreq, SetMode };

    SatelliteTracker* m_tracker;
    QTcpSocket* m_socket;
    QTimer* m_timer;
    Status m_status;
    Satellite m_satellite;
    double m_minMHz = 0.1;
    double m_maxMHz = 1750.0;

    QQueue<Pending> m_pending;
    QByteArray m_readBuffer;
    QDateTime m_lastSend;
    QDateTime m_lastConnectAttempt;
    bool m_needMode = false;

    void send(const QByteArray& command, Pending kind);
    void applyFrequency(qint64 radioHz);
    void setMessage(const QString& message);
};

#endif // RADIOCONTROLLER_H
