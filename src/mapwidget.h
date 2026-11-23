// Minimal slippy map: OpenStreetMap raster tiles, track polyline, car marker.
#pragma once

#include <QHash>
#include <QPixmap>
#include <QPointF>
#include <QSet>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;

class MapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MapWidget(QWidget *parent = nullptr);

    // Polylines in (lat, lon). The first one is drawn as the "current" clip.
    void setTracks(const QList<QList<QPointF>> &tracks, int current);
    void setCar(double lat, double lon, double heading, bool visible);
    void fitTracks();
    void setFollow(bool on) { m_follow = on; }
    void setZoom(int z) { m_zoom = qBound(2, z, 19); update(); }
    int zoom() const { return m_zoom; }
    bool follow() const { return m_follow; }
    void setDark(bool dark);
    bool isDark() const { return m_dark; }
    void setTileUrl(const QString &tmpl) { m_tileUrl = tmpl; m_tiles.clear(); update(); }

signals:
    // Emitted when the user clicks near the current track (index of point in that track)
    void trackClicked(int trackIndex, int pointIndex);
    void followChanged(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    QPointF project(double lat, double lon) const;  // world pixels at current zoom
    QPointF toScreen(double lat, double lon) const;
    void requestTile(int z, int x, int y);
    void tileArrived(QNetworkReply *r);

    QNetworkAccessManager *m_net;
    QString m_tileUrl = QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png");
    QHash<QString, QPixmap> m_tiles;      // as displayed (recoloured in dark mode)
    QHash<QString, QImage> m_rawTiles;    // original tiles, for switching themes
    bool m_dark = false;
    QSet<QString> m_pending;

    int m_zoom = 3;
    QPointF m_center{0.5, 0.5};  // normalized mercator [0,1]
    bool m_follow = true;

    QList<QList<QPointF>> m_tracks;
    int m_current = -1;
    double m_carLat = 0, m_carLon = 0, m_carHeading = 0;
    bool m_carVisible = false;

    QPoint m_dragStart;
    QPointF m_dragCenter;
    bool m_dragging = false, m_dragMoved = false;
};
