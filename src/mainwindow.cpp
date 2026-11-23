#include "mainwindow.h"

#include "exporters.h"
#include "mapwidget.h"

#include <QAction>
#include <QApplication>
#include <QAudioOutput>
#include <QComboBox>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <fstream>

using dashgps::GpsPoint;
using dashgps::Track;

namespace {

const QStringList kVideoExt = {"mp4", "mov", "ts"};

QString fmtTime(qint64 ms) {
    qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

// "20261031163028_005579F.MP4" -> 2026-10-31 16:30:28 ; also handles 2026_1031_163028 etc.
QDateTime timeFromName(const QString &name) {
    QString digits;
    for (QChar c : name) {
        if (c.isDigit()) digits += c;
        else if (c != '_' && c != '-') { if (digits.size() >= 14) break; if (!digits.isEmpty() && digits.size() < 8) digits.clear(); }
        if (digits.size() == 14) break;
    }
    if (digits.size() < 14) return {};
    return QDateTime::fromString(digits.left(14), QStringLiteral("yyyyMMddHHmmss"));
}

enum class Cam { Front, Rear, Unknown };

Cam camOf(const QFileInfo &fi) {
    QString base = fi.completeBaseName();
    QString dir = fi.absolutePath().toLower();
    if (dir.contains("_r") || dir.endsWith("rear") || dir.contains("/rear")) return Cam::Rear;
    if (dir.contains("_f") || dir.endsWith("front") || dir.contains("/front")) return Cam::Front;
    if (base.endsWith('R', Qt::CaseInsensitive) || base.endsWith("_B", Qt::CaseInsensitive)) return Cam::Rear;
    if (base.endsWith('F', Qt::CaseInsensitive) || base.endsWith("_A", Qt::CaseInsensitive)) return Cam::Front;
    return Cam::Unknown;
}

QList<Clip> buildClips(const QStringList &files) {
    QList<QFileInfo> fronts, rears;
    for (auto &f : files) {
        QFileInfo fi(f);
        (camOf(fi) == Cam::Rear ? rears : fronts).append(fi);
    }
    if (fronts.isEmpty()) { fronts = rears; rears.clear(); }  // only rear files given
    auto byTime = [](const QFileInfo &a, const QFileInfo &b) {
        QDateTime ta = timeFromName(a.fileName()), tb = timeFromName(b.fileName());
        if (ta != tb) return ta < tb;
        return a.fileName() < b.fileName();
    };
    std::sort(fronts.begin(), fronts.end(), byTime);
    std::sort(rears.begin(), rears.end(), byTime);

    QList<Clip> clips;
    int r = 0;
    for (auto &fi : fronts) {
        Clip c;
        c.front = fi.absoluteFilePath();
        c.start = timeFromName(fi.fileName());
        if (!c.start.isValid()) c.start = fi.lastModified();
        // match the rear clip whose start time is closest (within 5 s)
        if (c.start.isValid()) {
            while (r < rears.size() && timeFromName(rears[r].fileName()).secsTo(c.start) > 5) ++r;
            if (r < rears.size() && qAbs(timeFromName(rears[r].fileName()).secsTo(c.start)) <= 5)
                c.rear = rears[r++].absoluteFilePath();
        }
        c.title = c.start.isValid() ? c.start.toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")) : fi.fileName();
        clips.append(c);
    }
    return clips;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("DashGPS"));
    setAcceptDrops(true);
    buildUi();
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    m_mphAct->setChecked(s.value("mph", false).toBool());
}

void MainWindow::buildUi() {
    auto *tb = addToolBar(tr("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), tr("Open folder"), this, &MainWindow::openFolderDialog);
    tb->addAction(style()->standardIcon(QStyle::SP_FileIcon), tr("Open files"), this, &MainWindow::openFilesDialog);
    tb->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Export track"), this, &MainWindow::exportDialog);
    tb->addSeparator();
    m_viewMode = new QComboBox;
    m_viewMode->addItems({tr("Front camera"), tr("Rear camera"), tr("Front + rear")});
    m_viewMode->setToolTip(tr("Which camera to show"));
    tb->addWidget(m_viewMode);
    connect(m_viewMode, &QComboBox::currentIndexChanged, this, &MainWindow::updateViewMode);
    tb->addSeparator();
    m_followAct = tb->addAction(tr("Follow car"));
    m_followAct->setCheckable(true);
    m_followAct->setChecked(true);
    tb->addAction(tr("Fit track"), this, [this] {
        m_followAct->setChecked(false);
        m_map->setFollow(false);
        m_map->fitTracks();
    });
    m_mphAct = tb->addAction(tr("mph"));
    m_mphAct->setCheckable(true);
    m_mphAct->setToolTip(tr("Show speed in miles per hour"));
    connect(m_mphAct, &QAction::toggled, this, [this] {
        QSettings().setValue("mph", m_mphAct->isChecked());
        if (m_player) updateFromPosition(m_player->position());
    });

    // clip list
    m_list = new QListWidget;
    m_list->setMinimumWidth(170);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) { loadClip(m_list->row(it)); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { loadClip(m_list->row(it)); });

    // video
    m_video = new QVideoWidget;
    m_rearVideo = new QVideoWidget;
    m_video->setMinimumSize(320, 180);
    m_rearVideo->setMinimumSize(160, 90);
    m_rearVideo->hide();
    auto *videoBox = new QWidget;
    auto *vl = new QHBoxLayout(videoBox);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(2);
    vl->addWidget(m_video, 1);
    vl->addWidget(m_rearVideo, 1);
    videoBox->setStyleSheet("background:black");

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_video);
    m_rearPlayer = new QMediaPlayer(this);
    m_rearPlayer->setVideoOutput(m_rearVideo);

    // map + info
    m_map = new MapWidget;
    connect(m_followAct, &QAction::toggled, m_map, [this](bool on) {
        m_map->setFollow(on);
        if (on) updateFromPosition(m_player->position());
    });
    connect(m_map, &MapWidget::followChanged, m_followAct, &QAction::setChecked);
    connect(m_map, &MapWidget::trackClicked, this, [this](int clip, int point) {
        auto t = trackFor(clip);
        if (!t) return;
        // point indexes the polyline, which only contains samples with a fix
        int n = -1;
        qint64 ms = -1;
        for (auto &p : t->points)
            if (p.valid && ++n == point) { ms = qint64(p.t * 1000); break; }
        if (ms < 0) return;
        if (clip == m_current) m_player->setPosition(ms);
        else loadClip(clip, ms);
    });

    m_speedLbl = new QLabel("–");
    m_speedLbl->setStyleSheet("font-size:28px;font-weight:600");
    m_clockLbl = new QLabel;
    m_clockLbl->setStyleSheet("font-size:15px");
    m_infoLbl = new QLabel;
    m_infoLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_infoLbl->setStyleSheet("color:palette(placeholder-text)");
    auto *info = new QWidget;
    auto *il = new QHBoxLayout(info);
    il->addWidget(m_speedLbl);
    il->addSpacing(16);
    auto *il2 = new QVBoxLayout;
    il2->addWidget(m_clockLbl);
    il2->addWidget(m_infoLbl);
    il->addLayout(il2, 1);

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(m_map, 1);
    rl->addWidget(info);

    auto *split = new QSplitter;
    split->addWidget(m_list);
    split->addWidget(videoBox);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({200, 800, 500});

    // transport
    auto mkBtn = [](QStyle::StandardPixmap ic, const QString &tip) {
        auto *b = new QPushButton(qApp->style()->standardIcon(ic), QString());
        b->setToolTip(tip);
        b->setFlat(true);
        return b;
    };
    auto *prev = mkBtn(QStyle::SP_MediaSkipBackward, tr("Previous clip"));
    m_playBtn = mkBtn(QStyle::SP_MediaPlay, tr("Play / pause (Space)"));
    auto *next = mkBtn(QStyle::SP_MediaSkipForward, tr("Next clip"));
    m_slider = new QSlider(Qt::Horizontal);
    m_timeLbl = new QLabel("0:00 / 0:00");
    m_rate = new QComboBox;
    m_rate->addItems({"0.5×", "1×", "2×", "4×", "8×"});
    m_rate->setCurrentIndex(1);
    m_rate->setToolTip(tr("Playback speed"));
    connect(m_rate, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const double r[] = {0.5, 1, 2, 4, 8};
        m_player->setPlaybackRate(r[i]);
        m_rearPlayer->setPlaybackRate(r[i]);
    });
    connect(prev, &QPushButton::clicked, this, [this] { if (m_current > 0) loadClip(m_current - 1); });
    connect(next, &QPushButton::clicked, this, [this] { if (m_current + 1 < m_clips.size()) loadClip(m_current + 1); });
    connect(m_playBtn, &QPushButton::clicked, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
        else m_player->play();
    });

    auto *transport = new QWidget;
    auto *tl = new QHBoxLayout(transport);
    tl->addWidget(prev);
    tl->addWidget(m_playBtn);
    tl->addWidget(next);
    tl->addWidget(m_slider, 1);
    tl->addWidget(m_timeLbl);
    tl->addWidget(m_rate);

    auto *central = new QWidget;
    auto *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(split, 1);
    cl->addWidget(transport);
    setCentralWidget(central);

    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_sliderDown = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_sliderDown = false;
        m_player->setPosition(m_slider->value());
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        m_player->setPosition(v);
        updateFromPosition(v);
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 d) {
        m_slider->setRange(0, int(d));
        m_timeLbl->setText(fmtTime(m_player->position()) + " / " + fmtTime(d));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        if (!m_sliderDown) m_slider->setValue(int(ms));
        updateFromPosition(ms);
        // keep rear camera in sync
        if (!m_rearPlayer->source().isEmpty() && qAbs(m_rearPlayer->position() - ms) > 400)
            m_rearPlayer->setPosition(ms);
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
        m_playBtn->setIcon(style()->standardIcon(st == QMediaPlayer::PlayingState ? QStyle::SP_MediaPause
                                                                                  : QStyle::SP_MediaPlay));
        if (m_rearPlayer->source().isEmpty()) return;
        if (st == QMediaPlayer::PlayingState) m_rearPlayer->play();
        else m_rearPlayer->pause();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia && m_current + 1 < m_clips.size()) {
            loadClip(m_current + 1);
            m_player->play();
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        statusBar()->showMessage(tr("Playback error: %1").arg(msg), 8000);
    });

    statusBar()->showMessage(tr("Open your dashcam's SD card folder (or drag video files here)."));
}

