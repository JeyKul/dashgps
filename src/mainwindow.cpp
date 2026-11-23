#include "mainwindow.h"

#include "exporters.h"
#include "icons.h"
#include "mapwidget.h"
#include "stereoaudio.h"
#include "videoarea.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDirIterator>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleHints>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWindow>

#include <fstream>

using dashgps::GpsPoint;
using dashgps::Track;

namespace {

const QStringList kVideoExt = {"mp4", "mov", "ts"};
const double kRates[] = {0.5, 1, 2, 4, 8};
const int kTextSizes[] = {100, 125, 150};

QString fmtTime(qint64 ms) {
    qint64 s = qMax<qint64>(0, ms) / 1000;
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
        c.title = c.start.isValid() ? QLocale().toString(c.start, QLocale::ShortFormat) : fi.fileName();
        clips.append(c);
    }
    return clips;
}

// Time with seconds, 12 or 24 hours like the user's locale
QString fmtClock(const QTime &t) {
    QString f = QLocale().timeFormat(QLocale::ShortFormat);
    bool ampm = f.contains("AP", Qt::CaseInsensitive);
    return QLocale().toString(t, ampm ? QStringLiteral("h:mm:ss AP") : QStringLiteral("HH:mm:ss"));
}

bool isDark() {
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("DashGPS"));
    setAcceptDrops(true);
    m_baseFontPt = QApplication::font().pointSizeF();
    buildActions();
    buildUi();

    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    m_actMph->setChecked(s.value("mph", false).toBool());
    m_audio->setVolume(s.value("volume", 0.8).toFloat());
    m_volume->setValue(qRound(m_audio->volume() * 100));
    m_audio->setMuted(s.value("muted", false).toBool());
    m_actMute->setChecked(m_audio->isMuted());
    int theme = s.value("mapTheme", 0).toInt();
    m_themeGroup->actions().value(theme, m_themeGroup->actions().first())->setChecked(true);
    int size = s.value("textSize", 100).toInt();
    for (auto *a : m_sizeGroup->actions())
        if (a->data().toInt() == size) a->setChecked(true);
    applyTextSize(size);
    applyMapTheme();
    updateVolumeIcon();

    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        applyMapTheme();
        applyTextSize(m_sizeGroup->checkedAction()->data().toInt());  // also refreshes icons
    });

    // Drag & drop must work anywhere in the window, including on the video
    // surface (a native child window) and the clip list, so filter app-wide.
    qApp->installEventFilter(this);
}

void MainWindow::buildActions() {
    m_actOpenFolder = new QAction(tr("Open &folder…"), this);
    m_actOpenFolder->setShortcut(QKeySequence::Open);
    m_actOpenFolder->setToolTip(tr("Open a folder or SD card with dashcam videos (Ctrl+O)"));
    connect(m_actOpenFolder, &QAction::triggered, this, &MainWindow::openFolderDialog);

    m_actOpenFiles = new QAction(tr("Open f&iles…"), this);
    m_actOpenFiles->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    m_actOpenFiles->setToolTip(tr("Open single video files (Ctrl+Shift+O)"));
    connect(m_actOpenFiles, &QAction::triggered, this, &MainWindow::openFilesDialog);

    m_actExport = new QAction(tr("&Export route…"), this);
    m_actExport->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_actExport->setToolTip(tr("Save the route as GPX, KML, GeoJSON or CSV (Ctrl+E)"));
    m_actExport->setEnabled(false);
    connect(m_actExport, &QAction::triggered, this, &MainWindow::exportDialog);

    m_actPlay = new QAction(tr("&Play"), this);
    m_actPlay->setShortcuts({QKeySequence(Qt::Key_Space), QKeySequence(Qt::Key_K), QKeySequence(Qt::Key_MediaTogglePlayPause)});
    m_actPlay->setToolTip(tr("Play / pause (Space)"));
    connect(m_actPlay, &QAction::triggered, this, [this] {
        if (m_player->playbackState() == QMediaPlayer::PlayingState) m_player->pause();
        else play();
    });

    m_actMute = new QAction(tr("&Mute"), this);
    m_actMute->setCheckable(true);
    m_actMute->setShortcut(QKeySequence(Qt::Key_M));
    m_actMute->setToolTip(tr("Mute / unmute sound (M)"));
    connect(m_actMute, &QAction::toggled, this, [this](bool on) {
        m_audio->setMuted(on);
        QSettings().setValue("muted", on);
        updateVolumeIcon();
    });

    m_actFollow = new QAction(tr("&Follow car"), this);
    m_actFollow->setCheckable(true);
    m_actFollow->setChecked(true);
    m_actFollow->setShortcut(QKeySequence(Qt::Key_F));
    m_actFollow->setToolTip(tr("Keep the car in the middle of the map (F)"));

    m_actFit = new QAction(tr("Show &whole route"), this);
    m_actFit->setShortcut(QKeySequence(Qt::Key_W));
    m_actFit->setToolTip(tr("Zoom the map to the whole route of this clip (W)"));

    m_actMph = new QAction(tr("Speed in &mph"), this);
    m_actMph->setCheckable(true);
    connect(m_actMph, &QAction::toggled, this, [this] {
        QSettings().setValue("mph", m_actMph->isChecked());
        updateFromPosition(m_player ? m_player->position() : 0);
    });

    m_camGroup = new QActionGroup(this);
    const QStringList camNames = {tr("Front camera"), tr("Rear camera"), tr("Both cameras")};
    for (int i = 0; i < 3; ++i) {
        auto *a = new QAction(camNames[i], m_camGroup);
        a->setCheckable(true);
        a->setChecked(i == 0);
        a->setShortcut(QKeySequence(Qt::Key_1 + i));
        a->setData(i);
        a->setToolTip(camNames[i] + QStringLiteral(" (%1)").arg(i + 1));
    }
    connect(m_camGroup, &QActionGroup::triggered, this, &MainWindow::updateViewMode);

    m_themeGroup = new QActionGroup(this);
    const QStringList themes = {tr("Automatic (like system)"), tr("Light map"), tr("Dark map")};
    for (int i = 0; i < 3; ++i) {
        auto *a = new QAction(themes[i], m_themeGroup);
        a->setCheckable(true);
        a->setData(i);
    }
    connect(m_themeGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        QSettings().setValue("mapTheme", a->data());
        applyMapTheme();
    });

    m_sizeGroup = new QActionGroup(this);
    const QStringList sizes = {tr("Normal text"), tr("Large text"), tr("Extra large text")};
    for (int i = 0; i < 3; ++i) {
        auto *a = new QAction(sizes[i], m_sizeGroup);
        a->setCheckable(true);
        a->setData(kTextSizes[i]);
    }
    connect(m_sizeGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        QSettings().setValue("textSize", a->data());
        applyTextSize(a->data().toInt());
    });
}

