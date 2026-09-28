#include "satellitetab.h"
#include <libsgp4/Tle.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSplitter>
#include <QSettings>
#include <QRegularExpression>
#include <QTimeZone>
#include <algorithm>
#include <cmath>
#include <exception>

namespace {
const double kPassHours = 72.0;
const double kEarthRadiusKm = 6378.137;
const double kEarthMu = 398600.4418;  // km^3/s^2
const QColor kInProgressColor(90, 190, 110, 90);

QString localTime(const QDateTime& utc, bool withDay) {
    return utc.toLocalTime().toString(withDay ? "ddd MMM d h:mm AP" : "h:mm AP");
}
}

SatelliteTab::SatelliteTab(SatelliteTracker* tracker, RadioController* radio, QWidget *parent)
    : QWidget(parent)
    , m_tracker(tracker)
    , m_radio(radio)
    , m_completionModel(new QStringListModel(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Search row
    QHBoxLayout* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Name or NORAD catalog number, e.g. ISS or 25544");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMaximumWidth(420);
    QCompleter* completer = new QCompleter(m_completionModel, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(15);
    m_searchEdit->setCompleter(completer);
    QPushButton* findButton = new QPushButton("Show");

    m_minElevationSpin = new QSpinBox();
    m_minElevationSpin->setRange(0, 85);
    m_minElevationSpin->setSuffix("°");
    m_minElevationSpin->setValue(QSettings().value("satellite/minElevation", 10).toInt());

    searchRow->addWidget(new QLabel("Find satellite:"));
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(findButton);
    searchRow->addSpacing(20);
    searchRow->addWidget(new QLabel(QString("Passes in the next %1 h above").arg(kPassHours)));
    searchRow->addWidget(m_minElevationSpin);
    searchRow->addStretch();
    layout->addLayout(searchRow);

    // Details + passes on the left, sky map on the right
    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    QWidget* left = new QWidget();
    QVBoxLayout* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel("Search for a satellite, or double-click a pass in Upcoming Passes");
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    m_detailsLabel = new QLabel();
    m_detailsLabel->setTextFormat(Qt::RichText);
    m_detailsLabel->setWordWrap(true);
    m_detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_passTable = new QTableWidget();
    m_passTable->setColumnCount(6);
    m_passTable->setHorizontalHeaderLabels({"Rises", "Max El", "Peak", "Sets", "Duration", "Direction"});
    m_passTable->horizontalHeader()->setStretchLastSection(true);
    m_passTable->verticalHeader()->setVisible(false);
    m_passTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_passTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_passTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passTable->setAlternatingRowColors(true);

    m_downlinkList = new QListWidget();
    m_downlinkList->setAlternatingRowColors(true);
    m_downlinkList->setSelectionMode(QAbstractItemView::NoSelection);

    QHBoxLayout* armRow = new QHBoxLayout();
    m_armButton = new QPushButton("Tune at AOS");
    m_armButton->setEnabled(false);
    m_armButton->setToolTip(QString("Start radio tracking %1 s before the selected pass rises, "
                                    "using the transponder chosen in the Radio panel")
                                .arg(RadioController::kPreTuneSeconds));
    m_armHint = new QLabel();
    armRow->addWidget(m_armButton);
    armRow->addWidget(m_armHint, 1);

    leftLayout->addWidget(m_titleLabel);
    leftLayout->addWidget(m_detailsLabel);
    // Passes above downlinks; the ISS alone lists ~45 downlinks
    QWidget* passesBox = new QWidget();
    QVBoxLayout* passesLayout = new QVBoxLayout(passesBox);
    passesLayout->setContentsMargins(0, 0, 0, 0);
    passesLayout->addWidget(new QLabel("Upcoming passes (local time):"));
    passesLayout->addWidget(m_passTable, 1);
    passesLayout->addLayout(armRow);
    QWidget* downlinkBox = new QWidget();
    QVBoxLayout* downlinkLayout = new QVBoxLayout(downlinkBox);
    downlinkLayout->setContentsMargins(0, 0, 0, 0);
    downlinkLayout->addWidget(new QLabel("Downlinks:"));
    downlinkLayout->addWidget(m_downlinkList, 1);
    QSplitter* leftSplitter = new QSplitter(Qt::Vertical);
    leftSplitter->addWidget(passesBox);
    leftSplitter->addWidget(downlinkBox);
    leftSplitter->setSizes({400, 250});
    leftLayout->addWidget(leftSplitter, 1);

    m_skyMap = new SkyMapWidget();
    m_skyMap->setLegendVisible(false);
    m_skyMap->setMinimumWidth(420);

    splitter->addWidget(left);
    splitter->addWidget(m_skyMap);
    splitter->setSizes({700, 500});
    layout->addWidget(splitter, 1);

    connect(m_searchEdit, &QLineEdit::returnPressed, this, &SatelliteTab::onSearch);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this, [this](const QString& text) {
        m_searchEdit->setText(text);
        onSearch();
    });
    connect(findButton, &QPushButton::clicked, this, &SatelliteTab::onSearch);
    connect(m_minElevationSpin, &QSpinBox::valueChanged, this, [this](int elevation) {
        QSettings().setValue("satellite/minElevation", elevation);
        recalculatePasses();
    });
    connect(m_passTable, &QTableWidget::itemSelectionChanged, this, &SatelliteTab::onPassSelectionChanged);
    connect(m_armButton, &QPushButton::clicked, this, &SatelliteTab::onArmClicked);

    connect(m_tracker, &SatelliteTracker::tleDataUpdated, this, &SatelliteTab::refreshSatelliteList);
    connect(m_tracker, &SatelliteTracker::tleDataUpdated, this, [this]() { recalculatePasses(); });
    connect(m_tracker, &SatelliteTracker::locationUpdated, this, [this]() { recalculatePasses(); });
    connect(m_tracker, &SatelliteTracker::positionsUpdated, this, &SatelliteTab::refreshLive);
}

void SatelliteTab::refreshSatelliteList() {
    QStringList entries;
    for (const Satellite& sat : m_tracker->getAllSatellites()) {
        entries.append(QString("%1  #%2").arg(sat.name).arg(sat.catalogNumber));
    }
    entries.sort(Qt::CaseInsensitive);
    m_completionModel->setStringList(entries);
}

void SatelliteTab::onSearch() {
    QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    QList<Satellite> satellites = m_tracker->getAllSatellites();
    int found = 0;

    // "NAME  #12345" from the completer, or a bare catalog number
    QRegularExpressionMatch match = QRegularExpression("#?(\\d+)$").match(query);
    if (match.hasMatch()) {
        int catalog = match.captured(1).toInt();
        for (const Satellite& sat : satellites) {
            if (sat.catalogNumber == catalog) { found = catalog; break; }
        }
    }
    // Exact name, then first partial name match
    if (!found) {
        for (const Satellite& sat : satellites) {
            if (sat.name.compare(query, Qt::CaseInsensitive) == 0) { found = sat.catalogNumber; break; }
        }
    }
    if (!found) {
        for (const Satellite& sat : satellites) {
            if (sat.name.contains(query, Qt::CaseInsensitive)) { found = sat.catalogNumber; break; }
        }
    }

    if (!found) {
        m_titleLabel->setText(QString("No loaded satellite matches \"%1\"").arg(query));
        return;
    }
    showSatellite(found);
}

bool SatelliteTab::currentSatellite(Satellite* out) const {
    if (m_catalogNumber == 0) return false;
    for (const Satellite& sat : m_tracker->getAllSatellites()) {
        if (sat.catalogNumber == m_catalogNumber) {
            *out = sat;
            return true;
        }
    }
    return false;
}

void SatelliteTab::showSatellite(int catalogNumber, const QDateTime& passAosUtc) {
    m_catalogNumber = catalogNumber;
    Satellite sat;
    if (!currentSatellite(&sat)) return;

    m_searchEdit->setText(QString("%1  #%2").arg(sat.name).arg(sat.catalogNumber));
    m_titleLabel->setText(sat.name);
    updateDetails(sat);
    recalculatePasses(passAosUtc);
    emit satelliteChosen(sat.name);
}

void SatelliteTab::updateDetails(const Satellite& sat) {
    QStringList lines;
    lines << QString("<b>NORAD</b> %1").arg(sat.catalogNumber);

    try {
        libsgp4::Tle tle(sat.name.toStdString(), sat.line1.toStdString(), sat.line2.toStdString());
        libsgp4::DateTime e = tle.Epoch();
        QDateTime epoch(QDate(e.Year(), e.Month(), e.Day()), QTime(e.Hour(), e.Minute(), e.Second()),
                        QTimeZone::UTC);
        double ageDays = epoch.secsTo(QDateTime::currentDateTimeUtc()) / 86400.0;
        double n = tle.MeanMotion() * 2.0 * M_PI / 86400.0;  // rad/s
        double a = std::cbrt(kEarthMu / (n * n));
        double ecc = tle.Eccentricity();
        lines << QString("<b>Orbit</b> %1° inclination, %2 min period, %3 × %4 km")
                     .arg(tle.Inclination(true), 0, 'f', 1)
                     .arg(1440.0 / tle.MeanMotion(), 0, 'f', 1)
                     .arg(a * (1 - ecc) - kEarthRadiusKm, 0, 'f', 0)
                     .arg(a * (1 + ecc) - kEarthRadiusKm, 0, 'f', 0);
        QString age = QString("%1 days old").arg(ageDays, 0, 'f', 1);
        if (ageDays > 7) age = "<span style='color:#d08030'>" + age + " - refresh for accuracy</span>";
        lines << QString("<b>TLE epoch</b> %1 (%2)").arg(localTime(epoch, true), age);
    } catch (const std::exception&) {
        lines << "<b>TLE</b> invalid";
    }

    const SatellitePosition& p = sat.position;
    if (sat.isVisible) {
        lines << QString("<b>Now</b> <span style='color:#40a060'>up</span>: az %1°, el %2°, %3 km, %4 km/s")
                     .arg(p.azimuth, 0, 'f', 0).arg(p.elevation, 0, 'f', 1)
                     .arg(p.range, 0, 'f', 0).arg(p.rangerate, 0, 'f', 2);
    } else {
        lines << "<b>Now</b> below the horizon";
    }

    m_detailsLabel->setText(lines.join("<br>"));

    // Downlinks only change with a new satellite (or SatNOGS reload)
    if (sat.catalogNumber == m_downlinkListCatalog &&
        m_downlinkList->count() == int(std::count_if(sat.transponders.begin(), sat.transponders.end(),
                                           [](const Transponder& t) { return t.downlinkFreq > 0; }))) {
        return;
    }
    m_downlinkListCatalog = sat.catalogNumber;
    m_downlinkList->clear();
    for (const Transponder& t : sat.transponders) {
        if (t.downlinkFreq <= 0) continue;
        bool tunable = m_radio->inTunerRange(t.downlinkFreq);
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1 MHz   %2 (%3)%4").arg(t.downlinkFreq, 0, 'f', 3).arg(t.name, t.mode,
                                                  tunable ? QString() : QString(" - out of range")));
        if (!tunable) item->setForeground(Qt::gray);
        m_downlinkList->addItem(item);
    }
    if (m_downlinkList->count() == 0) m_downlinkList->addItem("None known");
}

