// Audio output that always plays stereo.
//
// QAudioOutput lets the FFmpeg backend open the stream with the device's
// preferred layout. On a 5.1 sink a mono dashcam track then ends up on the
// centre channel only, which is silent on stereo speakers. Instead we take
// decoded buffers converted to 48 kHz stereo and push them to a QAudioSink.
#pragma once

#include <QAudioFormat>
#include <QObject>

class QAudioBuffer;
class QAudioBufferOutput;
class QAudioSink;
class QIODevice;
class QMediaDevices;
class QMediaPlayer;

class StereoAudio : public QObject {
    Q_OBJECT
public:
    explicit StereoAudio(QMediaPlayer *player, QObject *parent = nullptr);

    void setVolume(float v);
    float volume() const { return m_volume; }
    void setMuted(bool m);
    bool isMuted() const { return m_muted; }
    void setRate(double r) { m_rate = r; }

private:
    void openSink();
    void onBuffer(const QAudioBuffer &buf);
    void applyVolume();

    QMediaPlayer *m_player;
    QAudioFormat m_format;
    QAudioBufferOutput *m_bufOut = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr;
    QMediaDevices *m_devices = nullptr;
    float m_volume = 0.8f;
    bool m_muted = false;
    double m_rate = 1.0;
};
