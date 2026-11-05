#include <QApplication>
#include <QCommandLineParser>
#include <QString>

#include <cstdio>
#include <memory>

#include "audio_output.h"
#include "main_window.h"
#include "tts_engine.h"

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("tts_qt_demo");

  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Chinese text-to-speech demo (AIVOX TTS) for Quectel Pi M2.");
  parser.addOptions({
      {"text", "Initial text shown in the editor.", "text"},
      {"audio-device", "ALSA playback device (e.g. plughw:0,0).", "device"},
      {"audio-rate",
       "Actual hardware playback rate (e.g. 48000) when codec clock is fixed "
       "and ALSA reports a wrong rate.", "rate"},
  });
  parser.process(app);

  QString text = parser.value("text");

  QString device = parser.value("audio-device");
  if (device.isEmpty()) device = qEnvironmentVariable("TTS_AUDIO_DEV");

  QString rate_text = parser.value("audio-rate");
  if (rate_text.isEmpty()) rate_text = qEnvironmentVariable("TTS_AUDIO_HW_RATE");
  unsigned int hw_rate = 0;
  if (!rate_text.isEmpty()) {
    bool rate_ok = true;
    hw_rate = rate_text.toUInt(&rate_ok);
    if (!rate_ok) {
      std::fprintf(stderr, "[audio] ignore invalid audio rate: %s\n",
                   rate_text.toUtf8().constData());
    }
  }

  auto sink = std::make_shared<tts::AlsaAudioSink>(device.toStdString(), hw_rate);

  auto engine_factory = []() -> std::shared_ptr<tts::ITtsEngine> {
    return tts::createEngine();
  };

  MainWindow window(sink, engine_factory, text);
  window.show();

  return app.exec();
}