void SatelliteTab::recalculatePasses(const QDateTime& selectAosUtc) {
    Satellite sat;
    if (!currentSatellite(&sat)) return;

    // Keep the current selection across recalculations
    QDateTime select = selectAosUtc;
    int current = selectedPassIndex();
    if (!select.isValid() && current >= 0) select = m_passes[current].aos;

    m_passes = PassPredictor::predictPasses(sat, m_tracker->getObserverLocation(),
                                            QDateTime::currentDateTimeUtc(), kPassHours,
                                            m_minElevationSpin->value());

    m_passTable->blockSignals(true);
    m_passTable->setRowCount(m_passes.size());
    int selectRow = m_passes.isEmpty() ? -1 : 0;
    for (int row = 0; row < m_passes.size(); ++row) {
        const SatellitePass& pass = m_passes[row];
        m_passTable->setItem(row, 0, new QTableWidgetItem(
            pass.aosBeforeSearch ? "(already up)" : localTime(pass.aos, true)));
        m_passTable->setItem(row, 1, new QTableWidgetItem(QString::number(pass.maxElevation, 'f', 0) + "°"));
        m_passTable->setItem(row, 2, new QTableWidgetItem(localTime(pass.tca, false)));
        m_passTable->setItem(row, 3, new QTableWidgetItem(
            pass.losAfterSearch ? "(still up)" : localTime(pass.los, false)));
        m_passTable->setItem(row, 4, new QTableWidgetItem(PassPredictor::formatDuration(pass.durationSeconds())));
        m_passTable->setItem(row, 5, new QTableWidgetItem(PassPredictor::directionText(pass)));
        // Same pass predicted again differs by at most a second or two
        if (select.isValid() && std::abs(pass.aos.secsTo(select)) < 60) selectRow = row;
    }
    m_passTable->resizeColumnsToContents();
    m_passTable->blockSignals(false);

    if (selectRow >= 0) m_passTable->selectRow(selectRow);
    onPassSelectionChanged();
    refreshLive();
}

