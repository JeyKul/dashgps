#include "mapwidget.h"

#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QtMath>

static constexpr int TILE = 256;

static QPointF mercator(double lat, double lon) {
    double x = (lon + 180.0) / 360.0;
    double s = std::sin(qDegreesToRadians(qBound(-85.0, lat, 85.0)));
    double y = 0.5 - std::log((1 + s) / (1 - s)) / (4 * M_PI);
    return {x, y};
}

MapWidget::MapWidget(QWidget *parent) : QWidget(parent), m_net(new QNetworkAccessManager(this)) {
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/tiles");
    cache->setMaximumCacheSize(500ll * 1024 * 1024);
    m_net->setCache(cache);
    connect(m_net, &QNetworkAccessManager::finished, this, &MapWidget::tileArrived);
    setMinimumSize(200, 200);
    setMouseTracking(false);
}

QPointF MapWidget::project(double lat, double lon) const {
    return mercator(lat, lon) * (TILE * double(1 << m_zoom));
}

QPointF MapWidget::toScreen(double lat, double lon) const {
    QPointF c = m_center * (TILE * double(1 << m_zoom));
    return project(lat, lon) - c + QPointF(width() / 2.0, height() / 2.0);
}

void MapWidget::setTracks(const QList<QList<QPointF>> &tracks, int current) {
    m_tracks = tracks;
    m_current = current;
    update();
}

void MapWidget::setCar(double lat, double lon, double heading, bool visible) {
    m_carLat = lat; m_carLon = lon; m_carHeading = heading; m_carVisible = visible;
    if (visible && m_follow) m_center = mercator(lat, lon);
    update();
}

void MapWidget::fitTracks() {
    double minX = 1, minY = 1, maxX = 0, maxY = 0;
    bool any = false;
    const auto &list = (m_current >= 0 && m_current < m_tracks.size() && !m_tracks[m_current].isEmpty())
                           ? QList<QList<QPointF>>{m_tracks[m_current]} : m_tracks;
    for (auto &t : list)
        for (auto &p : t) {
            QPointF m = mercator(p.x(), p.y());
            minX = qMin(minX, m.x()); maxX = qMax(maxX, m.x());
            minY = qMin(minY, m.y()); maxY = qMax(maxY, m.y());
            any = true;
        }
    if (!any) return;
    m_center = {(minX + maxX) / 2, (minY + maxY) / 2};
    m_zoom = 17;
    while (m_zoom > 2 && ((maxX - minX) * TILE * (1 << m_zoom) > width() * 0.8 ||
                          (maxY - minY) * TILE * (1 << m_zoom) > height() * 0.8))
        --m_zoom;
    update();
}

void MapWidget::requestTile(int z, int x, int y) {
    QString key = QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);
    if (m_pending.contains(key) || m_pending.size() > 32) return;
    m_pending.insert(key);
    QNetworkRequest req(QUrl(m_tileUrl.arg(z).arg(x).arg(y)));
    // OSM tile usage policy requires an identifying User-Agent
    req.setHeader(QNetworkRequest::UserAgentHeader, "dashgps/0.1 (+https://github.com/JeyKul/dashgps)");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::User, key);
    m_net->get(req);
}

void MapWidget::tileArrived(QNetworkReply *r) {
    QString key = r->request().attribute(QNetworkRequest::User).toString();
    m_pending.remove(key);
    QPixmap px;
    if (r->error() == QNetworkReply::NoError && px.loadFromData(r->readAll())) {
        m_tiles.insert(key, px);
        if (m_tiles.size() > 600) m_tiles.clear();  // crude bound, tiles stay in disk cache
        update();
    }
    r->deleteLater();
}

void MapWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0xe8, 0xe6, 0xe1));
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const int n = 1 << m_zoom;
    QPointF c = m_center * double(TILE * n);
    QPointF origin = c - QPointF(width() / 2.0, height() / 2.0);
    int x0 = int(std::floor(origin.x() / TILE)), y0 = int(std::floor(origin.y() / TILE));
    int x1 = int(std::floor((origin.x() + width()) / TILE)), y1 = int(std::floor((origin.y() + height()) / TILE));
    for (int ty = y0; ty <= y1; ++ty) {
        if (ty < 0 || ty >= n) continue;
        for (int tx = x0; tx <= x1; ++tx) {
            int wx = ((tx % n) + n) % n;
            QString key = QStringLiteral("%1/%2/%3").arg(m_zoom).arg(wx).arg(ty);
            QPointF pos(tx * TILE - origin.x(), ty * TILE - origin.y());
            auto it = m_tiles.constFind(key);
            if (it != m_tiles.constEnd()) {
                p.drawPixmap(pos, *it);
            } else {
                requestTile(m_zoom, wx, ty);
                // draw scaled parent tile while loading
                if (m_zoom > 0) {
                    QString pk = QStringLiteral("%1/%2/%3").arg(m_zoom - 1).arg(wx / 2).arg(ty / 2);
                    auto pit = m_tiles.constFind(pk);
                    if (pit != m_tiles.constEnd())
                        p.drawPixmap(QRectF(pos, QSizeF(TILE, TILE)), *pit,
                                     QRectF((wx % 2) * 128, (ty % 2) * 128, 128, 128));
                }
            }
        }
    }

    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (i == m_current) continue;
        QPainterPath path;
        bool first = true;
        for (auto &pt : m_tracks[i]) {
            QPointF s = toScreen(pt.x(), pt.y());
            if (first) path.moveTo(s); else path.lineTo(s);
            first = false;
        }
        p.setPen(QPen(QColor(90, 90, 200, 140), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }
    if (m_current >= 0 && m_current < m_tracks.size()) {
        QPainterPath path;
        bool first = true;
        for (auto &pt : m_tracks[m_current]) {
            QPointF s = toScreen(pt.x(), pt.y());
            if (first) path.moveTo(s); else path.lineTo(s);
            first = false;
        }
        p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(QColor(0x1e, 0x88, 0xe5), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    if (m_carVisible) {
        QPointF s = toScreen(m_carLat, m_carLon);
        p.save();
        p.translate(s);
        p.rotate(m_carHeading);
        QPolygonF arrow({{0, -13}, {9, 10}, {0, 5}, {-9, 10}});
        p.setPen(QPen(Qt::white, 2.5));
        p.setBrush(QColor(0xe5, 0x39, 0x35));
        p.drawPolygon(arrow);
        p.restore();
    }

    // Attribution is required by the OSM tile usage policy
    QString attr = QStringLiteral("© OpenStreetMap contributors");
    QFontMetrics fm(font());
    QRect ar(width() - fm.horizontalAdvance(attr) - 10, height() - fm.height() - 4, fm.horizontalAdvance(attr) + 10,
             fm.height() + 4);
    p.fillRect(ar, QColor(255, 255, 255, 200));
    p.setPen(Qt::black);
    p.drawText(ar, Qt::AlignCenter, attr);
}

void MapWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_dragMoved = false;
    m_dragStart = e->pos();
    m_dragCenter = m_center;
}

void MapWidget::mouseMoveEvent(QMouseEvent *e) {
    if (!m_dragging) return;
    QPoint d = e->pos() - m_dragStart;
    if (!m_dragMoved && d.manhattanLength() < 4) return;
    if (!m_dragMoved && m_follow) { m_follow = false; emit followChanged(false); }
    m_dragMoved = true;
    m_center = m_dragCenter - QPointF(d) / double(TILE * (1 << m_zoom));
    m_center.setY(qBound(0.0, m_center.y(), 1.0));
    update();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    m_dragging = false;
    if (m_dragMoved) return;
    // click: find nearest point on any track
    double best = 15 * 15;
    int bt = -1, bp = -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        for (int j = 0; j < m_tracks[i].size(); ++j) {
            QPointF d = toScreen(m_tracks[i][j].x(), m_tracks[i][j].y()) - e->position();
            double dd = d.x() * d.x() + d.y() * d.y();
            if (dd < best || (i == m_current && dd <= best)) { best = dd; bt = i; bp = j; }
        }
    if (bt >= 0) emit trackClicked(bt, bp);
}

void MapWidget::wheelEvent(QWheelEvent *e) {
    int nz = qBound(2, m_zoom + (e->angleDelta().y() > 0 ? 1 : -1), 19);
    if (nz == m_zoom) return;
    // keep the point under the cursor fixed
    QPointF off = e->position() - QPointF(width() / 2.0, height() / 2.0);
    QPointF under = m_center + off / double(TILE * (1 << m_zoom));
    m_zoom = nz;
    if (!m_follow) m_center = under - off / double(TILE * (1 << m_zoom));
    update();
}
