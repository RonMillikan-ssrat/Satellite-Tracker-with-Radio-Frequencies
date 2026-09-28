#include "passestab.h"
#include "numerictableitem.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSettings>
#include <QtConcurrent>

namespace {
const int kRefreshIntervalMs = 15 * 60 * 1000;  // roll the window forward
const QColor kInProgressColor(90, 190, 110, 90);

QString localTime(const QDateTime& utc, bool withDay) {
    return utc.toLocalTime().toString(withDay ? "ddd h:mm AP" : "h:mm AP");
}
}

PassesTab::PassesTab(SatelliteTracker* tracker, RadioController* radio, QWidget *parent)
    : QWidget(parent)
    , m_tracker(tracker)
    , m_radio(radio)
    , m_countdownTimer(new QTimer(this))
    , m_refreshTimer(new QTimer(this))
    , m_watcher(new QFutureWatcher<QList<SatellitePass>>(this))
{
    QSettings settings;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout* filters = new QHBoxLayout();
    m_hoursSpin = new QSpinBox();
    m_hoursSpin->setRange(1, 168);
    m_hoursSpin->setSuffix(" h");
    m_hoursSpin->setValue(settings.value("passes/hours", 24).toInt());
    m_minElevationSpin = new QSpinBox();
    m_minElevationSpin->setRange(0, 85);
    m_minElevationSpin->setSuffix("°");
    m_minElevationSpin->setValue(settings.value("passes/minElevation", 10).toInt());
    m_minElevationSpin->setToolTip("Hide passes that never get this high; low passes are hard to receive");
    m_receivableCheck = new QCheckBox("Only satellites I can receive");
    m_receivableCheck->setChecked(settings.value("passes/receivableOnly", true).toBool());
    m_receivableCheck->setToolTip("Hide satellites with no known downlink inside the receiver's tuning range");
    m_summaryLabel = new QLabel("Waiting for satellite data...");

    filters->addWidget(new QLabel("Next:"));
    filters->addWidget(m_hoursSpin);
    filters->addSpacing(12);
    filters->addWidget(new QLabel("Min elevation:"));
    filters->addWidget(m_minElevationSpin);
    filters->addSpacing(12);
    filters->addWidget(m_receivableCheck);
    filters->addSpacing(20);
    filters->addWidget(m_summaryLabel, 1);
    layout->addLayout(filters);

    m_table = new QTableWidget();
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({"Satellite", "Rises", "Max El", "Peak", "Sets",
                                        "Duration", "Direction", "Downlink", "Starts in"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ColRises, Qt::AscendingOrder);
    m_table->setToolTip("Double-click a pass to open it in the Satellite tab");
    layout->addWidget(m_table);

    connect(m_hoursSpin, &QSpinBox::valueChanged, this, [this](int hours) {
        QSettings().setValue("passes/hours", hours);
        recalculate();
    });
    connect(m_minElevationSpin, &QSpinBox::valueChanged, this, [this](int elevation) {
        QSettings().setValue("passes/minElevation", elevation);
        rebuildTable();
    });
    connect(m_receivableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("passes/receivableOnly", checked);
        rebuildTable();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &PassesTab::onCellDoubleClicked);
    connect(m_watcher, &QFutureWatcher<QList<SatellitePass>>::finished,
            this, &PassesTab::onPredictionFinished);

    connect(m_tracker, &SatelliteTracker::tleDataUpdated, this, &PassesTab::recalculate);
    connect(m_tracker, &SatelliteTracker::locationUpdated, this, &PassesTab::recalculate);
    // SatNOGS frequencies can arrive after the TLEs; refresh the downlink column
    connect(m_tracker, &SatelliteTracker::positionsUpdated, this, [this]() {
        int count = m_tracker->transmitterSatelliteCount();
        if (count != m_lastTransmitterCount) {
            m_lastTransmitterCount = count;
            rebuildTable();
        }
    });

    m_countdownTimer->start(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &PassesTab::onCountdownTick);
    m_refreshTimer->start(kRefreshIntervalMs);
    connect(m_refreshTimer, &QTimer::timeout, this, &PassesTab::recalculate);
}

void PassesTab::recalculate() {
    QList<Satellite> satellites = m_tracker->getAllSatellites();
    if (satellites.isEmpty()) return;
    if (m_watcher->isRunning()) {
        m_recalculatePending = true;
        return;
    }

    m_summaryLabel->setText("Calculating passes...");
    // Predict at 0° so changing the elevation filter doesn't need a recalculation
    ObserverLocation observer = m_tracker->getObserverLocation();
    QDateTime start = QDateTime::currentDateTimeUtc();
    double hours = m_hoursSpin->value();
    m_watcher->setFuture(QtConcurrent::run([satellites, observer, start, hours]() {
        return PassPredictor::predictAll(satellites, observer, start, hours, 0.0);
    }));
}

void PassesTab::onPredictionFinished() {
    m_passes = m_watcher->result();
    rebuildTable();
    if (m_recalculatePending) {
        m_recalculatePending = false;
        recalculate();
    }
}

void PassesTab::rebuildTable() {
    QDateTime now = QDateTime::currentDateTimeUtc();

    QHash<int, Satellite> satellites;
    for (const Satellite& sat : m_tracker->getAllSatellites()) {
        satellites.insert(sat.catalogNumber, sat);
    }

    m_shown.clear();
    for (const SatellitePass& pass : m_passes) {
        if (pass.los <= now) continue;
        if (pass.maxElevation < m_minElevationSpin->value()) continue;
        if (m_receivableCheck->isChecked() &&
            m_radio->defaultTransponder(satellites.value(pass.catalogNumber)) < 0) continue;
        m_shown.append(pass);
    }

    bool sortingWasEnabled = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    m_table->setRowCount(m_shown.size());

    for (int row = 0; row < m_shown.size(); ++row) {
        const SatellitePass& pass = m_shown[row];
        const Satellite sat = satellites.value(pass.catalogNumber);

        QTableWidgetItem* nameItem = new QTableWidgetItem(pass.satelliteName);
        nameItem->setData(Qt::UserRole, row);
        m_table->setItem(row, ColSatellite, nameItem);

        QString rises = pass.aosBeforeSearch ? "(already up)" : localTime(pass.aos, true);
        m_table->setItem(row, ColRises, new NumericTableItem(pass.aos.toMSecsSinceEpoch(), rises));
        m_table->setItem(row, ColMaxEl, new NumericTableItem(pass.maxElevation,
                                            QString::number(pass.maxElevation, 'f', 0) + "°"));
        m_table->setItem(row, ColPeak, new NumericTableItem(pass.tca.toMSecsSinceEpoch(),
                                           localTime(pass.tca, false)));
        QString sets = pass.losAfterSearch ? "(still up)" : localTime(pass.los, false);
        m_table->setItem(row, ColSets, new NumericTableItem(pass.los.toMSecsSinceEpoch(), sets));
        // Already up for hours (e.g. high-orbit AO-10): only the remaining time is known
        QString duration = PassPredictor::formatDuration(pass.durationSeconds());
        if (pass.aosBeforeSearch || pass.losAfterSearch) duration += "+";
        m_table->setItem(row, ColDuration, new NumericTableItem(pass.durationSeconds(), duration));
        m_table->setItem(row, ColDirection, new QTableWidgetItem(PassPredictor::directionText(pass)));

        int index = m_radio->defaultTransponder(sat);
        QString downlink = "-";
        if (index >= 0) {
            const Transponder& t = sat.transponders[index];
            downlink = QString("%1 MHz %2").arg(t.downlinkFreq, 0, 'f', 3).arg(t.mode);
        }
        m_table->setItem(row, ColDownlink, new QTableWidgetItem(downlink));
        m_table->setItem(row, ColStartsIn, new NumericTableItem(pass.aos.toMSecsSinceEpoch(), QString()));
    }

    if (sortingWasEnabled) m_table->setSortingEnabled(true);

    if (!m_columnsSized && !m_shown.isEmpty()) {
        m_table->resizeColumnsToContents();
        m_columnsSized = true;
    }
    onCountdownTick();
}

void PassesTab::onCountdownTick() {
    QDateTime now = QDateTime::currentDateTimeUtc();

    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem* nameItem = m_table->item(row, ColSatellite);
        if (!nameItem) continue;
        const SatellitePass& pass = m_shown[nameItem->data(Qt::UserRole).toInt()];

        if (pass.los <= now) {
            rebuildTable();  // drops finished passes, then comes back here
            return;
        }

        bool inProgress = pass.isInProgress(now);
        QString text = inProgress
            ? QString("Now - %1 left").arg(PassPredictor::formatDuration(now.secsTo(pass.los)))
            : PassPredictor::formatDuration(now.secsTo(pass.aos));
        m_table->item(row, ColStartsIn)->setText(text);

        QBrush background = inProgress ? QBrush(kInProgressColor) : QBrush();
        for (int col = 0; col < ColumnCount; ++col) {
            if (m_table->item(row, col)->background() != background) {
                m_table->item(row, col)->setBackground(background);
            }
        }
    }
    updateSummary(now);
}

