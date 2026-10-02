#include "mainwindow.h"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSplitter>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tracker(new SatelliteTracker(this))
    , m_updateTimer(new QTimer(this))
    , m_radio(new RadioController(m_tracker, this))
    , m_apiServer(new ApiServer(m_tracker, m_radio, this))
{
    setupUI();
    setupConnections();

    // Start update timer (update every 2 seconds)
    m_updateTimer->start(2000);

    // Set window properties
    setWindowTitle("Satellite Tracker");
    resize(1600, 850); // Wider to show full table without scrolling

    // Start local JSON API for external tools (port via SAT_TRACKER_API_PORT)
    quint16 apiPort = qEnvironmentVariableIntValue("SAT_TRACKER_API_PORT");
    if (apiPort == 0) apiPort = 8765;
    if (!m_apiServer->start(apiPort)) {
        m_statusLabel->setText(QString("Error: could not start API on port %1").arg(apiPort));
    }

    // Auto-fetch location on startup, and load only the last-used catalog
    // (from the local cache when it is fresh; nothing else is downloaded)
    m_tracker->fetchCurrentLocation();
    QString catalogId = QSettings().value("catalog/id", SatelliteCatalog::defaultId()).toString();
    if (!SatelliteCatalog::find(catalogId)) catalogId = SatelliteCatalog::defaultId();
    selectCatalogInCombo(catalogId);
    m_tracker->loadCatalog(catalogId);
}

MainWindow::~MainWindow() {
}