int SatelliteTab::selectedPassIndex() const {
    QList<QTableWidgetItem*> items = m_passTable->selectedItems();
    if (items.isEmpty()) return -1;
    int row = items.first()->row();
    return row < m_passes.size() ? row : -1;
}

void SatelliteTab::onPassSelectionChanged() {
    Satellite sat;
    int index = selectedPassIndex();
    if (index < 0 || !currentSatellite(&sat)) {
        m_skyMap->setPassTrack({}, QString(), QString());
        m_armButton->setEnabled(false);
        m_armHint->setText(m_catalogNumber && m_passes.isEmpty() ? "No passes in this window" : QString());
        return;
    }

    const SatellitePass& pass = m_passes[index];
    m_skyMap->setPassTrack(PassPredictor::passTrack(sat, m_tracker->getObserverLocation(), pass),
                           "Rise " + localTime(pass.aos, false),
                           "Set " + localTime(pass.los, false));

    bool tunable = m_radio->defaultTransponder(sat) >= 0;
    m_armButton->setEnabled(tunable);
    m_armHint->setText(tunable ? QString() : "No downlink in the receiver's range");
}

void SatelliteTab::onArmClicked() {
    int index = selectedPassIndex();
    if (index < 0) return;
    const SatellitePass& pass = m_passes[index];
    emit armRequested(pass.satelliteName, pass.aos);
}

void SatelliteTab::refreshLive() {
    Satellite sat;
    if (!currentSatellite(&sat)) return;

    updateDetails(sat);

    // Current position (if up) on top of the selected pass track
    QList<Satellite> shown;
    if (sat.isVisible) {
        sat.groundTrack.clear();
        shown.append(sat);
    }
    m_skyMap->setSatellites(shown);
    m_skyMap->setSelectedSatellite(sat.name);

    // Drop passes that have ended; highlight one in progress
    QDateTime now = QDateTime::currentDateTimeUtc();
    if (!m_passes.isEmpty() && m_passes.first().los <= now) {
        recalculatePasses();
        return;
    }
    for (int row = 0; row < m_passes.size(); ++row) {
        QBrush background = m_passes[row].isInProgress(now) ? QBrush(kInProgressColor) : QBrush();
        for (int col = 0; col < m_passTable->columnCount(); ++col) {
            m_passTable->item(row, col)->setBackground(background);
        }
    }
}
