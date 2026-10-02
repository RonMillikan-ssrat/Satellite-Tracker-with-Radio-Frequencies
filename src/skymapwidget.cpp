#include "skymapwidget.h"
#include <QPaintEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SkyMapWidget::SkyMapWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(400, 400);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMouseTracking(true); // Enable mouse tracking for hover effects
    setCursor(Qt::CrossCursor);
}

void SkyMapWidget::setSatellites(const QList<Satellite>& satellites) {
    m_satellites = satellites;
    update(); // Trigger repaint
}

void SkyMapWidget::setSelectedSatellite(const QString& satelliteName) {
    if (m_selectedSatellite != satelliteName) {
        m_selectedSatellite = satelliteName;
        update();
    }
}

void SkyMapWidget::setPassTrack(const QList<QPointF>& track, const QString& riseLabel, const QString& setLabel) {
    m_passTrack = track;
    m_passRiseLabel = riseLabel;
    m_passSetLabel = setLabel;
    update();
}

void SkyMapWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Fill background
    painter.fillRect(rect(), QColor(20, 20, 40)); // Dark blue sky

    // Draw the sky map components
    drawElevationCircles(painter);
    drawCompass(painter);
    drawGroundTracks(painter);  // Draw tracks before satellites
    drawPassTrack(painter);
    drawSatellites(painter);
    if (m_legendVisible) drawLegend(painter);  // Draw legend last
}

void SkyMapWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
}

void SkyMapWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QString satName = findSatelliteAtPoint(event->pos());
        if (!satName.isEmpty()) {
            // Clicked on a satellite
            m_selectedSatellite = satName;
            emit satelliteClicked(satName);
            update();
        } else {
            // Clicked on empty space - deselect
            if (!m_selectedSatellite.isEmpty()) {
                m_selectedSatellite.clear();
                emit satelliteClicked(QString()); // Signal with empty string to clear table selection
                update();
            }
        }
    }
}

void SkyMapWidget::mouseMoveEvent(QMouseEvent *event) {
    QString satName = findSatelliteAtPoint(event->pos());
    if (m_hoveredSatellite != satName) {
        m_hoveredSatellite = satName;
        if (!satName.isEmpty()) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::CrossCursor);
        }
        update();
    }
}

QString SkyMapWidget::findSatelliteAtPoint(const QPointF& point) const {
    const double clickRadius = 10.0; // Pixels

    for (const Satellite& sat : m_satellites) {
        if (!sat.isVisible) continue;

        QPointF satPoint = azElToPoint(sat.position.azimuth, sat.position.elevation);
        double dx = point.x() - satPoint.x();
        double dy = point.y() - satPoint.y();
        double distance = sqrt(dx * dx + dy * dy);

        if (distance <= clickRadius) {
            return sat.name;
        }
    }

    return QString();
}

QPointF SkyMapWidget::azElToPoint(double azimuth, double elevation) const {
    // Polar projection: center = zenith (90° elevation)
    // radius increases as elevation decreases

    int centerX = width() / 2;
    int centerY = height() / 2;
    int maxRadius = std::min(centerX, centerY) - 40;

    // Convert elevation to radius (0° = max radius, 90° = center)
    double radius = maxRadius * (90.0 - elevation) / 90.0;

    // Convert azimuth to angle (0° = North = up, 90° = East = right)
    double angleRad = (90.0 - azimuth) * M_PI / 180.0;

    double x = centerX + radius * cos(angleRad);
    double y = centerY - radius * sin(angleRad);

    return QPointF(x, y);
}

void SkyMapWidget::drawCompass(QPainter& painter) {
    // int centerX = width() / 2;
    // int centerY = height() / 2;
    // int maxRadius = std::min(centerX, centerY) - 40;

    painter.setPen(QPen(Qt::white, 1));
    painter.setFont(QFont("Arial", 12, QFont::Bold));

    // Draw cardinal directions
    QStringList directions = {"N", "E", "S", "W"};
    QList<double> azimuths = {0.0, 90.0, 180.0, 270.0};

    for (int i = 0; i < directions.size(); ++i) {
        QPointF point = azElToPoint(azimuths[i], 0.0);

        // Offset text slightly outward
        double angleRad = (90.0 - azimuths[i]) * M_PI / 180.0;
        point.rx() += 20 * cos(angleRad);
        point.ry() -= 20 * sin(angleRad);

        painter.drawText(point, directions[i]);
    }

    // Draw intercardinal directions
    QStringList interDirections = {"NE", "SE", "SW", "NW"};
    QList<double> interAzimuths = {45.0, 135.0, 225.0, 315.0};

    painter.setFont(QFont("Arial", 10));
    for (int i = 0; i < interDirections.size(); ++i) {
        QPointF point = azElToPoint(interAzimuths[i], 0.0);

        double angleRad = (90.0 - interAzimuths[i]) * M_PI / 180.0;
        point.rx() += 20 * cos(angleRad);
        point.ry() -= 20 * sin(angleRad);

        painter.drawText(point, interDirections[i]);
    }
}

