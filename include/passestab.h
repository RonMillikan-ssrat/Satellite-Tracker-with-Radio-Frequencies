#ifndef PASSESTAB_H
#define PASSESTAB_H

#include <QWidget>
#include <QTableWidget>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QFutureWatcher>
#include "satellitetracker.h"
#include "radiocontroller.h"
#include "passpredictor.h"

// "Upcoming Passes" tab: every satellite's passes over the next N hours,
// sorted by rise time, with a live countdown. Prediction runs on a worker
// thread and is redone when TLEs, the observer location or the window change.
class PassesTab : public QWidget {
    Q_OBJECT

public:
    PassesTab(SatelliteTracker* tracker, RadioController* radio, QWidget *parent = nullptr);

    void recalculate();

signals:
    void passActivated(const SatellitePass& pass);

private slots:
    void onPredictionFinished();
    void onCountdownTick();
    void onCellDoubleClicked(int row, int column);

private:
    enum Column { ColSatellite, ColRises, ColMaxEl, ColPeak, ColSets, ColDuration,
                  ColDirection, ColDownlink, ColStartsIn, ColumnCount };

    SatelliteTracker* m_tracker;
    RadioController* m_radio;

    QSpinBox* m_hoursSpin;
    QSpinBox* m_minElevationSpin;
    QCheckBox* m_receivableCheck;
    QLabel* m_summaryLabel;
    QTableWidget* m_table;
    QTimer* m_countdownTimer;
    QTimer* m_refreshTimer;

    QFutureWatcher<QList<SatellitePass>>* m_watcher;
    bool m_recalculatePending = false;
    bool m_columnsSized = false;
    int m_lastTransmitterCount = -1;
    QList<SatellitePass> m_passes;   // everything predicted (min elevation 0)
    QList<SatellitePass> m_shown;    // after filters; row data indexes into this

    void rebuildTable();
    void updateSummary(const QDateTime& nowUtc);
};

#endif // PASSESTAB_H