void MainWindow::setupUI() {
    // Create central widget
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // Main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(6, 6, 6, 6); // Reduce margins
    mainLayout->setSpacing(4); // Reduce spacing

    // Compact status bar at top
    QHBoxLayout* statusLayout = new QHBoxLayout();
    statusLayout->setSpacing(10);
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setMaximumHeight(20); // Limit height
    m_locationLabel = new QLabel("Location: Unknown");
    m_locationLabel->setMaximumHeight(20);
    m_catalogLabel = new QLabel("No catalog loaded");
    m_catalogLabel->setMaximumHeight(20);

    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_catalogLabel);
    statusLayout->addSpacing(16);
    statusLayout->addWidget(m_locationLabel);
    mainLayout->addLayout(statusLayout);

    // Compact control panel
    QGroupBox* controlGroup = new QGroupBox("Controls");
    controlGroup->setMaximumHeight(70); // Limit height
    QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
    controlLayout->setSpacing(6);
    controlLayout->setContentsMargins(6, 6, 6, 6);

    m_catalogCombo = new QComboBox();
    for (const SatelliteCatalog& catalog : SatelliteCatalog::all()) {
        m_catalogCombo->addItem(catalog.label, catalog.id);
        m_catalogCombo->setItemData(m_catalogCombo->count() - 1, catalog.description, Qt::ToolTipRole);
    }
    m_catalogCombo->setToolTip("Which satellites to track. A catalog is downloaded only when you choose it.");

    m_fetchTLEButton = new QPushButton("Refresh Data");
    m_fetchTLEButton->setToolTip("Re-download the selected catalog. CelesTrak updates about every "
                                 "2 hours, so a newer cached copy is reused instead.");
    m_autoLocateButton = new QPushButton("Auto-Locate");

    // Compact location inputs
    QLabel* locLabel = new QLabel("Location:");
    locLabel->setMaximumWidth(60);

    QLabel* latLabel = new QLabel("Lat:");
    latLabel->setMaximumWidth(30);
    m_latEdit = new QLineEdit();
    m_latEdit->setPlaceholderText("39.7392");
    m_latEdit->setMaximumWidth(80);

    QLabel* lonLabel = new QLabel("Lon:");
    lonLabel->setMaximumWidth(30);
    m_lonEdit = new QLineEdit();
    m_lonEdit->setPlaceholderText("-104.9903");
    m_lonEdit->setMaximumWidth(80);

    QLabel* altLabel = new QLabel("Alt (m):");
    altLabel->setMaximumWidth(50);
    m_altEdit = new QLineEdit();
    m_altEdit->setPlaceholderText("1609");
    m_altEdit->setMaximumWidth(60);

    controlLayout->addWidget(locLabel);
    controlLayout->addWidget(latLabel);
    controlLayout->addWidget(m_latEdit);
    controlLayout->addWidget(lonLabel);
    controlLayout->addWidget(m_lonEdit);
    controlLayout->addWidget(altLabel);
    controlLayout->addWidget(m_altEdit);
    controlLayout->addWidget(m_autoLocateButton);
    controlLayout->addSpacing(12);
    controlLayout->addWidget(new QLabel("Catalog:"));
    controlLayout->addWidget(m_catalogCombo);
    controlLayout->addWidget(m_fetchTLEButton);
    controlLayout->addStretch();

    mainLayout->addWidget(controlGroup);

    // Radio control: drives Gqrx / SDR++ / rigctld over the rigctl protocol
    QGroupBox* radioGroup = new QGroupBox("Radio (rigctl)");
    radioGroup->setMaximumHeight(70);
    QHBoxLayout* radioLayout = new QHBoxLayout(radioGroup);
    radioLayout->setSpacing(6);
    radioLayout->setContentsMargins(6, 6, 6, 6);

    QSettings settings;
    m_radioHostEdit = new QLineEdit(settings.value("radio/host", "127.0.0.1").toString());
    m_radioHostEdit->setMaximumWidth(120);
    m_radioPortSpin = new QSpinBox();
    m_radioPortSpin->setRange(1, 65535);
    m_radioPortSpin->setValue(settings.value("radio/port", 7356).toInt());
    m_radioPortSpin->setToolTip("Gqrx remote control: 7356, SDR++ rigctl server / rigctld: 4532");
    m_radioConnectButton = new QPushButton("Connect");

    m_radioSatLabel = new QLabel("Select a satellite");
    m_radioSatLabel->setMinimumWidth(140);
    m_transponderCombo = new QComboBox();
    m_transponderCombo->setMinimumWidth(260);
    m_transponderCombo->setEnabled(false);
    m_radioTuneButton = new QPushButton("Tune");
    m_radioTuneButton->setEnabled(false);
    m_radioTuneButton->setToolTip("Track the selected downlink with Doppler correction (or double-click a table row)");
    m_radioStopButton = new QPushButton("Stop");
    m_radioStopButton->setEnabled(false);
    m_radioStatusLabel = new QLabel("Not connected");

    radioLayout->addWidget(new QLabel("Host:"));
    radioLayout->addWidget(m_radioHostEdit);
    radioLayout->addWidget(new QLabel("Port:"));
    radioLayout->addWidget(m_radioPortSpin);
    radioLayout->addWidget(m_radioConnectButton);
    radioLayout->addSpacing(12);
    radioLayout->addWidget(m_radioSatLabel);
    radioLayout->addWidget(m_transponderCombo);
    radioLayout->addWidget(m_radioTuneButton);
    radioLayout->addWidget(m_radioStopButton);
    radioLayout->addSpacing(12);
    radioLayout->addWidget(m_radioStatusLabel, 1);

    m_radio->setServer(m_radioHostEdit->text(), quint16(m_radioPortSpin->value()));
    mainLayout->addWidget(radioGroup);

    // Create splitter for sky map and table
    QSplitter* splitter = new QSplitter(Qt::Horizontal);

    // Sky map
    m_skyMap = new SkyMapWidget();
    m_skyMap->setMinimumWidth(500); // Minimum width
    splitter->addWidget(m_skyMap);

    // Satellite table
    m_satelliteTable = new QTableWidget();
    m_satelliteTable->setColumnCount(7);
    m_satelliteTable->setHorizontalHeaderLabels({
        "Name", "Azimuth", "Elevation", "Range (km)", "Rate (km/s)", "Frequencies", "Status"
    });
    m_satelliteTable->horizontalHeader()->setStretchLastSection(true);
    m_satelliteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_satelliteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_satelliteTable->setAlternatingRowColors(true);
    m_satelliteTable->setSortingEnabled(true); // Enable sorting
    m_satelliteTable->setMinimumWidth(700); // Minimum width for table

    splitter->addWidget(m_satelliteTable);

    // Set splitter sizes (45% sky map, 55% table for better table visibility)
    splitter->setSizes({550, 750});

    // Location and radio controls stay above the tabs so the radio can be
    // watched and stopped from any of them
    m_tabs = new QTabWidget();
    m_passesTab = new PassesTab(m_tracker, m_radio);
    m_satelliteTab = new SatelliteTab(m_tracker, m_radio);
    m_tabs->addTab(splitter, "Live");
    m_tabs->addTab(m_passesTab, "Upcoming Passes");
    m_tabs->addTab(m_satelliteTab, "Satellite");
    mainLayout->addWidget(m_tabs);
}