void SkyMapWidget::drawElevationCircles(QPainter& painter) {
    int centerX = width() / 2;
    int centerY = height() / 2;
    int maxRadius = std::min(centerX, centerY) - 40;

    // Draw elevation circles at 0°, 30°, 60°, 90°
    QList<double> elevations = {0.0, 30.0, 60.0};

    for (double elev : elevations) {
        double radius = maxRadius * (90.0 - elev) / 90.0;

        painter.setPen(QPen(QColor(100, 100, 150), 1, Qt::DashLine));
        painter.drawEllipse(QPointF(centerX, centerY), radius, radius);

        // Label elevation
        painter.setPen(QPen(Qt::gray, 1));
        painter.setFont(QFont("Arial", 9));
        QString label = QString::number((int)elev) + "°";
        painter.drawText(centerX + 5, centerY - radius + 15, label);
    }

    // Draw horizon (0° elevation)
    painter.setPen(QPen(Qt::white, 2));
    painter.drawEllipse(QPointF(centerX, centerY), maxRadius, maxRadius);
}

namespace {
// Above this many satellites in view (e.g. a Starlink catalog) the map draws
// small unlabeled dots and only the selected satellite's track and labels
const int kDenseSkyThreshold = 60;

int visibleCount(const QList<Satellite>& satellites) {
    int count = 0;
    for (const Satellite& sat : satellites) {
        if (sat.isVisible) ++count;
    }
    return count;
}
}