void MainWindow::buildUi() {
    // ---- menus (keyboard accessible via Alt) ----
    auto *mFile = menuBar()->addMenu(tr("&File"));
    mFile->addAction(m_actOpenFolder);
    mFile->addAction(m_actOpenFiles);
    mFile->addSeparator();
    mFile->addAction(m_actExport);
    mFile->addSeparator();
    mFile->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto *mPlay = menuBar()->addMenu(tr("&Playback"));
    mPlay->addAction(m_actPlay);
    mPlay->addAction(tr("Back 5 seconds"), QKeySequence(Qt::Key_Left), this, [this] { seekBy(-5000); });
    mPlay->addAction(tr("Forward 5 seconds"), QKeySequence(Qt::Key_Right), this, [this] { seekBy(5000); });
    mPlay->addAction(tr("Back 1 minute"), QKeySequence(Qt::SHIFT | Qt::Key_Left), this, [this] { seekBy(-60000); });
    mPlay->addAction(tr("Forward 1 minute"), QKeySequence(Qt::SHIFT | Qt::Key_Right), this, [this] { seekBy(60000); });
    mPlay->addSeparator();
    mPlay->addAction(tr("Previous clip"), QKeySequence(Qt::Key_PageUp), this, [this] { loadClip(m_current - 1); });
    mPlay->addAction(tr("Next clip"), QKeySequence(Qt::Key_PageDown), this, [this] { loadClip(m_current + 1); });
    mPlay->addSeparator();
    mPlay->addAction(m_actMute);
    mPlay->addAction(tr("Louder"), QKeySequence(Qt::Key_Up), this, [this] { m_volume->setValue(m_volume->value() + 10); });
    mPlay->addAction(tr("Quieter"), QKeySequence(Qt::Key_Down), this, [this] { m_volume->setValue(m_volume->value() - 10); });
    mPlay->addSeparator();
    mPlay->addAction(tr("Faster"), QKeySequence(Qt::Key_BracketRight), this, [this] {
        for (double r : kRates) if (r > m_rate) { setRate(r); break; }
    });
    mPlay->addAction(tr("Slower"), QKeySequence(Qt::Key_BracketLeft), this, [this] {
        for (int i = 4; i >= 0; --i) if (kRates[i] < m_rate) { setRate(kRates[i]); break; }
    });
    mPlay->addAction(tr("Normal speed"), QKeySequence(Qt::Key_Backspace), this, [this] { setRate(1); });

    auto *mView = menuBar()->addMenu(tr("&View"));
    mView->addActions(m_camGroup->actions());
    mView->addSeparator();
    mView->addAction(m_actFollow);
    mView->addAction(m_actFit);
    mView->addAction(tr("Zoom map in"), QKeySequence(Qt::Key_Plus), this, [this] { m_map->setZoom(m_map->zoom() + 1); });
    mView->addAction(tr("Zoom map out"), QKeySequence(Qt::Key_Minus), this, [this] { m_map->setZoom(m_map->zoom() - 1); });
    mView->addSeparator();
    mView->addActions(m_themeGroup->actions());
    mView->addSeparator();
    mView->addActions(m_sizeGroup->actions());
    mView->addSeparator();
    mView->addAction(m_actMph);
    mView->addAction(tr("Full screen"), QKeySequence(Qt::Key_F11), this, [this] {
        isFullScreen() ? showNormal() : showFullScreen();
    });

    auto *mHelp = menuBar()->addMenu(tr("&Help"));
    mHelp->addAction(tr("&Keyboard shortcuts"), QKeySequence::HelpContents, this, [this] {
        QMessageBox::information(this, tr("Keyboard shortcuts"), tr(
            "<table cellpadding=4>"
            "<tr><td><b>Space</b></td><td>Play / pause</td></tr>"
            "<tr><td><b>← / →</b></td><td>5 seconds back / forward</td></tr>"
            "<tr><td><b>Shift + ← / →</b></td><td>1 minute back / forward</td></tr>"
            "<tr><td><b>Page Up / Down</b></td><td>Previous / next clip</td></tr>"
            "<tr><td><b>↑ / ↓</b></td><td>Louder / quieter</td></tr>"
            "<tr><td><b>M</b></td><td>Mute</td></tr>"
            "<tr><td><b>[ / ]</b></td><td>Slower / faster</td></tr>"
            "<tr><td><b>1 / 2 / 3</b></td><td>Front / rear / both cameras</td></tr>"
            "<tr><td><b>F</b></td><td>Follow car on map</td></tr>"
            "<tr><td><b>W</b></td><td>Show whole route</td></tr>"
            "<tr><td><b>+ / −</b></td><td>Zoom map</td></tr>"
            "<tr><td><b>F11</b></td><td>Full screen</td></tr>"
            "<tr><td><b>Ctrl + O</b></td><td>Open folder</td></tr>"
            "<tr><td><b>Ctrl + E</b></td><td>Export route</td></tr>"
            "</table>"));
    });
    mHelp->addAction(tr("&About DashGPS"), this, [this] {
        QMessageBox::about(this, tr("About DashGPS"), tr(
            "<h3>DashGPS %1</h3><p>Plays dashcam videos together with their GPS route.</p>"
            "<p>Everything stays on your computer. Map data © OpenStreetMap contributors.</p>"
            "<p><a href='https://github.com/JeyKul/dashgps'>github.com/JeyKul/dashgps</a></p>")
            .arg(QApplication::applicationVersion()));
    });

    // ---- toolbar: big, labelled buttons ----
    auto *tb = addToolBar(tr("Main toolbar"));
    tb->setObjectName("main");
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->addAction(m_actOpenFolder);
    tb->addAction(m_actExport);
    tb->addSeparator();
    tb->addActions(m_camGroup->actions());
    tb->addSeparator();
    tb->addAction(m_actFollow);
    tb->addAction(m_actFit);
    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    auto *settingsBtn = new QToolButton;
    settingsBtn->setText(tr("Settings"));
    settingsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    settingsBtn->setPopupMode(QToolButton::InstantPopup);
    settingsBtn->setAccessibleName(tr("Settings"));
    settingsBtn->setObjectName("settingsBtn");
    auto *sm = new QMenu(settingsBtn);
    sm->addSection(tr("Map"));
    sm->addActions(m_themeGroup->actions());
    sm->addSection(tr("Text size"));
    sm->addActions(m_sizeGroup->actions());
    sm->addSection(tr("Units"));
    sm->addAction(m_actMph);
    settingsBtn->setMenu(sm);
    tb->addWidget(settingsBtn);

    // ---- clip list, grouped by day ----
    m_list = new QTreeWidget;
    m_list->setHeaderHidden(true);
    m_list->setRootIsDecorated(true);
    m_list->setUniformRowHeights(true);
    m_list->setMinimumWidth(200);
    m_list->setAccessibleName(tr("Video clips"));
    m_list->setAccessibleDescription(tr("All clips, grouped by day. Press Enter to play a clip."));
    m_list->setStyleSheet("QTreeWidget::item { padding: 5px 2px; }");
    auto pick = [this](QTreeWidgetItem *it) {
        QVariant v = it->data(0, Qt::UserRole);
        if (v.isValid() && v.toInt() != m_current) loadClip(v.toInt());
    };
    connect(m_list, &QTreeWidget::itemActivated, this, pick);
    connect(m_list, &QTreeWidget::itemClicked, this, pick);

    // ---- video ----
    m_video = new QVideoWidget;
    m_rearVideo = new QVideoWidget;
    m_video->setAccessibleName(tr("Front camera video"));
    m_rearVideo->setAccessibleName(tr("Rear camera video"));
    m_videoArea = new VideoArea(m_video, m_rearVideo);

    m_centerStack = new QStackedWidget;
    m_centerStack->addWidget(buildEmptyPage());
    m_centerStack->addWidget(m_videoArea);

    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(m_video);
    m_audio = new StereoAudio(m_player, this);
    m_rearPlayer = new QMediaPlayer(this);  // no audio: it would just echo the front camera
    m_rearPlayer->setVideoOutput(m_rearVideo);
    auto aspectOf = [](QMediaPlayer *p) {
        QSize s = p->metaData().value(QMediaMetaData::Resolution).toSize();
        return s.height() > 0 ? double(s.width()) / s.height() : 0.0;
    };
    auto updateAspect = [this, aspectOf] { m_videoArea->setAspect(aspectOf(m_player), aspectOf(m_rearPlayer)); };
    connect(m_player, &QMediaPlayer::metaDataChanged, this, updateAspect);
    connect(m_rearPlayer, &QMediaPlayer::metaDataChanged, this, updateAspect);

    // ---- map + info ----
    m_map = new MapWidget;
    m_map->setAccessibleName(tr("Map"));
    m_map->setAccessibleDescription(tr("Map with the driven route. Click the route to jump to that moment."));
    connect(m_actFollow, &QAction::toggled, m_map, [this](bool on) {
        m_map->setFollow(on);
        if (on) updateFromPosition(m_player->position());
    });
    connect(m_actFit, &QAction::triggered, this, [this] {
        m_actFollow->setChecked(false);
        m_map->fitTracks();
    });
    connect(m_map, &MapWidget::followChanged, m_actFollow, &QAction::setChecked);
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

    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(0);
    rl->addWidget(m_map, 1);
    rl->addWidget(buildInfoPanel());

    auto *split = new QSplitter;
    split->setChildrenCollapsible(false);
    split->addWidget(m_list);
    split->addWidget(m_centerStack);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({230, 800, 500});

    auto *central = new QWidget;
    auto *cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    cl->addWidget(split, 1);
    m_transport = buildTransport();
    cl->addWidget(m_transport);
    setCentralWidget(central);

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
        bool playing = st == QMediaPlayer::PlayingState;
        m_actPlay->setText(playing ? tr("&Pause") : tr("&Play"));
        m_playBtn->setAccessibleName(playing ? tr("Pause") : tr("Play"));
        m_playBtn->setIcon(makeIcon(playing ? Icon::Pause : Icon::Play, palette().color(QPalette::ButtonText)));
        if (m_rearPlayer->source().isEmpty()) return;
        if (playing) m_rearPlayer->play();
        else m_rearPlayer->pause();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia && m_current + 1 < m_clips.size()) {
            loadClip(m_current + 1);
            play();
        }
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        statusBar()->showMessage(tr("Cannot play this video: %1").arg(msg), 10000);
    });

    m_transport->setEnabled(false);
    statusBar()->showMessage(tr("Ready"));
}