void MainWindow::setupConnections() {
    // Connect tracker signals
    connect(m_tracker, &SatelliteTracker::locationUpdated,
            this, &MainWindow::onLocationUpdated);
    connect(m_tracker, &SatelliteTracker::tleDataUpdated,
            this, &MainWindow::onTLEDataUpdated);
    connect(m_tracker, &SatelliteTracker::positionsUpdated,
            this, &MainWindow::onPositionsUpdated);
    connect(m_tracker, &SatelliteTracker::errorOccurred,
            this, &MainWindow::onErrorOccurred);
    connect(m_tracker, &SatelliteTracker::statusMessage,
            m_statusLabel, &QLabel::setText);
    connect(m_tracker, &SatelliteTracker::catalogLoading,
            this, &MainWindow::onCatalogLoading);
    connect(m_tracker, &SatelliteTracker::catalogLoaded,
            this, &MainWindow::onCatalogLoaded);
    // activated (not currentIndexChanged): only user picks trigger a load
    connect(m_catalogCombo, &QComboBox::activated,
            this, &MainWindow::onCatalogActivated);

    // Connect buttons
    connect(m_fetchTLEButton, &QPushButton::clicked,
            this, &MainWindow::onFetchTLEClicked);
    connect(m_autoLocateButton, &QPushButton::clicked,
            this, &MainWindow::onAutoLocateClicked);

    // Connect location edits
    connect(m_latEdit, &QLineEdit::returnPressed,
            this, &MainWindow::onLocationEditFinished);
    connect(m_lonEdit, &QLineEdit::returnPressed,
            this, &MainWindow::onLocationEditFinished);
    connect(m_altEdit, &QLineEdit::returnPressed,
            this, &MainWindow::onLocationEditFinished);

    // Connect update timer
    connect(m_updateTimer, &QTimer::timeout,
            this, &MainWindow::onUpdateTimer);

    // Connect table selection
    connect(m_satelliteTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onTableSelectionChanged);

    // Connect sky map clicks
    connect(m_skyMap, &SkyMapWidget::satelliteClicked,
            this, &MainWindow::onSkyMapSatelliteClicked);

    // Radio
    connect(m_satelliteTable, &QTableWidget::cellDoubleClicked,
            this, &MainWindow::onTableDoubleClicked);
    connect(m_radioConnectButton, &QPushButton::clicked,
            this, &MainWindow::onRadioConnectClicked);
    connect(m_radioTuneButton, &QPushButton::clicked,
            this, &MainWindow::onRadioTuneClicked);
    connect(m_radioStopButton, &QPushButton::clicked,
            m_radio, &RadioController::stopTracking);
    connect(m_radio, &RadioController::statusChanged,
            this, &MainWindow::onRadioStatusChanged);

    // Pass prediction tabs
    connect(m_passesTab, &PassesTab::passActivated,
            this, &MainWindow::onPassActivated);
    connect(m_satelliteTab, &SatelliteTab::satelliteChosen,
            this, &MainWindow::setRadioSatellite);
    connect(m_satelliteTab, &SatelliteTab::armRequested,
            this, &MainWindow::onArmRequested);
}

void MainWindow::onLocationUpdated(const ObserverLocation& location) {
    m_locationLabel->setText(QString("Location: %1°, %2°, %3m")
                            .arg(location.latitude, 0, 'f', 4)
                            .arg(location.longitude, 0, 'f', 4)
                            .arg(location.altitude, 0, 'f', 0));

    // Update edit fields
    m_latEdit->setText(QString::number(location.latitude, 'f', 4));
    m_lonEdit->setText(QString::number(location.longitude, 'f', 4));
    m_altEdit->setText(QString::number(location.altitude, 'f', 0));

    m_statusLabel->setText("Location updated");
}

