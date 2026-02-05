#include "mainwindow.h"
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSplitter>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tracker(new SatelliteTracker(this))
    , m_updateTimer(new QTimer(this))
{
    setupUI();
    setupConnections();

    // Start update timer (update every 2 seconds)
    m_updateTimer->start(2000);

    // Set window properties
    setWindowTitle("Satellite Tracker");
    resize(1600, 850); // Wider to show full table without scrolling

    // Auto-fetch location and TLE data on startup
    m_tracker->fetchCurrentLocation();
    m_tracker->fetchTLEData();
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

    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    statusLayout->addWidget(m_locationLabel);
    mainLayout->addLayout(statusLayout);

    // Compact control panel
    QGroupBox* controlGroup = new QGroupBox("Controls");
    controlGroup->setMaximumHeight(70); // Limit height
    QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
    controlLayout->setSpacing(6);
    controlLayout->setContentsMargins(6, 6, 6, 6);

    m_fetchTLEButton = new QPushButton("Fetch Satellite Data");
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
    controlLayout->addWidget(m_fetchTLEButton);
    controlLayout->addStretch();

    mainLayout->addWidget(controlGroup);

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

    mainLayout->addWidget(splitter);
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
}

void MainWindow::onUpdateTimer() {
    m_tracker->updatePositions();
}

void MainWindow::onFetchTLEClicked() {
    m_statusLabel->setText("Fetching satellite data...");
    m_tracker->fetchTLEData();
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

        // Name
        m_satelliteTable->setItem(i, 0, new QTableWidgetItem(sat.name));

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
        }
    } else {
        // Clear selection
        m_skyMap->setSelectedSatellite(QString());
    }

    m_skyMap->blockSignals(false);
}

void MainWindow::onSkyMapSatelliteClicked(const QString& satelliteName) {
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