QWidget *MainWindow::buildEmptyPage() {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addStretch(2);
    auto *title = new QLabel(tr("Watch your dashcam videos with a map"));
    title->setAlignment(Qt::AlignCenter);
    title->setWordWrap(true);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.6);
    f.setBold(true);
    title->setFont(f);
    title->setObjectName("emptyTitle");
    l->addWidget(title);
    l->addSpacing(8);
    auto *sub = new QLabel(tr("Put the dashcam's memory card into your computer, then open it here.\n"
                              "You can also drag the card, a folder or video files into this window."));
    sub->setAlignment(Qt::AlignCenter);
    sub->setWordWrap(true);
    l->addWidget(sub);
    l->addSpacing(20);
    auto *btn = new QPushButton(tr("Open dashcam folder…"));
    btn->setObjectName("bigOpen");
    btn->setDefault(true);
    btn->setMinimumHeight(56);
    btn->setMinimumWidth(280);
    btn->setAccessibleDescription(tr("Choose the memory card or folder that contains your dashcam videos"));
    connect(btn, &QPushButton::clicked, this, &MainWindow::openFolderDialog);
    auto *btn2 = new QPushButton(tr("Open single files…"));
    btn2->setMinimumHeight(40);
    connect(btn2, &QPushButton::clicked, this, &MainWindow::openFilesDialog);
    auto *bl = new QVBoxLayout;
    bl->setAlignment(Qt::AlignHCenter);
    bl->addWidget(btn, 0, Qt::AlignHCenter);
    bl->addWidget(btn2, 0, Qt::AlignHCenter);
    l->addLayout(bl);
    l->addStretch(3);
    return w;
}

