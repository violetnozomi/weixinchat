#ifndef VIDEOCALLDIALOG_H
#define VIDEOCALLDIALOG_H

#include <QDialog>
#include <QPushButton>
#include "videorender.h"
#include "api/video/video_sink_interface.h"

class VideoCallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VideoCallDialog(QWidget *parent = nullptr);
    ~VideoCallDialog();

    webrtc::VideoSinkInterface<webrtc::VideoFrame>* GetRemoteVideoSink();
    webrtc::VideoSinkInterface<webrtc::VideoFrame>* GetLocalVideoSink();

private slots:
    void on_hangup_clicked();

private:
    VideoRender* local_render_ = nullptr;
    VideoRender* remote_render_ = nullptr;
    QPushButton* btn_hangup_ = nullptr;
};

#endif // VIDEOCALLDIALOG_H