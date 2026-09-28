#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include "satellitetracker.h"
#include "skymapwidget.h"
#include "apiserver.h"
#include "radiocontroller.h"

// Custom table item that sorts by numeric value instead of text
class NumericTableItem : public QTableWidgetItem {
public:
    NumericTableItem(double value, const QString& displayText)
        : QTableWidgetItem(displayText), m_numericValue(value) {}
    
    bool operator<(const QTableWidgetItem& other) const override {
        const NumericTableItem* numericOther = dynamic_cast<const NumericTableItem*>(&other);
        if (numericOther) {
            return m_numericValue < numericOther->m_numericValue;
        }
        return QTableWidgetItem::operator<(other);
    }
    
private:
    double m_numericValue;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
private slots:
    void onLocationUpdated(const ObserverLocation& location);
    void onTLEDataUpdated(int count);
    void onPositionsUpdated();
    void onErrorOccurred(const QString& error);
    void onUpdateTimer();
    void onFetchTLEClicked();
    void onAutoLocateClicked();
    void onLocationEditFinished();
    void onTableSelectionChanged();
    void onSkyMapSatelliteClicked(const QString& satelliteName);
    void onTableDoubleClicked(int row, int column);
    void onRadioConnectClicked();
    void onRadioTuneClicked();
    void onRadioStatusChanged();
    
private:
    // GUI components
    SkyMapWidget* m_skyMap;
    QTableWidget* m_satelliteTable;
    QLabel* m_statusLabel;
    QLabel* m_locationLabel;
    QPushButton* m_fetchTLEButton;
    QPushButton* m_autoLocateButton;
    QLineEdit* m_latEdit;
    QLineEdit* m_lonEdit;
    QLineEdit* m_altEdit;

    // Radio (rigctl) controls
    QLineEdit* m_radioHostEdit;
    QSpinBox* m_radioPortSpin;
    QPushButton* m_radioConnectButton;
    QLabel* m_radioSatLabel;
    QComboBox* m_transponderCombo;
    QPushButton* m_radioTuneButton;
    QPushButton* m_radioStopButton;
    QLabel* m_radioStatusLabel;
    QString m_radioSatName;   // satellite whose transponders are in the combo
    
    // Backend
    SatelliteTracker* m_tracker;
    QTimer* m_updateTimer;
    RadioController* m_radio;
    ApiServer* m_apiServer;
    
    // Setup functions
    void setupUI();
    void setupConnections();
    void updateSatelliteTable();
    void setRadioSatellite(const QString& satelliteName);
    bool findSatellite(const QString& name, Satellite* out) const;
};

#endif // MAINWINDOW_H