void MainWindow::onTLEDataUpdated(int count) {
    m_statusLabel->setText(QString("Loaded %1 satellites").arg(count));
}

void MainWindow::onPositionsUpdated() {
    updateSatelliteTable();

    // Update sky map
    m_skyMap->setSatellites(m_tracker->getVisibleSatellites());
}

void MainWindow::onErrorOccurred(const QString& error) {
    m_statusLabel->setText("Error: " + error);
    // A failed catalog download leaves the previous catalog loaded
    SatelliteTracker::CatalogState state = m_tracker->catalogState();
    if (!state.loading) {
        selectCatalogInCombo(state.id);
        updateCatalogLabel();
    }
}

void MainWindow::onUpdateTimer() {
    m_tracker->updatePositions();
    updateCatalogLabel();  // keeps the data age current
}

void MainWindow::onFetchTLEClicked() {
    QString catalogId = m_catalogCombo->currentData().toString();
    m_tracker->loadCatalog(catalogId, true);
}

void MainWindow::onCatalogActivated(int index) {
    QString catalogId = m_catalogCombo->itemData(index).toString();
    SatelliteTracker::CatalogState state = m_tracker->catalogState();
    if (catalogId == state.id && !state.loading) return;
    m_tracker->loadCatalog(catalogId);
}

void MainWindow::onCatalogLoading(const QString& catalogId) {
    selectCatalogInCombo(catalogId);
    updateCatalogLabel();
}

void MainWindow::onCatalogLoaded(const QString& catalogId) {
    selectCatalogInCombo(catalogId);
    if (SatelliteCatalog::find(catalogId)) {
        QSettings().setValue("catalog/id", catalogId);  // reopen with the same catalog
    }
    updateCatalogLabel();
}

void MainWindow::selectCatalogInCombo(const QString& catalogId) {
    int index = m_catalogCombo->findData(catalogId);
    if (index >= 0) m_catalogCombo->setCurrentIndex(index);
}

void MainWindow::updateCatalogLabel() {
    SatelliteTracker::CatalogState state = m_tracker->catalogState();
    QString text;
    if (!state.id.isEmpty()) {
        qint64 minutes = state.dataTimeUtc.secsTo(QDateTime::currentDateTimeUtc()) / 60;
        QString age = minutes < 1 ? QString("just now")
                    : minutes < 120 ? QString("%1 min old").arg(minutes)
                    : QString("%1 h old").arg(minutes / 60);
        text = QString("%1 · %2 satellites · data %3").arg(state.label).arg(state.satelliteCount).arg(age);
    }
    if (state.loading) {
        const SatelliteCatalog* loading = SatelliteCatalog::find(state.loadingId);
        QString name = loading ? loading->label : state.loadingId;
        text = text.isEmpty() ? QString("Downloading %1...").arg(name)
                              : text + QString("  |  downloading %1...").arg(name);
    }
    m_catalogLabel->setText(text.isEmpty() ? QString("No catalog loaded") : text);
}

void MainWindow::onAutoLocateClicked() {
    m_statusLabel->setText("Fetching location...");
    m_tracker->fetchCurrentLocation();
}

void MainWindow::onLocationEditFinished() {
    bool ok1, ok2, ok3;
    double lat = m_latEdit->text().toDouble(&ok1);
    double lon = m_lonEdit->text().toDouble(&ok2);
    double alt = m_altEdit->text().toDouble(&ok3);

    if (ok1 && ok2 && ok3) {
        ObserverLocation location(lat, lon, alt);
        m_tracker->setObserverLocation(location);
    } else {
        m_statusLabel->setText("Invalid location values");
    }
}

