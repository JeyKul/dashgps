// Holds the front and rear video. With both cameras shown it picks side by
// side or one above the other, whichever gives the bigger pictures for the
// current shape of the area.
#pragma once

#include <QWidget>

class VideoArea : public QWidget {
    Q_OBJECT
public:
    enum Mode { FrontOnly, RearOnly, Both };

    VideoArea(QWidget *front, QWidget *rear, QWidget *parent = nullptr);
    void setMode(Mode m);
    void setAspect(double frontAspect, double rearAspect);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void relayout();

    QWidget *m_front, *m_rear;
    Mode m_mode = FrontOnly;
    double m_frontAspect = 16.0 / 9.0, m_rearAspect = 16.0 / 9.0;
};
