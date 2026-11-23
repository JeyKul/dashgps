#pragma once

#include "gpsparser.h"

#include <QDateTime>
#include <QHash>
#include <QMainWindow>
#include <QMutex>
#include <QPointer>
#include <QThread>

#include <memory>

class QAction;
class QActionGroup;
class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QSlider;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QVideoWidget;
class MapWidget;

struct Clip {
    QString front;       // main video (front camera, or the only one)
    QString rear;        // optional matching rear video
    QDateTime start;     // from filename, local time of camera
    QString title;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    void openPaths(const QStringList &paths);
    void play();

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void closeEvent(QCloseEvent *) override;
    void changeEvent(QEvent *) override;

private:
    void buildActions();
    void buildUi();
    QWidget *buildEmptyPage();
    QWidget *buildTransport();
    QWidget *buildInfoPanel();
    void openFolderDialog();
    void openFilesDialog();
    void exportDialog();
    void loadClip(int index, qint64 startMs = 0);
    void updateFromPosition(qint64 ms);
    void updateMapTracks();
    void updateViewMode();
    void updateVolumeIcon();
    void applyTextSize(int percent);
    void applyMapTheme();
    void startBackgroundParsing();
    void seekBy(qint64 deltaMs);
    void setRate(double r);
    std::shared_ptr<const dashgps::Track> trackFor(int clip);
    QString fmtSpeed(double kmh) const;

    QList<Clip> m_clips;
    int m_current = -1;
    QHash<int, std::shared_ptr<const dashgps::Track>> m_tracks;
    QMutex m_trackLock;
    int m_parseGeneration = 0;
    QPointer<QThread> m_parser;

    QMediaPlayer *m_player = nullptr, *m_rearPlayer = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoWidget *m_video = nullptr, *m_rearVideo = nullptr;
    QStackedWidget *m_centerStack = nullptr;
    MapWidget *m_map = nullptr;
    QTreeWidget *m_list = nullptr;
    QSlider *m_slider = nullptr, *m_volume = nullptr;
    QToolButton *m_playBtn = nullptr, *m_muteBtn = nullptr, *m_rateBtn = nullptr;
    QLabel *m_timeLbl = nullptr, *m_speedLbl = nullptr, *m_unitLbl = nullptr, *m_infoLbl = nullptr,
           *m_clockLbl = nullptr, *m_dateLbl = nullptr;
    QWidget *m_transport = nullptr;

    QAction *m_actOpenFolder = nullptr, *m_actOpenFiles = nullptr, *m_actExport = nullptr;
    QAction *m_actFollow = nullptr, *m_actFit = nullptr, *m_actMph = nullptr;
    QAction *m_actPlay = nullptr, *m_actMute = nullptr;
    QActionGroup *m_camGroup = nullptr, *m_themeGroup = nullptr, *m_sizeGroup = nullptr;
    double m_rate = 1.0;
    bool m_sliderDown = false;
    qreal m_baseFontPt = 0;
};