void MainWindow::openFolderDialog() {
    QString d = QFileDialog::getExistingDirectory(this, tr("Open dashcam folder"), QSettings().value("lastDir").toString());
    if (d.isEmpty()) return;
    QSettings().setValue("lastDir", d);
    openPaths({d});
}

void MainWindow::openFilesDialog() {
    QStringList f = QFileDialog::getOpenFileNames(this, tr("Open dashcam videos"), QSettings().value("lastDir").toString(),
                                                  tr("Videos (*.mp4 *.MP4 *.mov *.MOV *.ts *.TS)"));
    if (f.isEmpty()) return;
    QSettings().setValue("lastDir", QFileInfo(f.first()).absolutePath());
    openPaths(f);
}

void MainWindow::openPaths(const QStringList &paths) {
    QStringList files;
    for (auto &p : paths) {
        QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString f = it.next();
                if (kVideoExt.contains(QFileInfo(f).suffix().toLower())) files << f;
            }
        } else if (fi.isFile()) {
            files << fi.absoluteFilePath();
        }
    }
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("No videos"), tr("No dashcam videos found there."));
        return;
    }
    m_player->stop();
    m_rearPlayer->stop();
    {
        QMutexLocker l(&m_trackLock);
        m_tracks.clear();
        ++m_parseGeneration;
    }
    m_clips = buildClips(files);
    m_list->clear();
    for (auto &c : m_clips) {
        auto *it = new QListWidgetItem(c.start.isValid() ? c.start.toString("ddd d MMM  HH:mm:ss") : c.title);
        it->setToolTip(QFileInfo(c.front).fileName() + (c.rear.isEmpty() ? QString() : "\n" + QFileInfo(c.rear).fileName()));
        m_list->addItem(it);
    }
    statusBar()->showMessage(tr("%n clip(s) loaded", nullptr, m_clips.size()), 5000);
    startBackgroundParsing();
    loadClip(0);
}