QWidget *MainWindow::buildInfoPanel() {
    auto *info = new QWidget;
    info->setObjectName("infoPanel");
    info->setAttribute(Qt::WA_StyledBackground);
    auto *il = new QHBoxLayout(info);
    il->setContentsMargins(14, 10, 14, 10);

    m_speedLbl = new QLabel("–");
    m_speedLbl->setObjectName("speed");
    m_speedLbl->setAccessibleName(tr("Speed"));
    m_unitLbl = new QLabel;
    m_unitLbl->setObjectName("unit");
    auto *sl = new QVBoxLayout;
    sl->setSpacing(0);
    sl->addWidget(m_speedLbl, 0, Qt::AlignLeft | Qt::AlignBottom);
    sl->addWidget(m_unitLbl, 0, Qt::AlignLeft | Qt::AlignTop);
    il->addLayout(sl);
    il->addSpacing(20);

    m_clockLbl = new QLabel;
    m_clockLbl->setObjectName("clock");
    m_clockLbl->setAccessibleName(tr("Time of recording"));
    m_dateLbl = new QLabel;
    m_dateLbl->setAccessibleName(tr("Date of recording"));
    m_infoLbl = new QLabel;
    m_infoLbl->setObjectName("coords");
    m_infoLbl->setAccessibleName(tr("Position"));
    m_infoLbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    auto *tl = new QVBoxLayout;
    tl->setSpacing(2);
    tl->addWidget(m_clockLbl);
    tl->addWidget(m_dateLbl);
    tl->addWidget(m_infoLbl);
    il->addLayout(tl, 1);
    return info;
}

