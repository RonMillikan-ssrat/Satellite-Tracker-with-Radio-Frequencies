#ifndef SATELLITETAB_H
#define SATELLITETAB_H

#include <QWidget>
#include <QLineEdit>
#include <QCompleter>
#include <QStringListModel>
#include <QTableWidget>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include "satellitetracker.h"
#include "radiocontroller.h"
#include "passpredictor.h"
#include "skymapwidget.h"

// "Satellite" tab: look up one satellite by name or catalog number and show
// its orbit, frequencies and next passes, with the selected pass drawn on a
// sky map. A pass can be armed so the radio starts tracking at rise.
class SatelliteTab : public QWidget {
    Q_OBJECT

public:
    SatelliteTab(SatelliteTracker* tracker, RadioController* radio, QWidget *parent = nullptr);

    // Show a satellite; optionally preselect the pass rising at passAosUtc.
    void showSatellite(int catalogNumber, const QDateTime& passAosUtc = QDateTime());

signals:
    // A satellite was opened; the radio panel should offer its transponders.
    void satelliteChosen(const QString& satelliteName);
    void armRequested(const QString& satelliteName, const QDateTime& aosUtc);

private slots:
    void onSearch();
    void onPassSelectionChanged();
    void onArmClicked();
    void refreshSatelliteList();
    void refreshLive();

private:
    SatelliteTracker* m_tracker;
    RadioController* m_radio;

    QLineEdit* m_searchEdit;
    QStringListModel* m_completionModel;
    QSpinBox* m_minElevationSpin;
    QLabel* m_titleLabel;
    QLabel* m_detailsLabel;
    QListWidget* m_downlinkList;
    int m_downlinkListCatalog = 0;
    QTableWidget* m_passTable;
    QPushButton* m_armButton;
    QLabel* m_armHint;
    SkyMapWidget* m_skyMap;

    int m_catalogNumber = 0;
    QList<SatellitePass> m_passes;

    bool currentSatellite(Satellite* out) const;
    void recalculatePasses(const QDateTime& selectAosUtc = QDateTime());
    void updateDetails(const Satellite& sat);
    int selectedPassIndex() const;
};

#endif // SATELLITETAB_H
