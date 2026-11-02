#pragma once
/* ans_processor.h — AIVOX ANS（降噪）算法封装模块
 *
 * PIMPL 隐藏算法 SDK(aivox_aec_ans.h / aivox_slas.h)细节，上层(界面/采集)只依赖
 * audio::ProcessorConfig 等轻量类型，不 include 算法 SDK。
 * 仅在本地(本模块)接入算法接口，替换算法包只需改本文件与 ans_processor.cpp 一处。
 * 本模块只做降噪（ANS），不含回声消除。
 */

#include <QString>
#include "audio_types.h"

class AnsProcessorImpl;

class AnsProcessor {
public:
    AnsProcessor();
    ~AnsProcessor();

    /* 初始化：授权(SLAS) + 创建引擎（model 为降噪模型 .mnn 路径）。
     * 返回错误信息文本（成功为空串）。 */
    QString initialize(const QString& model,
                       const QString& licenseDir,
                       const QString& authCode,
                       int module = 3);
    bool isInitialized() const;

    /* 处理一帧降噪（单麦输入，无参考信号）。
     * 返回错误信息（空=成功）。 */
    QString process(const int16_t* in, int16_t* out);

    /* 配置（需在 initialize 之后调用） */
    void setDenoiseStrength(float value);

    /* 重置自适应状态（新一轮录制前调用，避免沿用上一轮的收敛结果） */
    void reset();

    /* 收尾 */
    void cleanup();

    /* 版本字符串 */
    QString version() const;

private:
    AnsProcessorImpl* impl_;
};

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

/* 流式处理 worker：start() 完成授权/建引擎后起常驻线程，逐帧(10ms)喂算法并回调输出。
 * 采集侧每采到一帧就 submitFrame()，输出通过 frameProcessed 异步回传界面，
 * 端到端延迟约 1~3 帧（10~30ms）。引擎有状态（降噪自适应），配置变更从下一帧生效。 */
class StreamProcessWorker : public QObject {
    Q_OBJECT
public:
    /* model/licenseDir/authCode/module 用于引擎初始化；authCode 传空则走本地已激活 license */
    explicit StreamProcessWorker(const QString& model, const QString& licenseDir,
                                 const QString& authCode, int module, QObject* parent = nullptr);
    ~StreamProcessWorker() override;

    /* 初始化引擎并启动处理线程（初始化可能耗时数百 ms，在调用线程内同步完成）。
     * 返回错误串（空=成功）。 */
    QString start(const audio::ProcessorConfig& cfg);
    void stop();                                   /* 停线程（先排空队列），算法引擎保留复用 */

    void setConfig(const audio::ProcessorConfig& cfg);        /* 从下一帧生效 */
    void submitFrame(const int16_t* in, size_t n);             /* 线程安全 */

    bool isRunning() const { return running_.load(); }
    audio::ProcessStats stats() const;
    /* 取走本次会话的完整处理结果（停止后调用；可重复取） */
    std::vector<int16_t> takeOutput() const;
    int  droppedFrames() const;

signals:
    void frameProcessed(const std::vector<int16_t>& out);
    void failed(const QString& err);

private:
    struct Item {
        std::vector<int16_t> in;    /* 主麦 */
    };
    void runLoop();

    AnsProcessor proc_;         /* 引擎仅由处理线程访问（start() 之后） */
    QString model_, license_, auth_;
    int     module_ = 3;

    std::atomic<bool> running_{false};
    std::thread thread_;

    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    std::deque<Item>        queue_;
    audio::ProcessorConfig  cfg_;
    audio::ProcessorConfig  applied_;      /* 已下发到引擎的参数（仅处理线程改） */
    audio::ProcessStats     stats_;
    std::vector<int16_t>    outAcc_;       /* 本次会话累计输出（停止时取走落盘） */
    int  dropped_ = 0;
    size_t skipOut_ = 0;                   /* 引擎收敛期丢弃的输出帧数（会话开始时设置） */

    /* 积压上限：约 2s。实时性优先，超出时丢最旧帧而不是无限增长延迟 */
    static constexpr size_t kMaxQueue = 200;
};
