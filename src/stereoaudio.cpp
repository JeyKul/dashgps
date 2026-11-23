#include "stereoaudio.h"

#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioSink>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QtMath>

StereoAudio::StereoAudio(QMediaPlayer *player, QObject *parent) : QObject(parent), m_player(player) {
    m_format.setSampleRate(48000);
    m_format.setChannelCount(2);
    m_format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    m_format.setSampleFormat(QAudioFormat::Float);

    m_bufOut = new QAudioBufferOutput(m_format, this);
    m_player->setAudioBufferOutput(m_bufOut);
    connect(m_bufOut, &QAudioBufferOutput::audioBufferReceived, this, &StereoAudio::onBuffer);

    // drop queued audio on seek / pause so it doesn't lag behind the picture
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState st) {
        if (st != QMediaPlayer::PlayingState) openSink();
    });
    connect(m_player, &QMediaPlayer::sourceChanged, this, &StereoAudio::openSink);

    m_devices = new QMediaDevices(this);
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &StereoAudio::openSink);
    openSink();
}

void StereoAudio::openSink() {
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
    }
    m_sink = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);
    m_sink->setBufferSize(m_format.bytesForDuration(150000));  // 150 ms
    applyVolume();
    m_io = m_sink->start();
}

void StereoAudio::onBuffer(const QAudioBuffer &buf) {
    if (!m_io || !buf.isValid()) return;
    if (!qFuzzyCompare(m_rate, 1.0)) return;  // fast/slow motion: silent, like most players
    if (m_player->playbackState() != QMediaPlayer::PlayingState) return;
    qint64 n = buf.byteCount();
    qint64 free = m_sink->bytesFree();
    // if the sink is full (e.g. after a hiccup), drop instead of building up delay
    m_io->write(buf.constData<char>(), qMin(n, free));
}

void StereoAudio::applyVolume() {
    if (!m_sink) return;
    // perceptual (logarithmic) volume curve for the slider
    float lin = QAudio::convertVolume(m_volume, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
    m_sink->setVolume(m_muted ? 0.0 : lin);
}

void StereoAudio::setVolume(float v) {
    m_volume = qBound(0.0f, v, 1.0f);
    applyVolume();
}

void StereoAudio::setMuted(bool m) {
    m_muted = m;
    applyVolume();
}
