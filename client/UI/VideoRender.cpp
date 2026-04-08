#include "videorender.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "third_party/libyuv/include/libyuv.h"

// =====================================================================
// 内部类实现：专门负责接收 WebRTC 底层线程推过来的视频帧
// =====================================================================
class VideoRender::FrameSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
public:
    FrameSink(VideoRender* render) : render_(render) {}

    // WebRTC 后台线程会不断调用这个函数
    void OnFrame(const webrtc::VideoFrame& frame)  {
        // Safety guard: widget may be destroying on main thread
        if (render_->_stopped.load(std::memory_order_acquire)) return;

        webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer = frame.video_frame_buffer();

        webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer = buffer->ToI420();
        if (!i420_buffer) return;

        QImage img(i420_buffer->width(), i420_buffer->height(), QImage::Format_ARGB32);

        // 利用 libyuv 极速转换颜色格式
        libyuv::I420ToARGB(i420_buffer->DataY(), i420_buffer->StrideY(),
                           i420_buffer->DataU(), i420_buffer->StrideU(),
                           i420_buffer->DataV(), i420_buffer->StrideV(),
                           img.bits(), img.bytesPerLine(),
                           i420_buffer->width(), i420_buffer->height());

        {
            std::lock_guard<std::mutex> lock(render_->mutex_);
            render_->current_image_ = img.copy(); // 拷入主类的变量中
        }

        // 发送信号给 Qt 主线程刷新界面
        emit render_->sig_frameUpdated();
    }

private:
    VideoRender* render_;
};

// =====================================================================
// Qt 主界面类实现
// =====================================================================
VideoRender::VideoRender(QWidget *parent)
    : QWidget(parent), sink_(std::make_unique<FrameSink>(this)) // 初始化内部接收器
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setStyleSheet("background-color: black;");

    // 跨线程信号槽连接
    connect(this, &VideoRender::sig_frameUpdated,
            this, &VideoRender::slot_updateUI,
            Qt::QueuedConnection);
}

VideoRender::~VideoRender()
{
    // Signal OnFrame to stop before sink_ and this widget are torn down
    _stopped.store(true, std::memory_order_release);
}

webrtc::VideoSinkInterface<webrtc::VideoFrame>* VideoRender::GetVideoSink()
{
    return sink_.get();
}

void VideoRender::slot_updateUI()
{
    update(); // 触发 paintEvent
}

void VideoRender::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    painter.fillRect(rect(), Qt::black);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_image_.isNull()) {
        // 保持比例缩放居中绘制
        QImage scaled_img = current_image_.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        int x = (width() - scaled_img.width()) / 2;
        int y = (height() - scaled_img.height()) / 2;
        painter.drawImage(x, y, scaled_img);
    }
}