void MainWindow::startBackgroundParsing() {
    // Parse all clips in a background thread so the map can show the whole trip.
    int gen = m_parseGeneration;
    QList<Clip> clips = m_clips;
    QThread *th = QThread::create([this, gen, clips] {
        for (int i = 0; i < clips.size(); ++i) {
            {
                QMutexLocker l(&m_trackLock);
                if (gen != m_parseGeneration) return;
                if (m_tracks.contains(i)) continue;
            }
            auto t = std::make_shared<const Track>(dashgps::parseFile(QFile::encodeName(clips[i].front).toStdString()));
            QMutexLocker l(&m_trackLock);
            if (gen != m_parseGeneration) return;
            if (!m_tracks.contains(i)) m_tracks.insert(i, t);
            if (i % 20 == 19 || i == clips.size() - 1)
                QMetaObject::invokeMethod(this, [this] { updateMapTracks(); }, Qt::QueuedConnection);
        }
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    m_parser = th;
    th->start(QThread::LowPriority);
}

std::shared_ptr<const Track> MainWindow::trackFor(int clip) {
    {
        QMutexLocker l(&m_trackLock);
        auto it = m_tracks.constFind(clip);
        if (it != m_tracks.constEnd()) return *it;
    }
    if (clip < 0 || clip >= m_clips.size()) return nullptr;
    auto t = std::make_shared<const Track>(dashgps::parseFile(QFile::encodeName(m_clips[clip].front).toStdString()));
    QMutexLocker l(&m_trackLock);
    m_tracks.insert(clip, t);
    return t;
}

void MainWindow::loadClip(int index, qint64 startMs) {
    if (index < 0 || index >= m_clips.size()) return;
    bool wasPlaying = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_current = index;
    const Clip &c = m_clips[index];
    m_list->setCurrentRow(index);
    m_player->setSource(QUrl::fromLocalFile(c.front));
    m_rearPlayer->setSource(c.rear.isEmpty() ? QUrl() : QUrl::fromLocalFile(c.rear));
    updateViewMode();
    if (startMs) m_player->setPosition(startMs);
    if (wasPlaying) m_player->play();
    else m_player->pause();  // shows the first frame

    auto t = trackFor(index);
    QString title = QFileInfo(c.front).fileName();
    if (!t || t->format.empty()) statusBar()->showMessage(tr("%1: no GPS data in this clip").arg(title), 6000);
    else if (!t->hasFix()) statusBar()->showMessage(tr("%1: camera had no GPS fix").arg(title), 6000);
    else statusBar()->clearMessage();
    setWindowTitle(QStringLiteral("DashGPS – %1").arg(c.title));
    updateMapTracks();
    if (!m_followAct->isChecked()) m_map->fitTracks();
    else if (m_map->zoom() < 10) m_map->setZoom(16);
    updateFromPosition(startMs);
}

void MainWindow::updateMapTracks() {
    if (m_current < 0) return;
    // show every clip of the same day; the current one highlighted
    QDate day = m_clips[m_current].start.date();
    QList<QList<QPointF>> lines;
    int cur = -1;
    QMutexLocker l(&m_trackLock);
    for (int i = 0; i < m_clips.size(); ++i) {
        QList<QPointF> line;
        if (m_clips[i].start.date() == day) {
            auto it = m_tracks.constFind(i);
            if (it != m_tracks.constEnd())
                for (auto &p : (*it)->points)
                    if (p.valid) line.append({p.lat, p.lon});
        }
        if (i == m_current) cur = i;
        lines.append(line);
    }
    m_map->setTracks(lines, cur);
}

QString MainWindow::fmtSpeed(double kmh) const {
    if (m_mphAct->isChecked()) return QStringLiteral("%1 mph").arg(qRound(kmh / 1.609344));
    return QStringLiteral("%1 km/h").arg(qRound(kmh));
}

void MainWindow::updateFromPosition(qint64 ms) {
    m_timeLbl->setText(fmtTime(ms) + " / " + fmtTime(m_player->duration()));
    if (m_current < 0) return;
    const Clip &c = m_clips[m_current];
    if (c.start.isValid()) m_clockLbl->setText(c.start.addMSecs(ms).toString("dddd, d MMMM yyyy   HH:mm:ss"));
    auto t = trackFor(m_current);
    GpsPoint p;
    if (t && t->at(ms / 1000.0, p)) {
        m_speedLbl->setText(fmtSpeed(p.speedKmh));
        m_infoLbl->setText(QStringLiteral("%1, %2   heading %3°")
                               .arg(p.lat, 0, 'f', 6).arg(p.lon, 0, 'f', 6).arg(qRound(p.heading)));
        m_map->setCar(p.lat, p.lon, p.heading, true);
    } else {
        m_speedLbl->setText("–");
        m_infoLbl->setText(tr("no GPS"));
        m_map->setCar(0, 0, 0, false);
    }
}

void MainWindow::updateViewMode() {
    bool hasRear = m_current >= 0 && !m_clips[m_current].rear.isEmpty();
    int mode = m_viewMode->currentIndex();
    if (!hasRear) mode = 0;
    m_video->setVisible(mode != 1);
    m_rearVideo->setVisible(mode != 0);
    if (mode == 1 && !hasRear) m_video->show();
}

void MainWindow::exportDialog() {
    if (m_clips.isEmpty()) return;
    QStringList choices = {tr("Current clip"), tr("All clips of this day"), tr("All loaded clips")};
    QMessageBox box(this);
    box.setWindowTitle(tr("Export track"));
    box.setText(tr("What do you want to export?"));
    auto *b0 = box.addButton(choices[0], QMessageBox::AcceptRole);
    auto *b1 = box.addButton(choices[1], QMessageBox::AcceptRole);
    auto *b2 = box.addButton(choices[2], QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    int scope = box.clickedButton() == b0 ? 0 : box.clickedButton() == b1 ? 1 : box.clickedButton() == b2 ? 2 : -1;
    if (scope < 0) return;

    QString sel;
    QString fn = QFileDialog::getSaveFileName(this, tr("Export track"), QSettings().value("lastExport").toString(),
                                              tr("GPX (*.gpx);;Google Earth KML (*.kml);;GeoJSON (*.geojson);;CSV (*.csv)"), &sel);
    if (fn.isEmpty()) return;
    QString fmt = sel.startsWith("GPX") ? "gpx" : sel.startsWith("Google") ? "kml" : sel.startsWith("GeoJSON") ? "geojson" : "csv";
    if (QFileInfo(fn).suffix().isEmpty()) fn += "." + fmt;
    QSettings().setValue("lastExport", QFileInfo(fn).absolutePath());

    std::vector<dashgps::NamedTrack> tracks;
    QDate day = m_clips[m_current].start.date();
    for (int i = 0; i < m_clips.size(); ++i) {
        if (scope == 0 && i != m_current) continue;
        if (scope == 1 && m_clips[i].start.date() != day) continue;
        auto t = trackFor(i);
        if (t && t->hasFix()) tracks.push_back({QFileInfo(m_clips[i].front).fileName().toStdString(), *t});
    }
    std::ofstream o(QFile::encodeName(fn).toStdString(), std::ios::binary);
    dashgps::writeAny(fmt.toStdString(), o, tracks);
    statusBar()->showMessage(o ? tr("Exported %n clip(s) to %1", nullptr, int(tracks.size())).arg(fn)
                               : tr("Could not write %1").arg(fn), 6000);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e) {
    QStringList paths;
    for (auto &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) openPaths(paths);
}

void MainWindow::keyPressEvent(QKeyEvent *e) {
    switch (e->key()) {
    case Qt::Key_Space: m_playBtn->click(); return;
    case Qt::Key_Left: m_player->setPosition(qMax<qint64>(0, m_player->position() - 5000)); return;
    case Qt::Key_Right: m_player->setPosition(m_player->position() + 5000); return;
    case Qt::Key_PageUp: if (m_current > 0) loadClip(m_current - 1); return;
    case Qt::Key_PageDown: if (m_current + 1 < m_clips.size()) loadClip(m_current + 1); return;
    default: QMainWindow::keyPressEvent(e);
    }
}

void MainWindow::closeEvent(QCloseEvent *e) {
    {
        QMutexLocker l(&m_trackLock);
        ++m_parseGeneration;  // stop background parsing
    }
    if (m_parser) m_parser->wait();
    QSettings().setValue("geometry", saveGeometry());
    QMainWindow::closeEvent(e);
}
