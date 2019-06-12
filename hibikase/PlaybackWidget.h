// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <chrono>
#include <memory>

#include <QAudioOutput>
#include <QByteArray>
#include <QIODevice>
#include <QLabel>
#include <QPushButton>
#include <QThread>
#include <QWidget>

#include "AudioFile.h"

class AudioOutputWorker final : public QObject
{
    Q_OBJECT

public:
    explicit AudioOutputWorker(std::unique_ptr<QIODevice> io_device, QWidget* parent = nullptr);

public slots:
    void Initialize();
    void Play();
    void Stop();

signals:
    void StateChanged(QAudio::State state);
    void TimeUpdated(std::chrono::milliseconds ms);

private slots:
    void OnNotify();

private:
    std::unique_ptr<QIODevice> m_io_device;
    std::unique_ptr<AudioFile> m_audio_file;
    std::unique_ptr<QAudioOutput> m_audio_output;
};

class PlaybackWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlaybackWidget(QWidget* parent = nullptr);
    ~PlaybackWidget();

    void LoadAudio(std::unique_ptr<QIODevice> io_device);  // Can be nullptr

signals:
    void TimeUpdated(std::chrono::milliseconds ms);

private slots:
    void OnPlayButtonClicked();
    void OnStateChanged(QAudio::State state);
    void UpdateTime(std::chrono::milliseconds ms);

private:
    QPushButton* m_play_button;
    QLabel* m_time_label;

    AudioOutputWorker* m_worker = nullptr;
    QThread m_thread;

    QAudio::State m_state;
};

Q_DECLARE_METATYPE(std::chrono::milliseconds);