QWidget *MainWindow::buildTransport() {
    auto *w = new QWidget;
    w->setObjectName("transport");
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(10, 6, 10, 6);
    l->setSpacing(6);

    auto mk = [](const QString &name, const QString &tip) {
        auto *b = new QToolButton;
        b->setAccessibleName(name);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(28, 28));
        b->setFocusPolicy(Qt::StrongFocus);
        return b;
    };
    auto *prev = mk(tr("Previous clip"), tr("Previous clip (Page Up)"));
    prev->setProperty("dgIcon", int(Icon::Prev));
    connect(prev, &QToolButton::clicked, this, [this] { loadClip(m_current - 1); });
    auto *back = mk(tr("Back 5 seconds"), tr("Back 5 seconds (←)"));
    back->setProperty("dgIcon", int(Icon::Back));
    connect(back, &QToolButton::clicked, this, [this] { seekBy(-5000); });
    m_playBtn = mk(tr("Play"), tr("Play / pause (Space)"));
    m_playBtn->setIconSize(QSize(40, 40));
    m_playBtn->setObjectName("play");
    connect(m_playBtn, &QToolButton::clicked, m_actPlay, &QAction::trigger);
    auto *fwd = mk(tr("Forward 5 seconds"), tr("Forward 5 seconds (→)"));
    fwd->setProperty("dgIcon", int(Icon::Forward));
    connect(fwd, &QToolButton::clicked, this, [this] { seekBy(5000); });
    auto *next = mk(tr("Next clip"), tr("Next clip (Page Down)"));
    next->setProperty("dgIcon", int(Icon::Next));
    connect(next, &QToolButton::clicked, this, [this] { loadClip(m_current + 1); });

    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setAccessibleName(tr("Position in clip"));
    m_slider->setMinimumHeight(28);
    m_slider->setPageStep(10000);
    m_slider->setSingleStep(1000);
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_sliderDown = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_sliderDown = false;
        m_player->setPosition(m_slider->value());
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) {
        m_player->setPosition(v);
        updateFromPosition(v);
    });
    connect(m_slider, &QSlider::actionTriggered, this, [this](int action) {
        // keyboard / page clicks on the slider
        if (action != QAbstractSlider::SliderMove) m_player->setPosition(m_slider->sliderPosition());
    });

    m_timeLbl = new QLabel("0:00 / 0:00");
    m_timeLbl->setObjectName("time");
    m_timeLbl->setAccessibleName(tr("Playback time"));

    m_muteBtn = mk(tr("Mute"), tr("Mute / unmute (M)"));
    m_muteBtn->setCheckable(true);
    connect(m_muteBtn, &QToolButton::clicked, m_actMute, &QAction::trigger);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setFixedWidth(110);
    m_volume->setAccessibleName(tr("Volume"));
    m_volume->setToolTip(tr("Volume (↑ / ↓)"));
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_audio->setVolume(v / 100.0f);
        QSettings().setValue("volume", v / 100.0);
        if (v > 0 && m_audio->isMuted()) m_actMute->setChecked(false);
        updateVolumeIcon();
    });

    m_rateBtn = new QToolButton;
    m_rateBtn->setText("1×");
    m_rateBtn->setAccessibleName(tr("Playback speed"));
    m_rateBtn->setToolTip(tr("Playback speed ([ slower, ] faster)"));
    m_rateBtn->setPopupMode(QToolButton::InstantPopup);
    m_rateBtn->setMinimumWidth(56);
    auto *rm = new QMenu(m_rateBtn);
    for (double r : kRates)
        rm->addAction(QStringLiteral("%1×").arg(r), this, [this, r] { setRate(r); });
    m_rateBtn->setMenu(rm);

    l->addWidget(prev);
    l->addWidget(back);
    l->addWidget(m_playBtn);
    l->addWidget(fwd);
    l->addWidget(next);
    l->addSpacing(6);
    l->addWidget(m_slider, 1);
    l->addSpacing(6);
    l->addWidget(m_timeLbl);
    l->addSpacing(12);
    l->addWidget(m_muteBtn);
    l->addWidget(m_volume);
    l->addSpacing(8);
    l->addWidget(m_rateBtn);
    return w;
}