void MainWindow::updateSatelliteTable() {
    QList<Satellite> visible = m_tracker->getVisibleSatellites();

    // Store currently selected satellite name (if any) before clearing table
    QString selectedSatName;
    QList<QTableWidgetItem*> selectedItems = m_satelliteTable->selectedItems();
    if (!selectedItems.isEmpty()) {
        int row = selectedItems[0]->row();
        QTableWidgetItem* nameItem = m_satelliteTable->item(row, 0);
        if (nameItem) {
            selectedSatName = nameItem->text();
        }
    }

    // Also check sky map selection as authoritative source
    QString skyMapSelection = m_skyMap->getSelectedSatellite();
    if (!skyMapSelection.isEmpty()) {
        selectedSatName = skyMapSelection;
    }

    // Block ALL signals to prevent any events during update
    m_satelliteTable->blockSignals(true);

    // Disable sorting temporarily while updating to avoid issues
    bool sortingWasEnabled = m_satelliteTable->isSortingEnabled();
    m_satelliteTable->setSortingEnabled(false);

    // Clear selection before rebuilding
    m_satelliteTable->clearSelection();

    m_satelliteTable->setRowCount(visible.size());

    // int rowToSelect = -1;

    for (int i = 0; i < visible.size(); ++i) {
        const Satellite& sat = visible[i];

        // Check if this row should be selected
        // if (!selectedSatName.isEmpty() && sat.name == selectedSatName) {
        //     rowToSelect = i;
        // }

        // Name (bold while the radio is tracking it)
        QTableWidgetItem* nameItem = new QTableWidgetItem(sat.name);
        if (m_radio->isTracking() && sat.catalogNumber == m_radio->status().catalogNumber) {
            QFont font = nameItem->font();
            font.setBold(true);
            nameItem->setFont(font);
        }
        m_satelliteTable->setItem(i, 0, nameItem);

        // Azimuth (sortable by numeric value)
        m_satelliteTable->setItem(i, 1,
            new NumericTableItem(sat.position.azimuth,
                                 QString::number(sat.position.azimuth, 'f', 1) + "°"));

        // Elevation (sortable by numeric value)
        m_satelliteTable->setItem(i, 2,
            new NumericTableItem(sat.position.elevation,
                                 QString::number(sat.position.elevation, 'f', 1) + "°"));

        // Range (sortable by numeric value)
        m_satelliteTable->setItem(i, 3,
            new NumericTableItem(sat.position.range,
                                 QString::number(sat.position.range, 'f', 1)));

        // Range rate (sortable by numeric value)
        m_satelliteTable->setItem(i, 4,
            new NumericTableItem(sat.position.rangerate,
                                 QString::number(sat.position.rangerate, 'f', 2)));

        // Frequencies
        QString freqText;
        if (sat.transponders.isEmpty()) {
            freqText = "N/A";
        } else {
            QStringList freqList;
            for (const Transponder& trans : sat.transponders) {
                QString freq;
                if (trans.downlinkFreq > 0) {
                    freq = QString("↓%1 MHz (%2)")
                        .arg(trans.downlinkFreq, 0, 'f', 3)
                        .arg(trans.mode);
                    freqList.append(freq);
                }
            }
            freqText = freqList.join("; ");
            if (freqText.isEmpty()) freqText = "N/A";
        }
        m_satelliteTable->setItem(i, 5, new QTableWidgetItem(freqText));

        // Status (approaching/departing)
        QString status;
        QColor color;
        if (sat.position.rangerate < -0.1) {
            status = "Approaching";
            color = QColor(100, 150, 255); // Blue
        } else if (sat.position.rangerate > 0.1) {
            status = "Departing";
            color = QColor(255, 100, 100); // Red
        } else {
            status = "Stable";
            color = QColor(200, 200, 100); // Yellow
        }

        QTableWidgetItem* statusItem = new QTableWidgetItem(status);
        statusItem->setBackground(color);
        m_satelliteTable->setItem(i, 6, statusItem);
    }

    // Re-enable sorting (this will trigger a sort)
    if (sortingWasEnabled) {
        m_satelliteTable->setSortingEnabled(true);
    }

    // After sorting is done, find and select the correct row by name
    if (!selectedSatName.isEmpty()) {
        bool found = false;
        for (int row = 0; row < m_satelliteTable->rowCount(); ++row) {
            QTableWidgetItem* nameItem = m_satelliteTable->item(row, 0);
            if (nameItem && nameItem->text() == selectedSatName) {
                m_satelliteTable->selectRow(row);
                m_satelliteTable->setCurrentItem(nameItem); // Set current item explicitly
                found = true;
                break;
            }
        }

        // If not found (satellite went below horizon), clear everything
        if (!found) {
            m_satelliteTable->clearSelection();
            m_satelliteTable->setCurrentItem(nullptr);
            m_skyMap->setSelectedSatellite(QString()); // Also clear sky map
        }
    } else {
        // No selection - clear current item too
        m_satelliteTable->setCurrentItem(nullptr);
    }

    // Unblock signals AFTER selection is restored
    m_satelliteTable->blockSignals(false);

    // Don't auto-resize columns - let user control column widths
    // Only resize on first load if columns haven't been sized yet
    static bool firstLoad = true;
    if (firstLoad) {
        m_satelliteTable->resizeColumnsToContents();
        firstLoad = false;
    }
}

