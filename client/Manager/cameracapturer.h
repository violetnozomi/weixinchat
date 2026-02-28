#ifndef CAMERACAPTURER_H
#define CAMERACAPTURER_H

#include <memory>
#include "api/video/video_frame.h"
#include "api/video/video_source_interface.h"
#include "media/base/video_broadcaster.h"
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "pc/video_track_source.h"

// 1. 摄像头采集器：负责从物理摄像头抓取画面
class CameraCapturer : public webrtc::VideoSinkInterface<webrtc::VideoFrame>,
                       public webrtc::VideoSourceInterface<webrtc::VideoFrame> {
public:
    static CameraCapturer* Create();
    ~CameraCapturer() override;

    void AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                         const webrtc::VideoSinkWants& wants) override;
    void RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) override;
    void OnFrame(const webrtc::VideoFrame& frame) override;

private:
    CameraCapturer();
    bool Init();

    webrtc::scoped_refptr<webrtc::VideoCaptureModule> vcm_;
    webrtc::VideoBroadcaster broadcaster_;
};

// 2. 视频源适配器：把采集器包装成 WebRTC 轨道需要的 Source 格式
class CapturerTrackSource : public webrtc::VideoTrackSource {
public:
    static webrtc::scoped_refptr<CapturerTrackSource> Create();
protected:
    explicit CapturerTrackSource(CameraCapturer* capturer);
    webrtc::VideoSourceInterface<webrtc::VideoFrame>* source() override;
private:
    std::unique_ptr<CameraCapturer> capturer_;
};

#endif // CAMERACAPTURER_H