void MainWindow::applyTextSize(int percent) {
    QFont f = QApplication::font();
    f.setPointSizeF(m_baseFontPt * percent / 100.0);
    QApplication::setFont(f);

    QColor fg = palette().color(QPalette::WindowText);
    QColor dim = palette().color(QPalette::PlaceholderText);
    QColor win = palette().color(QPalette::Window);
    QColor panel = isDark() ? win.lighter(125) : win.darker(106);
    QColor accent = palette().color(QPalette::Highlight);
    qreal pt = f.pointSizeF();
    setStyleSheet(QStringLiteral(
        "QToolBar { spacing: 4px; padding: 4px; }"
        "QToolBar QToolButton { padding: 6px 10px; }"
        "QToolBar QToolButton { border: 1px solid transparent; border-radius: 6px; }"
        "QToolBar QToolButton:hover { border-color: %2; }"
        "QToolBar QToolButton:checked { background: %4; color: palette(highlighted-text); border-color: %4; }"
        "QToolBar QToolButton:disabled { color: %2; }"
        "#infoPanel { background: %3; }"
        "#infoPanel QLabel { color: %1; }"
        "#speed { font-size: %5pt; font-weight: 700; }"
        "#infoPanel #unit { color: %2; }"
        "#clock { font-size: %6pt; font-weight: 600; }"
        "#infoPanel #coords { color: %2; }"
        "#time { font-size: %6pt; }"
        "#bigOpen { font-size: %6pt; font-weight: 600; padding: 10px 30px; }"
        "QToolButton:focus, QPushButton:focus, QSlider:focus { outline: 2px solid %4; }")
        .arg(fg.name(), dim.name(), panel.name(), accent.name())
        .arg(pt * 2.6).arg(pt * 1.25));

    // repaint icons in the current text colour
    QColor ic = palette().color(QPalette::ButtonText);
    m_actOpenFolder->setIcon(makeIcon(Icon::Folder, ic));
    m_actOpenFiles->setIcon(makeIcon(Icon::File, ic));
    m_actExport->setIcon(makeIcon(Icon::Export, ic));
    m_actFollow->setIcon(makeIcon(Icon::Follow, ic));
    m_actFit->setIcon(makeIcon(Icon::Fit, ic));
    if (auto *sb = findChild<QToolButton *>("settingsBtn")) sb->setIcon(makeIcon(Icon::Settings, ic));
    for (auto *b : m_transport->findChildren<QToolButton *>()) {
        QVariant v = b->property("dgIcon");
        if (v.isValid()) b->setIcon(makeIcon(Icon(v.toInt()), ic));
    }
    bool playing = m_player->playbackState() == QMediaPlayer::PlayingState;
    m_playBtn->setIcon(makeIcon(playing ? Icon::Pause : Icon::Play, ic));
    updateVolumeIcon();
    int iconPx = qRound(22 * percent / 100.0);
    for (auto *tb : findChildren<QToolBar *>()) tb->setIconSize(QSize(iconPx, iconPx));
}

void MainWindow::applyMapTheme() {
    int mode = m_themeGroup->checkedAction() ? m_themeGroup->checkedAction()->data().toInt() : 0;
    m_map->setDark(mode == 2 || (mode == 0 && isDark()));
}

void MainWindow::updateVolumeIcon() {
    if (!m_muteBtn) return;
    bool muted = m_audio->isMuted() || m_audio->volume() <= 0.001f;
    m_muteBtn->setIcon(makeIcon(muted ? Icon::Muted : Icon::Volume, palette().color(QPalette::ButtonText)));
    m_muteBtn->setChecked(m_audio->isMuted());
    m_muteBtn->setAccessibleName(m_audio->isMuted() ? tr("Unmute") : tr("Mute"));
}

