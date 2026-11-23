#include "icons.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>

namespace {

void paintIcon(QPainter &p, Icon which, const QColor &c) {
    // drawn on a 24x24 grid
    QPen pen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto fill = [&](const QPolygonF &poly) {
        p.setBrush(c);
        p.drawPolygon(poly);
        p.setBrush(Qt::NoBrush);
    };
    switch (which) {
    case Icon::Play: fill({{7, 4}, {20, 12}, {7, 20}}); break;
    case Icon::Pause:
        p.setBrush(c);
        p.drawRoundedRect(QRectF(6, 4, 4, 16), 1, 1);
        p.drawRoundedRect(QRectF(14, 4, 4, 16), 1, 1);
        break;
    case Icon::Prev:
        fill({{19, 5}, {9, 12}, {19, 19}});
        p.drawLine(QPointF(6, 5), QPointF(6, 19));
        break;
    case Icon::Next:
        fill({{5, 5}, {15, 12}, {5, 19}});
        p.drawLine(QPointF(18, 5), QPointF(18, 19));
        break;
    case Icon::Back:
        fill({{12, 6}, {4, 12}, {12, 18}});
        fill({{20, 6}, {12, 12}, {20, 18}});
        break;
    case Icon::Forward:
        fill({{4, 6}, {12, 12}, {4, 18}});
        fill({{12, 6}, {20, 12}, {12, 18}});
        break;
    case Icon::Volume:
    case Icon::Muted:
        fill({{3, 9}, {7, 9}, {12, 5}, {12, 19}, {7, 15}, {3, 15}});
        if (which == Icon::Volume) {
            p.drawArc(QRectF(10, 8, 6, 8), -60 * 16, 120 * 16);
            p.drawArc(QRectF(10, 4, 11, 16), -60 * 16, 120 * 16);
        } else {
            p.drawLine(QPointF(15, 9), QPointF(21, 15));
            p.drawLine(QPointF(21, 9), QPointF(15, 15));
        }
        break;
    case Icon::Folder: {
        QPainterPath path;
        path.moveTo(3, 7); path.lineTo(3, 19); path.lineTo(21, 19); path.lineTo(21, 9);
        path.lineTo(11, 9); path.lineTo(9, 6); path.lineTo(3, 6); path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::File: {
        QPainterPath path;
        path.moveTo(6, 3); path.lineTo(14, 3); path.lineTo(19, 8); path.lineTo(19, 21);
        path.lineTo(6, 21); path.closeSubpath();
        p.drawPath(path);
        fill({{10, 10}, {16, 14}, {10, 18}});
        break;
    }
    case Icon::Export:
        p.drawLine(QPointF(12, 3), QPointF(12, 15));
        p.drawPolyline(QPolygonF({{7, 8}, {12, 3}, {17, 8}}));
        p.drawPolyline(QPolygonF({{4, 14}, {4, 20}, {20, 20}, {20, 14}}));
        break;
    case Icon::Follow:
        p.drawEllipse(QPointF(12, 12), 7, 7);
        p.drawLine(QPointF(12, 2), QPointF(12, 5));
        p.drawLine(QPointF(12, 19), QPointF(12, 22));
        p.drawLine(QPointF(2, 12), QPointF(5, 12));
        p.drawLine(QPointF(19, 12), QPointF(22, 12));
        p.setBrush(c);
        p.drawEllipse(QPointF(12, 12), 2.5, 2.5);
        break;
    case Icon::Fit:
        p.drawPolyline(QPolygonF({{3, 8}, {3, 3}, {8, 3}}));
        p.drawPolyline(QPolygonF({{16, 3}, {21, 3}, {21, 8}}));
        p.drawPolyline(QPolygonF({{21, 16}, {21, 21}, {16, 21}}));
        p.drawPolyline(QPolygonF({{8, 21}, {3, 21}, {3, 16}}));
        p.drawPolyline(QPolygonF({{7, 16}, {10, 10}, {14, 13}, {17, 8}}));
        break;
    case Icon::Settings:
        for (int i = 0; i < 3; ++i) {
            double y = 6 + i * 6;
            p.drawLine(QPointF(3, y), QPointF(21, y));
            p.setBrush(c);
            p.drawEllipse(QPointF(i == 1 ? 15 : 8, y), 2.5, 2.5);
            p.setBrush(Qt::NoBrush);
        }
        break;
    case Icon::Save:
        p.drawLine(QPointF(12, 3), QPointF(12, 15));
        p.drawPolyline(QPolygonF({{7, 10}, {12, 15}, {17, 10}}));
        p.drawPolyline(QPolygonF({{4, 14}, {4, 20}, {20, 20}, {20, 14}}));
        break;
    case Icon::Speed:
        p.drawArc(QRectF(3, 5, 18, 18), 0, 180 * 16);
        p.drawLine(QPointF(12, 14), QPointF(17, 8));
        break;
    }
}

class PaintedIconEngine : public QIconEngine {
public:
    PaintedIconEngine(Icon w, QColor c) : m_which(w), m_color(c) {}
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        QColor c = m_color;
        if (mode == QIcon::Disabled) c.setAlphaF(0.35);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->translate(rect.topLeft());
        p->scale(rect.width() / 24.0, rect.height() / 24.0);
        paintIcon(*p, m_which, c);
        p->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(), size), mode, state);
        return pm;
    }
    QIconEngine *clone() const override { return new PaintedIconEngine(m_which, m_color); }

private:
    Icon m_which;
    QColor m_color;
};

} // namespace

QIcon makeIcon(Icon which, const QColor &color) {
    return QIcon(new PaintedIconEngine(which, color));
}
