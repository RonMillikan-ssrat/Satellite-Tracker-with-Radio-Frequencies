#ifndef SKYMAPWIDGET_H
#define SKYMAPWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QList>
#include "satellite.h"

class SkyMapWidget : public QWidget {
    Q_OBJECT
    
public:
    explicit SkyMapWidget(QWidget *parent = nullptr);
    
    void setSatellites(const QList<Satellite>& satellites);
    void setSelectedSatellite(const QString& satelliteName);
    QString getSelectedSatellite() const { return m_selectedSatellite; }

    // Draw one predicted pass (x = azimuth, y = elevation) with rise, peak and
    // set markers. An empty track clears it.
    void setPassTrack(const QList<QPointF>& track, const QString& riseLabel, const QString& setLabel);
    void setLegendVisible(bool visible) { m_legendVisible = visible; update(); }
    
signals:
    void satelliteClicked(const QString& satelliteName);
    
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    
private:
    QList<Satellite> m_satellites;
    QString m_selectedSatellite;
    QString m_hoveredSatellite;
    QList<QPointF> m_passTrack;
    QString m_passRiseLabel;
    QString m_passSetLabel;
    bool m_legendVisible = true;
    
    // Convert azimuth/elevation to widget coordinates (polar projection)
    QPointF azElToPoint(double azimuth, double elevation) const;
    
    // Find satellite at point
    QString findSatelliteAtPoint(const QPointF& point) const;
    
    // Draw compass directions
    void drawCompass(QPainter& painter);
    
    // Draw elevation circles
    void drawElevationCircles(QPainter& painter);
    
    // Draw ground tracks
    void drawGroundTracks(QPainter& painter);
    
    // Draw the predicted pass set with setPassTrack()
    void drawPassTrack(QPainter& painter);

    // Draw legend
    void drawLegend(QPainter& painter);
    
    // Draw satellites
    void drawSatellites(QPainter& painter);
};

#endif // SKYMAPWIDGET_H