void MainWindow::setRate(double r) {
    m_rate = r;
    m_player->setPlaybackRate(r);
    m_rearPlayer->setPlaybackRate(r);
    m_audio->setRate(r);
    m_rateBtn->setText(QStringLiteral("%1×").arg(r));
    statusBar()->showMessage(tr("Playback speed %1×").arg(r), 2000);
}

void MainWindow::seekBy(qint64 deltaMs) {
    if (m_current < 0) return;
    qint64 t = m_player->position() + deltaMs;
    if (t < 0 && m_current > 0 && deltaMs < -1000) {
        loadClip(m_current - 1);
        return;
    }
    m_player->setPosition(qBound<qint64>(0, t, qMax<qint64>(0, m_player->duration() - 100)));
}

void MainWindow::play() {
    if (m_current < 0) return;
    m_player->play();
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
        } else if (fi.isFile() && kVideoExt.contains(fi.suffix().toLower())) {
            files << fi.absoluteFilePath();
        }
    }
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("No videos found"),
                                 tr("There are no dashcam videos (MP4, MOV or TS) in what you opened."));
        return;
    }
    m_player->stop();
    m_rearPlayer->stop();
    {
        QMutexLocker l(&m_trackLock);
        m_tracks.clear();
        ++m_parseGeneration;
    }
    m_current = -1;
    m_clips = buildClips(files);

    m_list->clear();
    QTreeWidgetItem *day = nullptr;
    QDate lastDay;
    for (int i = 0; i < m_clips.size(); ++i) {
        const Clip &c = m_clips[i];
        QDate d = c.start.date();
        if (!day || d != lastDay) {
            day = new QTreeWidgetItem(m_list, {d.isValid() ? QLocale().toString(d, QLocale::LongFormat) : tr("Unknown date")});
            day->setFlags(Qt::ItemIsEnabled);
            QFont f = day->font(0);
            f.setBold(true);
            day->setFont(0, f);
            lastDay = d;
        }
        QString label = c.start.isValid() ? fmtClock(c.start.time()) : QFileInfo(c.front).fileName();
        if (!c.rear.isEmpty()) label += QStringLiteral("   ") + tr("(front + rear)");
        auto *it = new QTreeWidgetItem(day, {label});
        it->setData(0, Qt::UserRole, i);
        it->setToolTip(0, QFileInfo(c.front).fileName() + (c.rear.isEmpty() ? QString() : "\n" + QFileInfo(c.rear).fileName()));
    }
    m_list->expandAll();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *top = m_list->topLevelItem(i);
        top->setText(0, top->text(0) + QStringLiteral("  (%1)").arg(top->childCount()));
    }
    m_centerStack->setCurrentIndex(1);
    m_transport->setEnabled(true);
    m_actExport->setEnabled(true);
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

    // select in list
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *top = m_list->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j)
            if (top->child(j)->data(0, Qt::UserRole).toInt() == index) {
                m_list->setCurrentItem(top->child(j));
                m_list->scrollToItem(top->child(j));
            }
    }

    m_player->setSource(QUrl::fromLocalFile(c.front));
    m_rearPlayer->setSource(c.rear.isEmpty() ? QUrl() : QUrl::fromLocalFile(c.rear));
    m_player->setPlaybackRate(m_rate);
    m_rearPlayer->setPlaybackRate(m_rate);
    updateViewMode();
    if (startMs) m_player->setPosition(startMs);
    if (wasPlaying) m_player->play();
    else m_player->pause();  // shows the first frame

    auto t = trackFor(index);
    QString name = QFileInfo(c.front).fileName();
    if (!t || t->format.empty()) statusBar()->showMessage(tr("%1: this clip has no GPS data").arg(name));
    else if (!t->hasFix()) statusBar()->showMessage(tr("%1: the camera had no GPS signal yet").arg(name));
    else statusBar()->showMessage(tr("Clip %1 of %2  ·  %3").arg(index + 1).arg(m_clips.size()).arg(name));
    setWindowTitle(QStringLiteral("DashGPS – %1").arg(c.title));
    updateMapTracks();
    if (!m_actFollow->isChecked()) m_map->fitTracks();
    else if (m_map->zoom() < 10) m_map->setZoom(16);
    updateFromPosition(startMs);
}

