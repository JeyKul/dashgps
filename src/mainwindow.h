#pragma once

#include "gpsparser.h"

#include <QDateTime>
#include <QMainWindow>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QThread>

#include <memory>

class QAction;
class QAudioOutput;
class QComboBox;
class QLabel;
class QListWidget;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QStackedLayout;
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

protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

private:
    void buildUi();
    void openFolderDialog();
    void openFilesDialog();
    void exportDialog();
    void loadClip(int index, qint64 startMs = 0);
    void updateFromPosition(qint64 ms);
    void updateMapTracks();
    void updateViewMode();
    void startBackgroundParsing();
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
    MapWidget *m_map = nullptr;
    QListWidget *m_list = nullptr;
    QSlider *m_slider = nullptr;
    QPushButton *m_playBtn = nullptr;
    QLabel *m_timeLbl = nullptr, *m_speedLbl = nullptr, *m_infoLbl = nullptr, *m_clockLbl = nullptr;
    QComboBox *m_viewMode = nullptr, *m_rate = nullptr;
    QAction *m_followAct = nullptr, *m_mphAct = nullptr;
    bool m_sliderDown = false;
};
