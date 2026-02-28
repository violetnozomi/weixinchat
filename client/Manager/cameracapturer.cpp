#include "cameracapturer.h"
#include <QDebug>

// =========================================================
// CameraCapturer 实现：操控物理摄像头
// =========================================================
CameraCapturer::CameraCapturer() {}

CameraCapturer::~CameraCapturer() {
    if (vcm_) {
        vcm_->StopCapture();
        vcm_->DeRegisterCaptureDataCallback();
        vcm_ = nullptr;
    }
}

CameraCapturer* CameraCapturer::Create() {
    CameraCapturer* capturer = new CameraCapturer();
    if (!capturer->Init()) {
        delete capturer;
        return nullptr;
    }
    return capturer;
}

bool CameraCapturer::Init() {
    // 获取设备信息
    std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
        webrtc::VideoCaptureFactory::CreateDeviceInfo());
    if (!info || info->NumberOfDevices() == 0) {
        qDebug() << "[Camera] ❌ 没有找到任何摄像头设备！";
        return false;
    }

    // 获取第一个摄像头 (默认摄像头)
    char device_name[256];
    char unique_name[256];
    if (info->GetDeviceName(0, device_name, 256, unique_name, 256) != 0) {
        return false;
    }

    qDebug() << "[Camera] 📷 成功找到并准备打开摄像头:" << device_name;

    // 创建采集模块
    vcm_ = webrtc::VideoCaptureFactory::Create(unique_name);
    if (!vcm_) return false;

    vcm_->RegisterCaptureDataCallback(this);

    // 设置采集分辨率和帧率 (640x480, 30fps)
    webrtc::VideoCaptureCapability capability;
    capability.width = 640;
    capability.height = 480;
    capability.maxFPS = 30;
    capability.videoType = webrtc::VideoType::kI420;

    if (vcm_->StartCapture(capability) != 0) {
        qDebug() << "[Camera] ❌ 启动摄像头失败，可能被其他程序占用！";
        return false;
    }
    return true;
}

void CameraCapturer::OnFrame(const webrtc::VideoFrame& frame) {
    broadcaster_.OnFrame(frame); // 把画面广播给所有绑定的画板
}

void CameraCapturer::AddOrUpdateSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink,
                                     const webrtc::VideoSinkWants& wants) {
    broadcaster_.AddOrUpdateSink(sink, wants);
}

void CameraCapturer::RemoveSink(webrtc::VideoSinkInterface<webrtc::VideoFrame>* sink) {
    broadcaster_.RemoveSink(sink);
}

// =========================================================
// CapturerTrackSource 实现：适配 WebRTC 规范
// =========================================================
webrtc::scoped_refptr<CapturerTrackSource> CapturerTrackSource::Create() {
    CameraCapturer* capturer = CameraCapturer::Create();
    if (capturer) {
        return webrtc::make_ref_counted<CapturerTrackSource>(capturer);
    }
    return nullptr;
}

CapturerTrackSource::CapturerTrackSource(CameraCapturer* capturer)
    : webrtc::VideoTrackSource(false), capturer_(capturer) {}

webrtc::VideoSourceInterface<webrtc::VideoFrame>* CapturerTrackSource::source() {
    return capturer_.get();
}