void MainWindow::updateMapTracks() {
    if (m_current < 0) return;
    // show every clip of the same day; the current one highlighted
    QDate day = m_clips[m_current].start.date();
    QList<QList<QPointF>> lines;
    QMutexLocker l(&m_trackLock);
    for (int i = 0; i < m_clips.size(); ++i) {
        QList<QPointF> line;
        if (m_clips[i].start.date() == day) {
            auto it = m_tracks.constFind(i);
            if (it != m_tracks.constEnd())
                for (auto &p : (*it)->points)
                    if (p.valid) line.append({p.lat, p.lon});
        }
        lines.append(line);
    }
    m_map->setTracks(lines, m_current);
}

QString MainWindow::fmtSpeed(double kmh) const {
    return QString::number(qRound(m_actMph->isChecked() ? kmh / 1.609344 : kmh));
}

void MainWindow::updateFromPosition(qint64 ms) {
    if (!m_timeLbl) return;
    m_timeLbl->setText(fmtTime(ms) + " / " + fmtTime(m_player->duration()));
    m_unitLbl->setText(m_actMph->isChecked() ? tr("mph") : tr("km/h"));
    if (m_current < 0) return;
    const Clip &c = m_clips[m_current];
    if (c.start.isValid()) {
        QDateTime now = c.start.addMSecs(ms);
        m_clockLbl->setText(fmtClock(now.time()));
        m_dateLbl->setText(QLocale().toString(now.date(), QLocale::LongFormat));
    }
    auto t = trackFor(m_current);
    GpsPoint p;
    if (t && t->at(ms / 1000.0, p)) {
        m_speedLbl->setText(fmtSpeed(p.speedKmh));
        m_infoLbl->setText(QStringLiteral("%1, %2  ·  %3°").arg(p.lat, 0, 'f', 6).arg(p.lon, 0, 'f', 6).arg(qRound(p.heading)));
        m_map->setCar(p.lat, p.lon, p.heading, true);
    } else {
        m_speedLbl->setText("–");
        m_infoLbl->setText(tr("No GPS signal"));
        m_map->setCar(0, 0, 0, false);
    }
}

void MainWindow::updateViewMode() {
    bool hasRear = m_current >= 0 && !m_clips[m_current].rear.isEmpty();
    int mode = m_camGroup->checkedAction()->data().toInt();
    for (auto *a : m_camGroup->actions())
        if (a->data().toInt() != 0) a->setEnabled(hasRear || m_current < 0);
    if (!hasRear) mode = 0;
    m_videoArea->setMode(VideoArea::Mode(mode));
}

void MainWindow::exportDialog() {
    if (m_clips.isEmpty() || m_current < 0) return;
    QMessageBox box(this);
    box.setWindowTitle(tr("Export route"));
    box.setText(tr("Which route do you want to save?"));
    auto *b0 = box.addButton(tr("This clip"), QMessageBox::AcceptRole);
    auto *b1 = box.addButton(tr("The whole day"), QMessageBox::AcceptRole);
    auto *b2 = box.addButton(tr("Everything"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(b1);
    box.exec();
    int scope = box.clickedButton() == b0 ? 0 : box.clickedButton() == b1 ? 1 : box.clickedButton() == b2 ? 2 : -1;
    if (scope < 0) return;

    QString sel;
    QString fn = QFileDialog::getSaveFileName(this, tr("Export route"), QSettings().value("lastExport").toString(),
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
    statusBar()->showMessage(o ? tr("Saved %n clip(s) to %1", nullptr, int(tracks.size())).arg(fn)
                               : tr("Could not save %1").arg(fn), 8000);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev) {
    auto belongsToUs = [this, obj] {
        if (auto *w = qobject_cast<QWidget *>(obj)) return w->window() == this;
        // native child windows, e.g. the video surface
        for (auto *win = qobject_cast<QWindow *>(obj); win; win = win->parent())
            if (win == windowHandle()) return true;
        return false;
    };
    if ((ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove || ev->type() == QEvent::Drop) &&
        !belongsToUs())
        return QMainWindow::eventFilter(obj, ev);
    switch (ev->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *de = static_cast<QDragMoveEvent *>(ev);
        if (de->mimeData()->hasUrls()) {
            de->acceptProposedAction();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        auto *de = static_cast<QDropEvent *>(ev);
        QStringList paths;
        for (auto &u : de->mimeData()->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (!paths.isEmpty()) {
            de->acceptProposedAction();
            QMetaObject::invokeMethod(this, [this, paths] { openPaths(paths); }, Qt::QueuedConnection);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::changeEvent(QEvent *e) {
    if (e->type() == QEvent::PaletteChange && m_sizeGroup && m_sizeGroup->checkedAction()) {
        // light/dark switch: recolour icons and styles
        QMetaObject::invokeMethod(this, [this] {
            applyTextSize(m_sizeGroup->checkedAction()->data().toInt());
            applyMapTheme();
        }, Qt::QueuedConnection);
    }
    QMainWindow::changeEvent(e);
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
