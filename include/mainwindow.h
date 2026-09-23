#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QLineEdit>
#include <QVBoxLayout>
#include "satellitetracker.h"
#include "skymapwidget.h"
#include "apiserver.h"

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
    
    // Backend
    SatelliteTracker* m_tracker;
    QTimer* m_updateTimer;
    ApiServer* m_apiServer;
    
    // Setup functions
    void setupUI();
    void setupConnections();
    void updateSatelliteTable();
};

#endif // MAINWINDOW_H
