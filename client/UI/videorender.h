#ifndef VIDEORENDER_H
#define VIDEORENDER_H

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <mutex>
#include <memory>

// 前向声明 WebRTC 的类，绝不在此暴露完整头文件给 Qt 的 MOC
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

#include <atomic>

class VideoRender : public QWidget
{
    Q_OBJECT

public:
    explicit VideoRender(QWidget *parent = nullptr);
    ~VideoRender() override;

    // 获取 WebRTC 视频接收器接口
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* GetVideoSink();

    // 关闭帧接收，调用后 OnFrame 将立即返回（用于平安关闭）
    void Stop() { _stopped.store(true, std::memory_order_release); }

protected:
    void paintEvent(QPaintEvent *event) override;

signals:
    void sig_frameUpdated();

private slots:
    void slot_updateUI();

private:
    // 定义一个内部类，专门用来做 WebRTC 的视频帧接收器
    class FrameSink;
    std::unique_ptr<FrameSink> sink_;

    std::atomic<bool> _stopped{false};
    std::mutex mutex_;
    QImage current_image_;

    friend class FrameSink;
};

#endif // VIDEORENDER_H