void SkyMapWidget::drawGroundTracks(QPainter& painter) {
    bool hasSelection = !m_selectedSatellite.isEmpty();
    bool dense = visibleCount(m_satellites) > kDenseSkyThreshold;

    for (const Satellite& sat : m_satellites) {
        if (!sat.isVisible || sat.groundTrack.isEmpty()) continue;

        bool isSelected = (sat.name == m_selectedSatellite);
        if (dense && !isSelected && sat.name != m_hoveredSatellite) continue;

        // Color based on range rate: blue (approaching) to red (departing)
        QColor trackColor;
        if (sat.position.rangerate < -0.1) {
            // Approaching - Blue
            trackColor = QColor(50, 100, 255, 150);
        } else if (sat.position.rangerate > 0.1) {
            // Departing - Red
            trackColor = QColor(255, 100, 50, 150);
        } else {
            // Nearly stationary - Purple
            trackColor = QColor(200, 100, 200, 150);
        }

        // Dim unselected tracks if there's a selection
        if (hasSelection && !isSelected) {
            trackColor.setAlpha(30); // Very dim
        } else if (isSelected) {
            trackColor.setAlpha(200); // Brighter for selected
        }

        // Draw the track as a polyline
        QPainterPath path;
        bool firstPoint = true;

        for (const QPointF& azEl : sat.groundTrack) {
            QPointF point = azElToPoint(azEl.x(), azEl.y());

            if (firstPoint) {
                path.moveTo(point);
                firstPoint = false;
            } else {
                path.lineTo(point);
            }
        }

        // Draw the track
        int lineWidth = isSelected ? 3 : 2;
        painter.setPen(QPen(trackColor, lineWidth, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // Draw small dots along the track
        if (!hasSelection || isSelected) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(trackColor);
            for (int i = 0; i < sat.groundTrack.size(); i += 3) { // Every 3rd point
                QPointF point = azElToPoint(sat.groundTrack[i].x(), sat.groundTrack[i].y());
                painter.drawEllipse(point, 2, 2);
            }
        }
    }
}

void SkyMapWidget::drawPassTrack(QPainter& painter) {
    if (m_passTrack.size() < 2) return;

    painter.save();
    QPainterPath path;
    int peak = 0;
    for (int i = 0; i < m_passTrack.size(); ++i) {
        QPointF point = azElToPoint(m_passTrack[i].x(), m_passTrack[i].y());
        if (i == 0) path.moveTo(point); else path.lineTo(point);
        if (m_passTrack[i].y() > m_passTrack[peak].y()) peak = i;
    }
    painter.setPen(QPen(QColor(255, 210, 60), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    // Labels go on the side of the marker facing the zenith, clear of the
    // compass labels outside the horizon circle
    QPointF center(width() / 2.0, height() / 2.0);
    painter.setFont(QFont("Arial", 9, QFont::Bold));
    QFontMetrics metrics(painter.font());
    auto marker = [&](const QPointF& azEl, const QColor& color, const QString& label) {
        QPointF point = azElToPoint(azEl.x(), azEl.y());
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(point, 6, 6);
        painter.setPen(Qt::white);
        int textWidth = metrics.horizontalAdvance(label);
        double x = point.x() < center.x() ? point.x() + 10 : point.x() - 10 - textWidth;
        double y = point.y() < center.y() ? point.y() + metrics.ascent() + 6 : point.y() - 8;
        painter.drawText(QPointF(x, y), label);
    };
    marker(m_passTrack.first(), QColor(80, 220, 120), m_passRiseLabel);
    marker(m_passTrack.last(), QColor(255, 100, 80), m_passSetLabel);
    marker(m_passTrack[peak], QColor(255, 210, 60),
           QString::number(m_passTrack[peak].y(), 'f', 0) + "°");
    painter.restore();
}

void SkyMapWidget::drawLegend(QPainter& painter) {
    // Save painter state to prevent interference
    painter.save();

    // Draw legend in top-left corner
    int legendX = 10;
    int legendY = 10;
    int legendWidth = 180;
    int legendHeight = 100;

    // Semi-transparent black background
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.setPen(Qt::NoPen);
    painter.drawRect(legendX, legendY, legendWidth, legendHeight);

    // White border
    painter.setPen(QPen(Qt::white, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(legendX, legendY, legendWidth, legendHeight);

    // Title
    painter.setFont(QFont("Arial", 10, QFont::Bold));
    painter.setPen(Qt::white);
    painter.drawText(legendX + 5, legendY + 18, "Satellite Motion");

    // Legend items
    painter.setFont(QFont("Arial", 9));
    int itemY = legendY + 35;
    int itemSpacing = 22;

    // Approaching (blue)
    painter.setBrush(QColor(50, 100, 255));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(legendX + 10, itemY - 5, 10, 10);
    painter.setPen(Qt::white);
    painter.drawText(legendX + 30, itemY + 5, "Approaching");

    // Stationary (purple)
    itemY += itemSpacing;
    painter.setBrush(QColor(200, 100, 200));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(legendX + 10, itemY - 5, 10, 10);
    painter.setPen(Qt::white);
    painter.drawText(legendX + 30, itemY + 5, "Stationary");

    // Departing (red)
    itemY += itemSpacing;
    painter.setBrush(QColor(255, 100, 50));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(legendX + 10, itemY - 5, 10, 10);
    painter.setPen(Qt::white);
    painter.drawText(legendX + 30, itemY + 5, "Departing");

    // Restore painter state
    painter.restore();
}

void SkyMapWidget::drawSatellites(QPainter& painter) {
    // Determine if we should dim unselected satellites
    bool hasSelection = !m_selectedSatellite.isEmpty();
    bool dense = visibleCount(m_satellites) > kDenseSkyThreshold;

    for (const Satellite& sat : m_satellites) {
        if (!sat.isVisible) continue;

        QPointF point = azElToPoint(sat.position.azimuth, sat.position.elevation);

        bool isSelected = (sat.name == m_selectedSatellite);
        bool isHovered = (sat.name == m_hoveredSatellite);

        // Color based on range rate: blue (approaching) to red (departing)
        QColor color;
        // Range rate is negative when approaching (distance decreasing)
        // Range rate is positive when departing (distance increasing)
        if (sat.position.rangerate < -0.1) {
            // Approaching - Blue
            color = QColor(50, 100, 255);
        } else if (sat.position.rangerate > 0.1) {
            // Departing - Red
            color = QColor(255, 100, 50);
        } else {
            // Nearly stationary - Purple
            color = QColor(200, 100, 200);
        }

        // Dim unselected satellites if there's a selection
        if (hasSelection && !isSelected) {
            color.setAlpha(80); // Make translucent
        }

        // Draw satellite circle
        int radius = dense ? 3 : 6;
        if (isSelected) {
            radius = 10; // Larger for selected
            // Draw selection ring
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, radius + 4, radius + 4);
        } else if (isHovered) {
            radius = 8; // Slightly larger for hover
            painter.setPen(QPen(Qt::white, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, radius + 2, radius + 2);
        }

        // Draw satellite as a circle
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(point, radius, radius);

        // Draw satellite name (always show for selected, hover, or if no selection)
        if (isSelected || isHovered || (!hasSelection && !dense)) {
            painter.setPen(Qt::white);
            painter.setFont(QFont("Arial", isSelected ? 9 : 8, isSelected ? QFont::Bold : QFont::Normal));
            painter.drawText(point.x() + radius + 5, point.y() + 5, sat.name);

            // Draw range rate and elevation
            QString rateText = QString::number(sat.position.rangerate, 'f', 2) + " km/s";
            QString elevText = QString::number(sat.position.elevation, 'f', 1) + "°";
            painter.setPen(Qt::lightGray);
            painter.setFont(QFont("Arial", 7));
            painter.drawText(point.x() + radius + 5, point.y() + 18, rateText);
            painter.drawText(point.x() + radius + 5, point.y() + 30, elevText);
        }
    }
}
