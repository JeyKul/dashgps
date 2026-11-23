#include "videoarea.h"

#include <QResizeEvent>

namespace {

// visible picture area when a video of the given aspect is fitted into w x h
double fittedArea(double w, double h, double aspect) {
    if (w <= 0 || h <= 0) return 0;
    double vw = qMin(w, h * aspect);
    return vw * (vw / aspect);
}

} // namespace

VideoArea::VideoArea(QWidget *front, QWidget *rear, QWidget *parent)
    : QWidget(parent), m_front(front), m_rear(rear) {
    m_front->setParent(this);
    m_rear->setParent(this);
    setAttribute(Qt::WA_StyledBackground);
    setStyleSheet("VideoArea { background: black; }");
    setMinimumSize(320, 180);
}

void VideoArea::setMode(Mode m) {
    m_mode = m;
    relayout();
}

void VideoArea::setAspect(double frontAspect, double rearAspect) {
    if (frontAspect > 0.1) m_frontAspect = frontAspect;
    if (rearAspect > 0.1) m_rearAspect = rearAspect;
    relayout();
}

void VideoArea::resizeEvent(QResizeEvent *) {
    relayout();
}

void VideoArea::relayout() {
    const int gap = 2;
    QRect r = rect();
    m_front->setVisible(m_mode != RearOnly);
    m_rear->setVisible(m_mode != FrontOnly);
    if (m_mode == FrontOnly) { m_front->setGeometry(r); return; }
    if (m_mode == RearOnly) { m_rear->setGeometry(r); return; }

    double w = r.width(), h = r.height();
    double side = fittedArea((w - gap) / 2, h, m_frontAspect) + fittedArea((w - gap) / 2, h, m_rearAspect);
    double stack = fittedArea(w, (h - gap) / 2, m_frontAspect) + fittedArea(w, (h - gap) / 2, m_rearAspect);
    if (side >= stack) {
        int half = (r.width() - gap) / 2;
        m_front->setGeometry(r.x(), r.y(), half, r.height());
        m_rear->setGeometry(r.x() + half + gap, r.y(), r.width() - half - gap, r.height());
    } else {
        int half = (r.height() - gap) / 2;
        m_front->setGeometry(r.x(), r.y(), r.width(), half);
        m_rear->setGeometry(r.x(), r.y() + half + gap, r.width(), r.height() - half - gap);
    }
}