void PassesTab::updateSummary(const QDateTime& nowUtc) {
    if (m_passes.isEmpty() && !m_watcher->isRunning()) return;
    if (m_watcher->isRunning()) return;  // keep "Calculating passes..." visible

    QString text = QString("%1 passes in the next %2 h").arg(m_shown.size()).arg(m_hoursSpin->value());
    QStringList now;
    const SatellitePass* next = nullptr;
    for (const SatellitePass& pass : m_shown) {
        if (pass.isInProgress(nowUtc)) now.append(pass.satelliteName);
        else if (!next || pass.aos < next->aos) next = &pass;
    }
    if (!now.isEmpty()) text += "   |   Up now: " + now.join(", ");
    if (next) {
        text += QString("   |   Next: %1 in %2 (%3°)")
                    .arg(next->satelliteName, PassPredictor::formatDuration(nowUtc.secsTo(next->aos)))
                    .arg(next->maxElevation, 0, 'f', 0);
    }
    m_summaryLabel->setText(text);
}

void PassesTab::onCellDoubleClicked(int row, int /*column*/) {
    QTableWidgetItem* nameItem = m_table->item(row, ColSatellite);
    if (!nameItem) return;
    emit passActivated(m_shown[nameItem->data(Qt::UserRole).toInt()]);
}
