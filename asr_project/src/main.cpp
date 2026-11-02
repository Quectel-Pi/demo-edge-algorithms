// main.cpp - 入口与组装
//
// 解析命令行模型路径，创建音频源、识别引擎和界面，启动应用。
// 模型路径默认指向板端算法包内的流式中文模型，可用
// --tokens/--encoder/--decoder/--joiner 覆盖。

#include <memory>
#include <vector>

#include <QApplication>
#include <QCommandLineParser>
#include <QString>

#include "asr_engine.h"
#include "audio_input.h"
#include "main_window.h"

namespace {

struct ModelPaths {
  QString tokens;
  QString encoder;
  QString decoder;
  QString joiner;
};

ModelPaths parseArgs(const QCoreApplication& app) {
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Real-time streaming speech recognition demo (sherpa-onnx).");
  parser.addOptions({
      {"tokens", "Path to tokens.txt.", "path"},
      {"encoder", "Path to encoder.onnx.", "path"},
      {"decoder", "Path to decoder.onnx.", "path"},
      {"joiner", "Path to joiner.onnx.", "path"},
  });
  parser.process(app);

  // 模型默认指向内嵌算法包（相对可执行文件：build/ 的上一级即工程根目录）
  const QString m = QCoreApplication::applicationDirPath() +
      "/../sherpa-onnx-asr-conformer-dewatermark"
      "/res/sherpa-onnx-streaming-conformer-zh-2023-05-23";
  ModelPaths p;
  p.tokens = parser.value("tokens");
  p.encoder = parser.value("encoder");
  p.decoder = parser.value("decoder");
  p.joiner = parser.value("joiner");
  if (p.tokens.isEmpty()) p.tokens = m + "/tokens.txt";
  if (p.encoder.isEmpty()) p.encoder = m + "/encoder-epoch-99-avg-1.int8.onnx";
  if (p.decoder.isEmpty()) p.decoder = m + "/decoder-epoch-99-avg-1.int8.onnx";
  if (p.joiner.isEmpty()) p.joiner = m + "/joiner-epoch-99-avg-1.int8.onnx";
  return p;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("asr_qt_demo");

  const ModelPaths paths = parseArgs(app);

  // 音频获取：板载麦克风（ALSA，系统默认设备）
  auto source = std::make_shared<asr::AlsaAudioSource>(/*device=*/"");

  // 算法接入：唯一接触 sherpa-onnx 的地方。
  // 用工厂延迟创建——窗口显示后由界面在后台低优先级线程里预加载模型，
  // 这样点 Start 立刻就能说话，模型加载也不会让窗口卡死。
  auto engine_factory = [paths]() -> std::shared_ptr<asr::IAsrEngine> {
    return std::make_shared<asr::SherpaAsrEngine>(
        paths.tokens.toStdString(), paths.encoder.toStdString(),
        paths.decoder.toStdString(), paths.joiner.toStdString());
  };

  // 处理链：空 = 直通；降噪 / AEC 后续在此插入
  std::vector<std::shared_ptr<asr::IAudioProcessor>> processors;

  MainWindow window(source, engine_factory, processors);
  window.show();

  return app.exec();
}