void MainWindow::onTableSelectionChanged() {
    // Block signals temporarily to prevent feedback loops
    m_skyMap->blockSignals(true);

    QList<QTableWidgetItem*> selectedItems = m_satelliteTable->selectedItems();
    if (!selectedItems.isEmpty()) {
        // Get the satellite name from the first column of selected row
        int row = selectedItems[0]->row();
        QTableWidgetItem* nameItem = m_satelliteTable->item(row, 0);
        if (nameItem) {
            QString satelliteName = nameItem->text();
            m_skyMap->setSelectedSatellite(satelliteName);
            setRadioSatellite(satelliteName);
        }
    } else {
        // Clear selection
        m_skyMap->setSelectedSatellite(QString());
    }

    m_skyMap->blockSignals(false);
}

void MainWindow::onSkyMapSatelliteClicked(const QString& satelliteName) {
    if (!satelliteName.isEmpty()) setRadioSatellite(satelliteName);

    // Block signals to prevent feedback loop
    m_satelliteTable->blockSignals(true);

    if (satelliteName.isEmpty()) {
        // Clear both selection AND current item to prevent subdued highlighting
        m_satelliteTable->clearSelection();
        m_satelliteTable->setCurrentItem(nullptr);
    } else {
        // Find and select the corresponding row in the table
        bool found = false;
        for (int row = 0; row < m_satelliteTable->rowCount(); ++row) {
            QTableWidgetItem* nameItem = m_satelliteTable->item(row, 0);
            if (nameItem && nameItem->text() == satelliteName) {
                m_satelliteTable->selectRow(row);
                m_satelliteTable->scrollToItem(nameItem);
                m_satelliteTable->setCurrentItem(nameItem); // Set current item explicitly
                found = true;
                break;
            }
        }

        // If not found, clear selection and current item
        if (!found) {
            m_satelliteTable->clearSelection();
            m_satelliteTable->setCurrentItem(nullptr);
        }
    }

    m_satelliteTable->blockSignals(false);
}

bool MainWindow::findSatellite(const QString& name, Satellite* out) const {
    for (const Satellite& sat : m_tracker->getAllSatellites()) {
        if (sat.name == name) {
            *out = sat;
            return true;
        }
    }
    return false;
}

void MainWindow::setRadioSatellite(const QString& satelliteName) {
    if (satelliteName == m_radioSatName) return;

    Satellite sat;
    if (!findSatellite(satelliteName, &sat)) return;
    m_radioSatName = satelliteName;
    m_radioSatLabel->setText(satelliteName);

    m_transponderCombo->clear();
    for (int i = 0; i < sat.transponders.size(); ++i) {
        const Transponder& t = sat.transponders[i];
        if (t.downlinkFreq <= 0) continue;
        bool tunable = m_radio->inTunerRange(t.downlinkFreq);
        m_transponderCombo->addItem(QString("%1 MHz  %2 (%3)%4")
                                        .arg(t.downlinkFreq, 0, 'f', 3)
                                        .arg(t.name, t.mode,
                                             tunable ? QString() : QString("  - out of range")),
                                    i);
        if (!tunable) {
            // Grey out downlinks the SDR can't receive (e.g. 10 GHz QO-100)
            m_transponderCombo->setItemData(m_transponderCombo->count() - 1, 0, Qt::UserRole - 1);
        }
    }

    int defaultIndex = m_radio->defaultTransponder(sat);
    int comboIndex = m_transponderCombo->findData(defaultIndex);
    if (comboIndex >= 0) m_transponderCombo->setCurrentIndex(comboIndex);

    bool canTune = defaultIndex >= 0;
    m_transponderCombo->setEnabled(m_transponderCombo->count() > 0);
    m_radioTuneButton->setEnabled(canTune);
    if (m_transponderCombo->count() == 0) {
        m_transponderCombo->addItem("No known downlinks");
    }
}

void MainWindow::onTableDoubleClicked(int row, int /*column*/) {
    QTableWidgetItem* nameItem = m_satelliteTable->item(row, 0);
    if (!nameItem) return;
    setRadioSatellite(nameItem->text());
    if (m_radioTuneButton->isEnabled()) onRadioTuneClicked();
}

void MainWindow::onRadioConnectClicked() {
    if (m_radio->status().connected) {
        m_radio->disconnectFromRadio();
        return;
    }
    QSettings settings;
    settings.setValue("radio/host", m_radioHostEdit->text().trimmed());
    settings.setValue("radio/port", m_radioPortSpin->value());
    m_radio->setServer(m_radioHostEdit->text().trimmed(), quint16(m_radioPortSpin->value()));
    m_radio->connectToRadio();
}

void MainWindow::onRadioTuneClicked() {
    Satellite sat;
    if (!findSatellite(m_radioSatName, &sat)) return;
    QVariant data = m_transponderCombo->currentData();
    if (!data.isValid()) return;

    m_radio->setServer(m_radioHostEdit->text().trimmed(), quint16(m_radioPortSpin->value()));
    QString failure = m_radio->startTracking(sat, data.toInt());
    if (!failure.isEmpty()) {
        m_radioStatusLabel->setText(failure);
    }
}

void MainWindow::onRadioStatusChanged() {
    RadioController::Status s = m_radio->status();

    m_radioConnectButton->setText(s.connected ? "Disconnect" : "Connect");
    m_radioHostEdit->setEnabled(!s.connected);
    m_radioPortSpin->setEnabled(!s.connected);
    m_radioStopButton->setEnabled(s.tracking || s.armed);

    if (s.tracking && s.connected && s.tunedHz > 0) {
        QString text = QString("%1: %2 MHz (Doppler %3%4 kHz")
                           .arg(s.satelliteName)
                           .arg(s.tunedHz / 1e6, 0, 'f', 6)
                           .arg(s.dopplerHz >= 0 ? "+" : "")
                           .arg(s.dopplerHz / 1e3, 0, 'f', 2);
        if (s.offsetHz != 0) {
            text += QString(", offset %1%2 kHz").arg(s.offsetHz >= 0 ? "+" : "").arg(s.offsetHz / 1e3, 0, 'f', 2);
        }
        text += ")";
        if (s.elevation < 0) text += " - below horizon";
        m_radioStatusLabel->setText(text);
    } else {
        m_radioStatusLabel->setText(s.message);
    }
    if (s.armed) {
        QString armed = QString("Armed: %1 at %2")
                            .arg(s.armedSatelliteName,
                                 s.armedAosUtc.toLocalTime().toString("ddd h:mm AP"));
        m_radioStatusLabel->setText(s.tracking ? m_radioStatusLabel->text() + "  |  " + armed : armed);
    }
    m_radioStatusLabel->setToolTip(s.message);
}

void MainWindow::onPassActivated(const SatellitePass& pass) {
    m_satelliteTab->showSatellite(pass.catalogNumber, pass.aos);
    m_tabs->setCurrentWidget(m_satelliteTab);
}

void MainWindow::onArmRequested(const QString& satelliteName, const QDateTime& aosUtc) {
    Satellite sat;
    if (!findSatellite(satelliteName, &sat)) return;
    setRadioSatellite(satelliteName);

    // Use the transponder chosen in the Radio panel
    QVariant data = m_transponderCombo->currentData();
    int index = data.isValid() ? data.toInt() : m_radio->defaultTransponder(sat);

    m_radio->setServer(m_radioHostEdit->text().trimmed(), quint16(m_radioPortSpin->value()));
    QString failure = m_radio->armForPass(sat, index, aosUtc);
    if (!failure.isEmpty()) {
        m_radioStatusLabel->setText(failure);
    }
}
