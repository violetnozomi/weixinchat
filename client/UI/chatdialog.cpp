#include "chatdialog.h"
#include "ui_chatdialog.h"
#include "FileTransferMgr.h"
#include "profileeditdialog.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QMessageBox>
#include "chatbubbleitem.h"
#include "chat_user_wid.h"
#include "searchuseritem.h"
#include "addfriendwidget.h"
#include "httpmgr.h"
#include "tcpmgr.h"
#include <QTimer>
#include <QDebug>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QScrollBar>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QStringList>
#include <QTextDocument>
#include <QTextBlock>
#include <QMessageBox>
#include <QRegularExpression>
#include <QImage>
#include <QTextCursor>
#include <QHBoxLayout>
#include <QMediaDevices>
#include "voicecalldialog.h"
#include "videocalldialog.h"
#include "CallSessionMgr.h"
#include "webrtcmgr.h"

#ifdef SendMessage
#undef SendMessage
#endif

namespace {

QString JsonIdToString(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(static_cast<qulonglong>(value.toDouble()));
    }
    return QString();
}

QString FormatVoiceIdleLabel(int totalSec)
{
    return QString("Play %1s").arg(totalSec);
}

QString FormatVoicePlayingLabel(int currentSec, int totalSec)
{
    return QString("Stop %1:%2 / %3s")
        .arg(currentSec / 60, 2, 10, QLatin1Char('0'))
        .arg(currentSec % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSec);
}

int VoiceBubbleWidth(int totalSec)
{
    return qBound(132, 132 + qMin(totalSec, 24) * 3, 220);
}

void UpdateVoiceButtonVisual(QPushButton *button, int totalSec,
                             const QString &state, int currentSec = 0)
{
    if (!button) {
        return;
    }

    button->setProperty("voiceState", state);
    button->setText(state == "playing"
                        ? FormatVoicePlayingLabel(currentSec, totalSec)
                        : FormatVoiceIdleLabel(totalSec));
    repolish(button);
}

QString BuildVoiceMessagePayload(const QString &voicePath, int durationMs)
{
    QJsonObject obj;
    obj["local_path"] = voicePath;
    obj["duration_ms"] = durationMs;
    return QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool ParseVoiceMessagePayload(const QString &payload,
                              QString *voicePath,
                              int *durationMs)
{
    const auto doc = QJsonDocument::fromJson(payload.toUtf8());
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    const QString path = obj["local_path"].toString();
    const int duration = obj["duration_ms"].toInt();
    if (path.isEmpty() || duration <= 0) {
        return false;
    }

    if (voicePath) {
        *voicePath = path;
    }
    if (durationMs) {
        *durationMs = duration;
    }
    return true;
}

QString VoicePreviewText(int durationMs)
{
    return QString::fromUtf8("[语音] %1s").arg(qMax(1, durationMs / 1000));
}

void AppendLe16(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
}

void AppendLe32(QByteArray &data, quint32 value)
{
    data.append(static_cast<char>(value & 0xff));
    data.append(static_cast<char>((value >> 8) & 0xff));
    data.append(static_cast<char>((value >> 16) & 0xff));
    data.append(static_cast<char>((value >> 24) & 0xff));
}

QByteArray BuildWaveHeader(const QAudioFormat &format, quint32 dataBytes)
{
    QByteArray header;
    header.reserve(44);

    const quint16 channels = static_cast<quint16>(qMax(1, format.channelCount()));
    const quint32 sampleRate = static_cast<quint32>(qMax(1, format.sampleRate()));
    const quint16 bitsPerSample =
        static_cast<quint16>(qMax(8, format.bytesPerSample() * 8));
    const quint16 blockAlign =
        static_cast<quint16>(qMax(1, format.bytesPerFrame()));
    const quint32 byteRate = sampleRate * blockAlign;
    const quint16 audioFormat =
        format.sampleFormat() == QAudioFormat::Float ? 3 : 1;

    header.append("RIFF", 4);
    AppendLe32(header, 36u + dataBytes);
    header.append("WAVE", 4);
    header.append("fmt ", 4);
    AppendLe32(header, 16u);
    AppendLe16(header, audioFormat);
    AppendLe16(header, channels);
    AppendLe32(header, sampleRate);
    AppendLe32(header, byteRate);
    AppendLe16(header, blockAlign);
    AppendLe16(header, bitsPerSample);
    header.append("data", 4);
    AppendLe32(header, dataBytes);
    return header;
}

bool RewriteWaveHeader(QFile *file, const QAudioFormat &format)
{
    if (!file || !file->isOpen()) {
        return false;
    }

    const qint64 fileSize = file->size();
    if (fileSize < 44) {
        return false;
    }

    const QByteArray header = BuildWaveHeader(
        format, static_cast<quint32>(fileSize - 44));
    file->seek(0);
    return file->write(header) == header.size();
}

} // namespace

chatDialog::chatDialog(QWidget *parent)
    : QDialog(parent)
    , _mode(ChatUIMode::ChatMode)
    , _state(ChatUIMode::ChatMode)
    , _b_loading(false)
    , _current_page(0)
    , _current_chat_uid(0)
    , ui(new Ui::chatDialog)
    , _list_stack(nullptr)
    , _chat_user_list(nullptr)
    , _contact_panel(nullptr)
    , _right_stack(nullptr)
    , _verify_panel(nullptr)
    , _profile_panel(nullptr)
    , _search_list_page(nullptr)
    , _search_scroll(nullptr)
    , _search_content(nullptr)
    , _search_layout(nullptr)
    , _search_hint(nullptr)
    , _msg_layout(nullptr)
    , _net_status_bar(nullptr)
    , _countdown_timer(new QTimer(this))
    , _reconnect_countdown_sec(0)
{
    ui->setupUi(this);

    // 窗口属�?
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    resize(900, 650);
    setMinimumSize(800, 600);
    setObjectName("chatDialog");

    // 加载样式�?
    QFile qss(":/style/stylesheet.qss");
    if (qss.open(QFile::ReadOnly)) {
        this->setStyleSheet(qss.readAll());
        qss.close();
    }
    _record_max_timer = new QTimer(this);
    _record_max_timer->setSingleShot(true);
    connect(_record_max_timer, &QTimer::timeout, this, [this]() {
        if (_is_recording_voice) {
            slot_toggle_record_voice();
        }
    });
    slot_warmup_audio_input();

    // ── 断线重连提示横幅 ────────────────────────────────────────────────
    // 插入�?chat_data_widget �?verticalLayout_4 中（title �?scroll 之间�?
    _net_status_bar = new QLabel(this);
    _net_status_bar->setObjectName("net_status_bar");
    _net_status_bar->setAlignment(Qt::AlignCenter);
    _net_status_bar->setFixedHeight(28);
    _net_status_bar->setStyleSheet(
        "background:#f59a23; color:white; font-size:12px;"
        " font-family:'Microsoft YaHei'; border:none;");
    _net_status_bar->hide();
    // 插入�?verticalLayout_4 �?index 1（title=0, statusbar=1, scroll=2�?
    if (auto *vl = qobject_cast<QVBoxLayout*>(ui->chat_data_widget->layout()))
        vl->insertWidget(1, _net_status_bar);

    // 倒计时刷�?
    _countdown_timer->setInterval(1000);
    connect(_countdown_timer, &QTimer::timeout, this, [this]() {
        if (_reconnect_countdown_sec <= 0) {
            _countdown_timer->stop();
            return;
        }
        --_reconnect_countdown_sec;
        _net_status_bar->setText(
            QString::fromUtf8("网络已断开，%1 秒后重试，").arg(_reconnect_countdown_sec));
    });
    // ────────────────────────────────────────────────────────────────────

    // ── 右侧面板 QStackedWidget 包装 ─────────────────────────────────────
    // �?chat_data_widget 从外�?horizontalLayout 取出，放�?_right_stack index 0
    {
        QLayout *topLayout = this->layout();  // horizontalLayout
        topLayout->removeWidget(ui->chat_data_widget);

        _right_stack = new QStackedWidget(this);
        _right_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        _right_stack->addWidget(ui->chat_data_widget);  // index 0: 聊天面板

        _verify_panel = new FriendVerifyPanel();
        _right_stack->addWidget(_verify_panel);          // index 1: 好友验证面板

        _profile_panel = new FriendProfilePanel();
        _right_stack->addWidget(_profile_panel);         // index 2: 好友资料�?

        topLayout->addWidget(_right_stack);

        // 重置各列宽度拉伸�?
        if (auto *hLayout = qobject_cast<QHBoxLayout*>(topLayout)) {
            hLayout->setStretch(0, 0);   // side_bar: 固定
            hLayout->setStretch(1, 0);   // chat_user_widget: 固定
            hLayout->setStretch(2, 1);   // _right_stack: 拉伸
        }

        _right_stack->setCurrentIndex(0);
    }
    // ──────────────────────────────────────────────────────────────────────

    // ── 直接�?QStackedWidget 加入 chat_user_widget �?QVBoxLayout ──────────
    // 不通过任何中间 native QWidget，避�?native 窗口隔断布局传播
    QVBoxLayout *outerVBox = qobject_cast<QVBoxLayout *>(ui->chat_user_widget->layout());
    Q_ASSERT(outerVBox);

    _list_stack = new QStackedWidget();
    _list_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outerVBox->addWidget(_list_stack);        // 追加�?verticalLayout_3
    outerVBox->setStretch(0, 0);              // search_widget: 不拉�?
    outerVBox->setStretch(1, 1);              // _list_stack  : 占满剩余空间

    // page 0: 聊天列表（_chat_user_list 直接作为页面，省去多余容器）
    _chat_user_list = new Chat_User_List();
    _chat_user_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _list_stack->addWidget(_chat_user_list);  // index 0

    // page 1: 搜索结果页（带滚动条�?
    _search_scroll = new QScrollArea();
    _search_scroll->setWidgetResizable(true);
    _search_scroll->setFrameShape(QFrame::NoFrame);
    _search_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _search_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    _search_content = new QWidget();
    _search_content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _search_layout = new QVBoxLayout(_search_content);
    _search_layout->setSpacing(0);
    _search_layout->setContentsMargins(0, 0, 0, 0);
    _search_layout->addStretch();   // 底部弹簧，让结果顶部对齐

    // ── 空闲提示页（搜索框聚焦但尚未搜索时展示）────────────────────────
    _search_hint = new QWidget(_search_content);
    _search_hint->setObjectName("search_hint_widget");
    _search_hint->setMinimumHeight(260);
    _search_hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    {
        QVBoxLayout *hv = new QVBoxLayout(_search_hint);
        hv->setContentsMargins(0, 0, 0, 0);
        hv->setSpacing(0);

        // 搜索图标
        QLabel *iconLb = new QLabel();
        QPixmap px(":/res/search.png");
        iconLb->setPixmap(px.scaled(54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLb->setAlignment(Qt::AlignCenter);
        iconLb->setStyleSheet("background:transparent; opacity:0.4;");

        // 主提示文�?
        QLabel *titleLb = new QLabel(QString::fromUtf8("搜索微信号 / 手机号 / QQ号"));
        titleLb->setObjectName("search_hint_title");
        titleLb->setAlignment(Qt::AlignCenter);

        // 副标�?
        QLabel *subLb = new QLabel(QString::fromUtf8("添加新的朋友"));
        subLb->setObjectName("search_hint_sub");
        subLb->setAlignment(Qt::AlignCenter);

        hv->addStretch(1);
        hv->addWidget(iconLb);
        hv->addSpacing(16);
        hv->addWidget(titleLb);
        hv->addSpacing(6);
        hv->addWidget(subLb);
        hv->addStretch(1);
    }
    // 插在底部 stretch 之前（index 0�?
    _search_layout->insertWidget(0, _search_hint);
    // ─────────────────────────────────────────────────────────────────────

    _search_scroll->setWidget(_search_content);

    _search_list_page = _search_scroll;   // 兼容旧指针（指向同一对象�?
    _list_stack->addWidget(_search_scroll); // index 1

    // page 2: 联系人面板（ContactMode 左侧�?
    _contact_panel = new ContactListPanel();
    _contact_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _list_stack->addWidget(_contact_panel);   // index 2

    _list_stack->setCurrentIndex(0);
    // ──────────────────────────────────────────────────────────────────────

    // 配置搜索�?
    QAction *search_action = new QAction(ui->search_edit);
    search_action->setIcon(QIcon(":/res/search.png"));
    ui->search_edit->addAction(search_action, QLineEdit::LeadingPosition);
    ui->search_edit->setPlaceholderText(QStringLiteral("搜索"));

    QAction *clear_action = new QAction(ui->search_edit);
    ui->search_edit->addAction(clear_action, QLineEdit::TrailingPosition);
    clear_action->setIcon(QIcon(":/res/close_transparent.png"));

    connect(ui->search_edit, &QLineEdit::textChanged, this, [this, clear_action](const QString &text) {
        clear_action->setIcon(text.isEmpty()
                              ? QIcon(":/res/close_transparent.png")
                              : QIcon(":/res/close_search.png"));
        // 输入被清空时立即恢复提示�?
        if (text.trimmed().isEmpty() && _mode == ChatUIMode::SearchMode) {
            DoSearch("");
        }
    });
    connect(clear_action, &QAction::triggered, this, [this, clear_action]() {
        ui->search_edit->clear();
        clear_action->setIcon(QIcon(":/res/close_transparent.png"));
        DoSearch("");   // 恢复提示�?
    });

    connect(ui->search_edit, &CustomizeEdit::sig_focus_out,
            this, &chatDialog::slot_search_focus_out);
    ui->search_edit->installEventFilter(this);
    // 让对话框本身及所有子 widget 的鼠标事件都经过 eventFilter
    qApp->installEventFilter(this);

    // 连接好友列表信号
    connect(_chat_user_list, &Chat_User_List::sig_loading_chat_user,
            this, &chatDialog::slot_loading_chat_user);
    connect(_chat_user_list, &Chat_User_List::sig_chat_user_clicked,
            this, qOverload<const QString &>(&chatDialog::slot_chat_user_clicked));


    ShowSearch(false);
    ui->search_edit->SetMaxLength(15);
    ui->search_edit->clearFocus();
    this->setFocus();

    // ── side_bar 初始�?──────────────────────────────────────────────────
    // 自己头像（圆形裁剪）
    {
        QPixmap src(":/res/head_1.jpg");
        if (!src.isNull()) {
            int sz = 36;
            src = src.scaled(sz, sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QPixmap rounded(sz, sz);
            rounded.fill(Qt::transparent);
            QPainter p(&rounded);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addEllipse(0, 0, sz, sz);
            p.setClipPath(path);
            p.drawPixmap(0, 0, src);
            ui->side_head_lb->setPixmap(rounded);
        }
    }

    // 消息图标
    ui->side_chat_lb->SetState("normal", "hover", "pressed", "selected_normal",
                               "selected_hover", "selected_pressed");

    // 联系人图�?
    ui->side_contact_lb->SetState("normal", "hover", "pressed", "selected_normal",
                                  "selected_hover", "selected_pressed");
    ui->side_group_lb->SetState("normal", "hover", "pressed", "selected_normal",
                                "selected_hover", "selected_pressed");
    ui->side_group_lb->setToolTip("Create group chat");
    ui->side_group_lb->setAlignment(Qt::AlignCenter);

    // 左侧互斥 + 面板切换
    connect(ui->side_chat_lb, &ClickedLable::clicked, this, [this](){
        if (ui->side_contact_lb->GetCurState() == ClickLbState::Selected) {
            ui->side_contact_lb->ResetToNormal();
        }
        slot_switch_chat_mode();
    });
    connect(ui->side_contact_lb, &ClickedLable::clicked, this, [this](){
        if (ui->side_chat_lb->GetCurState() == ClickLbState::Selected) {
            ui->side_chat_lb->ResetToNormal();
        }
        slot_switch_contact_mode();
    });
    connect(ui->side_group_lb, &ClickedLable::clicked, this, [this](){
        ui->side_group_lb->ResetToNormal();
        slot_create_group_clicked();
    });

    // 联系人面板：点击"新的朋友"�?�?右侧显示验证面板
    connect(_contact_panel, &ContactListPanel::sig_new_friends_clicked, this, [this](){
        _right_stack->setCurrentIndex(1);
    });

    // 联系人面板：点击好友�?�?右侧显示好友资料�?
    connect(_contact_panel, &ContactListPanel::sig_friend_clicked,
            this, &chatDialog::slot_friend_profile_clicked);

    // 好友资料卡：点击"发消�?
    connect(_profile_panel, &FriendProfilePanel::sig_send_message,
            this, &chatDialog::slot_profile_send_msg);

    // 好友验证面板信号
    connect(_verify_panel, &FriendVerifyPanel::sig_accepted, this,
            &chatDialog::slot_friend_req_accepted);
    connect(_verify_panel, &FriendVerifyPanel::sig_rejected, this,
            &chatDialog::slot_friend_req_rejected);
    connect(_verify_panel, &FriendVerifyPanel::sig_pending_changed, this,
            [this](int count) { _contact_panel->SetPendingCount(count); });

    // TCP 层信号连�?
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_notify_add_friend,
            this, &chatDialog::slot_notify_add_friend);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_accept_friend_rsp,
            this, &chatDialog::slot_accept_friend_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_notify_accept_friend,
            this, &chatDialog::slot_notify_accept_friend);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_msg_received,
            this, &chatDialog::slot_text_msg_received, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_text_msg_self_echo,
            this, &chatDialog::slot_text_msg_self_echo, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_ai_text_msg_received,
            this, &chatDialog::slot_ai_text_msg_received, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_created,
            this, &chatDialog::slot_group_created);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_text_msg_received,
            this, &chatDialog::slot_group_text_msg_received);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_text_msg_rsp,
            this, &chatDialog::slot_group_text_msg_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_group_sync_received,
            this, &chatDialog::slot_group_sync_received);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_stream_start,
            this, &chatDialog::slot_stream_start, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_stream_chunk,
            this, &chatDialog::slot_stream_chunk, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_stream_end,
            this, &chatDialog::slot_stream_end, Qt::QueuedConnection);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_friends_list,
            this, &chatDialog::slot_friends_list_received);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_reconnecting,
            this, &chatDialog::slot_reconnecting);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_reconnected,
            this, &chatDialog::slot_reconnected);
    connect(GroupMgr::GetInstance().get(), &GroupMgr::sig_group_updated,
            this, &chatDialog::slot_group_updated);

    // 个人资料修改回执
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_update_profile_rsp,
            this, &chatDialog::slot_update_profile_rsp);

    // 好友资料卡：删除 / 屏蔽
    connect(_profile_panel, &FriendProfilePanel::sig_delete_friend,
            this, &chatDialog::slot_delete_friend);
    connect(_profile_panel, &FriendProfilePanel::sig_block_friend,
            this, &chatDialog::slot_block_friend);
    // 删除/屏蔽结果
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_delete_friend_rsp,
            this, &chatDialog::slot_delete_friend_rsp);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::sig_block_friend_rsp,
            this, &chatDialog::slot_block_friend_rsp);

    // 图片消息自动下载完成
    connect(FileTransferMgr::GetInstance().get(),
            &FileTransferMgr::sig_image_ready,
            this, &chatDialog::slot_image_ready);

    // ── 右侧工具栏标签初始化 ─────────────────────────────────────────────
    // select 系列�?normal 系列相同 �?点击后不保持 selected 态，每次都弹�?
   // ── 右侧工具栏标签初始化 ─────────────────────────────────────────────
// ── 右侧工具栏标签初始化 ─────────────────────────────────────────────
// ── 右侧工具栏标签初始化 ─────────────────────────────────────────────
ui->emo_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
ui->file_lb->SetState("normal", "hover", "press", "normal", "hover", "press");
ui->video_label->SetState("normal", "hover", "press", "normal", "hover", "press");
ui->audio_label->SetState("normal", "hover", "press", "normal", "hover", "press");
ui->emo_lb->setToolTip("Insert emoji");
ui->file_lb->setToolTip("Send file");
ui->video_label->setToolTip("Start video call");
ui->audio_label->setToolTip("Start voice call");
ui->voice_msg_btn->setToolTip(QString::fromUtf8("发送语音消息"));
ui->voice_msg_btn->setCursor(Qt::PointingHandCursor);

_emoji_picker = new EmojiPickerWidget(this);
connect(_emoji_picker, &EmojiPickerWidget::emojiSelected,
        this, &chatDialog::slot_emoji_clicked);

// 视频通话入口（音视频）
connect(ui->video_label, &ClickedLable::clicked, this, [this]() {
    if (_current_chat_uid == 0) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个聊天对象"));
        return;
    }

    if (_call_dialog || _voice_call_dialog) {
        FocusActiveCallDialog();
        return;
    }

    qDebug() << "[UI] 发起音视频通话, to uid=" << _current_chat_uid;

    ShowVideoCallDialog(ResolveCallPeerName(_current_chat_uid));
    _call_dialog->ApplyCallState(CallSessionMgr::CallState::Ringing, true);

    // 音视频模式：需要本地视频预览和远端视频渲染
    WebRTCMgr::GetInstance().startLocalVideo(_call_dialog->GetLocalVideoSink());
    WebRTCMgr::GetInstance().setRemoteSink(_call_dialog->GetRemoteVideoSink());
    WebRTCMgr::GetInstance().StartCall(_current_chat_uid, true);
});

// 语音通话入口（仅音频）
connect(ui->audio_label, &ClickedLable::clicked, this, [this]() {
    if (_current_chat_uid == 0) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个聊天对象"));
        return;
    }

    if (_call_dialog || _voice_call_dialog) {
        FocusActiveCallDialog();
        return;
    }

    qDebug() << "[UI] 发起语音通话, to uid=" << _current_chat_uid;

    ShowVoiceCallDialog(ResolveCallPeerName(_current_chat_uid));
    _voice_call_dialog->ApplyCallState(CallSessionMgr::CallState::Ringing, true);

    // 纯语音：不启动本地摄像头、不绑定远端视频
    WebRTCMgr::GetInstance().StartCall(_current_chat_uid, false);
});

// 表情按钮
connect(ui->voice_msg_btn, &QPushButton::clicked, this, [this]() {
    if (!_is_recording_voice && CurrentConversationKey().isEmpty()) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个聊天会话"));
        return;
    }

    slot_toggle_record_voice();
});

connect(ui->emo_lb, &ClickedLable::clicked, this, [this]() {
    if (_emoji_picker->isVisible()) {
        _emoji_picker->hide();
        return;
    }
    QPoint gp = ui->emo_lb->mapToGlobal(QPoint(0, 0));
    _emoji_picker->showAbove(gp);
});

// 收到来电（新签名）
connect(&WebRTCMgr::GetInstance(), &WebRTCMgr::sig_incomingCall,
        this, [this](int from_uid, bool withVideo) {
    qDebug() << "[UI] 收到来电 from uid=" << from_uid << ", withVideo=" << withVideo;

    QString title = QString::fromUtf8("来电");
    QString text = withVideo
        ? QString::fromUtf8("收到音视频来电，是否接听？")
        : QString::fromUtf8("收到语音来电，是否接听？");

    auto btn = QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) {
        WebRTCMgr::GetInstance().RejectOrHangUp(); // 拒绝也通知对端
        return;
    }

    if (withVideo) {
        ShowVideoCallDialog(ResolveCallPeerName(from_uid));
        _call_dialog->ApplyCallState(CallSessionMgr::CallState::Ringing, false);
        WebRTCMgr::GetInstance().startLocalVideo(_call_dialog->GetLocalVideoSink());
        WebRTCMgr::GetInstance().setRemoteSink(_call_dialog->GetRemoteVideoSink());
    } else {
        ShowVoiceCallDialog(ResolveCallPeerName(from_uid));
        _voice_call_dialog->ApplyCallState(CallSessionMgr::CallState::Ringing, false);
    }

    WebRTCMgr::GetInstance().acceptCall();
});

// 通话建立成功
connect(CallSessionMgr::GetInstance().get(), &CallSessionMgr::sig_call_state_changed,
        this, [this](CallSessionMgr::CallState state, int peerUid, bool withVideo, bool outgoing) {
    const QString peer_name = ResolveCallPeerName(peerUid);
    if (withVideo && _call_dialog) {
        _call_dialog->SetPeerName(peer_name);
        _call_dialog->ApplyCallState(state, outgoing);
    }
    if (!withVideo && _voice_call_dialog) {
        _voice_call_dialog->SetPeerName(peer_name);
        _voice_call_dialog->ApplyCallState(state, outgoing);
    }
});

connect(&WebRTCMgr::GetInstance(), &WebRTCMgr::sig_callConnected,
        this, [this]() {
    qDebug() << "[UI] 通话连接成功";
});

// 通话结束
connect(&WebRTCMgr::GetInstance(), &WebRTCMgr::sig_callEnded,
        this, [this]() {
    qDebug() << "[UI] 通话结束";
});
_voice_player = new QMediaPlayer(this);
_voice_audio_output = new QAudioOutput(this);
_voice_player->setAudioOutput(_voice_audio_output);

// 播放状态变化：结束后恢复按钮文案
connect(_voice_player, &QMediaPlayer::playbackStateChanged, this,
        [this](QMediaPlayer::PlaybackState st) {
    if (!_playing_voice_btn) return;

    int total = _voice_btn_duration_sec.value(_playing_voice_btn, 1);
    if (st != QMediaPlayer::PlayingState) {
        _playing_voice_btn->setText(QString::fromUtf8("▶ 语音 %1s").arg(total));
        _playing_voice_btn = nullptr;
        _playing_voice_path.clear();
    } else {
        _playing_voice_btn->setText(QString::fromUtf8("■ 00:00 / %1s").arg(total));
    }
});

// 播放进度变化：实时更新时间
connect(_voice_player, &QMediaPlayer::positionChanged, this,
        [this](qint64 posMs) {
    if (!_playing_voice_btn) return;

    int total = _voice_btn_duration_sec.value(_playing_voice_btn, 1);
    int curSec = static_cast<int>(posMs / 1000);
    _playing_voice_btn->setText(QString::fromUtf8("■ %1:%2 / %3s")
        .arg(curSec / 60, 2, 10, QLatin1Char('0'))
        .arg(curSec % 60, 2, 10, QLatin1Char('0'))
        .arg(total));
});
connect(_voice_player, &QMediaPlayer::playbackStateChanged, this,
        [this](QMediaPlayer::PlaybackState st) {
    if (!_playing_voice_btn) {
        return;
    }

    QPushButton *activeBtn = _playing_voice_btn;
    const int total = _voice_btn_duration_sec.value(activeBtn, 1);
    if (st != QMediaPlayer::PlayingState) {
        UpdateVoiceButtonVisual(activeBtn, total, "idle");
        _playing_voice_btn = nullptr;
        _playing_voice_path.clear();
    } else {
        UpdateVoiceButtonVisual(activeBtn, total, "playing", 0);
    }
});
connect(_voice_player, &QMediaPlayer::positionChanged, this,
        [this](qint64 posMs) {
    if (!_playing_voice_btn) {
        return;
    }

    const int total = _voice_btn_duration_sec.value(_playing_voice_btn, 1);
    const int curSec = static_cast<int>(posMs / 1000);
    UpdateVoiceButtonVisual(_playing_voice_btn, total, "playing", curSec);
});
connect(FileTransferMgr::GetInstance().get(),&FileTransferMgr::sig_voice_ready,this, &chatDialog::slot_voice_ready);
    // ── 消息区初始化 ────────────────────────────────────────────────────
    _msg_host_layout = qobject_cast<QVBoxLayout*>(ui->msg_scroll_content->layout());
    Q_ASSERT(_msg_host_layout);

    while (_msg_host_layout->count() > 0) {
        QLayoutItem *item = _msg_host_layout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }

    _msg_stack = new QStackedWidget(ui->msg_scroll_content);
    _msg_stack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _msg_host_layout->addWidget(_msg_stack);

    _empty_msg_page = new QWidget(_msg_stack);
    auto *emptyLayout = new QVBoxLayout(_empty_msg_page);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->addStretch();
    _msg_stack->addWidget(_empty_msg_page);
    _msg_stack->setCurrentWidget(_empty_msg_page);
    _msg_layout = nullptr;
    _stream_flush_timer = new QTimer(this);
    _stream_flush_timer->setSingleShot(true);
    connect(_stream_flush_timer, &QTimer::timeout,
            this, &chatDialog::FlushStreamChunks);

    // 自己的头像（圆形�?
    _self_avatar = ChatBubbleItem::makeRoundAvatar(QPixmap(":/res/head_1.jpg"), 40);
    _peer_avatar = ChatBubbleItem::makeRoundAvatar(QPixmap(":/res/head_2.jpg"), 40);

    // 发送按�?
    connect(ui->pushButton_3, &QPushButton::clicked, this, &chatDialog::SendMessage);
    ui->pushButton_3->setCursor(Qt::PointingHandCursor);
    ui->pushButton_2->hide();
    if (ui->horizontalLayout_6 && ui->horizontalSpacer_8) {
        ui->horizontalLayout_6->removeItem(ui->horizontalSpacer_8);
        delete ui->horizontalSpacer_8;
        ui->horizontalSpacer_8 = nullptr;
    }

    // 搜索按钮 / 输入框回�?�?执行搜索
    connect(ui->search_btn, &QPushButton::clicked, this, [this]() {
        ShowSearch(true);   // 确保已切换到搜索�?
        DoSearch(ui->search_edit->text().trimmed());
    });
    connect(ui->search_edit, &QLineEdit::returnPressed, this, [this]() {
        DoSearch(ui->search_edit->text().trimmed());
    });

    // 输入�?Ctrl+Enter 也能发送（Enter 换行，Ctrl+Enter 发送）
    ui->input_edit->installEventFilter(this);

    // 搜索 HTTP 响应 �?更新搜索列表
    connect(HttpMgr::GetInstance().get(), &HttpMgr::sig_search_mod_finish,
            this, &chatDialog::slot_search_finished);
    // ──────────────────────────────────────────────────────────────────────

    // 发送文件按�?
    connect(ui->file_lb, &ClickedLable::clicked, this, [this]() {
        if (_current_chat_uid == 0) {
            QMessageBox::information(this,
                QString::fromUtf8("提示"),
                QString::fromUtf8("请先选择一个聊天对象"));
            return;
        }
        QString path = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("选择要发送的文件"),
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
        if (path.isEmpty()) return;

        // 生成唯一占位 URL，存入待发送表
        QString uid = QString("file-pending://%1").arg(
                          QDateTime::currentMSecsSinceEpoch());
        _pending_files[uid] = path;

        // 把 file_Bubble.jpg 注册为该 URL 的资源
        QImage bubbleImg(":/res/file_Bubble.jpg");
        ui->input_edit->document()->addResource(
            QTextDocument::ImageResource, QUrl(uid),
            bubbleImg.scaled(160, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));

        // 插入固定尺寸的气泡图到输入框
        QTextImageFormat imgFmt;
        imgFmt.setName(uid);
        imgFmt.setWidth(160);
        imgFmt.setHeight(60);
        QTextCursor cursor = ui->input_edit->textCursor();
        cursor.insertImage(imgFmt);
        ui->input_edit->setTextCursor(cursor);
        ui->input_edit->setFocus();
    });

    // 文件传输进度 / 完成 / 新文件通知
    connect(FileTransferMgr::GetInstance().get(),
            &FileTransferMgr::sig_upload_progress,
            this, [this](QString fileId, int sent, int total) {
        Q_UNUSED(fileId)
        qDebug() << "[File] Upload progress" << sent << "/" << total;
    });
    // MinIO 发送方成功：显示自身文件气�?
    connect(FileTransferMgr::GetInstance().get(),
            &FileTransferMgr::sig_minio_upload_done,
            this, &chatDialog::slot_minio_upload_done);
    // MinIO 接收方：收到服务端推送的文件通知 �?显示文件气泡
    connect(FileTransferMgr::GetInstance().get(),
            &FileTransferMgr::sig_minio_file_incoming,
            this, &chatDialog::slot_minio_file_incoming);
    // ───────────────────────────────────────────────────────────────────

    QTimer::singleShot(500, this, []() {
        qDebug() << "[系统] 界面已加载，初始化 WebRTC...";
        // Init() 内部由 std::once_flag 保证仅执行一次，
        // 且自行创建 signaling/network/worker 线程，不需要 detach 裸线程。
        WebRTCMgr::GetInstance().Init();
    });
}

chatDialog::~chatDialog()
{
    delete ui;
}

QString chatDialog::ResolveCallPeerName(int uid) const
{
    if (uid <= 0) {
        return QStringLiteral("对方");
    }

    if (_chat_user_list) {
        if (auto *wid = _chat_user_list->GetChatUserWid(uid)) {
            const QString name = wid->GetName();
            if (!name.isEmpty()) {
                return name;
            }
        }
    }

    auto friend_mgr = FriendMgr::GetInstance();
    if (friend_mgr->HasFriend(uid)) {
        const QString name = friend_mgr->GetFriend(uid).name;
        if (!name.isEmpty()) {
            return name;
        }
    }

    if (uid == UserMgr::GetInstance()->GetUid() &&
        !UserMgr::GetInstance()->GetName().isEmpty()) {
        return UserMgr::GetInstance()->GetName();
    }

    return QStringLiteral("用户 %1").arg(uid);
}

bool chatDialog::IsGroupConversationActive() const
{
    return !_current_group_id.isEmpty();
}

QString chatDialog::CurrentConversationKey() const
{
    if (!_current_chat_key.isEmpty()) {
        return _current_chat_key;
    }
    if (_current_chat_uid > 0) {
        return MakePeerConversationKey(_current_chat_uid);
    }
    if (!_current_group_id.isEmpty()) {
        return MakeGroupConversationKey(_current_group_id);
    }
    return QString();
}

QString chatDialog::ResolveConversationName(const QString &conversationKey) const
{
    if (_chat_user_list) {
        if (auto *wid = _chat_user_list->GetChatUserWid(conversationKey)) {
            if (!wid->GetName().isEmpty()) {
                return wid->GetName();
            }
        }
    }

    if (IsGroupConversationKey(conversationKey)) {
        return GroupMgr::GetInstance()->GetGroupName(
            ExtractGroupIdFromConversationKey(conversationKey));
    }

    return ResolveCallPeerName(ExtractPeerUidFromConversationKey(conversationKey));
}

QString chatDialog::FormatDisplayText(const MessageInfo &msg) const
{
    if (msg.type == MessageInfo::Voice) {
        QString voicePath;
        int durationMs = 0;
        return ParseVoiceMessagePayload(msg.content, &voicePath, &durationMs)
            ? VoicePreviewText(durationMs)
            : QString::fromUtf8("[语音]");
    }

    if (msg.conversationType != ConversationType::Group || msg.isSelf) {
        return msg.content;
    }

    const QString senderName = GroupMgr::GetInstance()->ResolveMemberName(msg.groupId, msg.fromUid);
    return senderName.isEmpty() ? msg.content : QString("%1:\n%2").arg(senderName, msg.content);
}

QPixmap chatDialog::ResolveMessageAvatar(const MessageInfo &msg) const
{
    if (msg.isSelf) {
        return _self_avatar;
    }

    const int senderUid = msg.conversationType == ConversationType::Group
        ? msg.fromUid
        : ExtractPeerUidFromConversationKey(msg.conversationKey);

    if (senderUid == UserMgr::GetInstance()->GetUid()) {
        return _self_avatar;
    }

    auto friendMgr = FriendMgr::GetInstance();
    if (friendMgr->HasFriend(senderUid)) {
        const QString avatarPath = friendMgr->GetFriend(senderUid).avatarPath;
        if (!avatarPath.isEmpty()) {
            return ChatBubbleItem::makeRoundAvatar(QPixmap(avatarPath), 40);
        }
    }

    return ChatBubbleItem::makeRoundAvatar(
        QPixmap(QString(":/res/head_%1.jpg").arg((senderUid % 5) + 1)), 40);
}

QWidget *chatDialog::CurrentMessageHost() const
{
    if (_msg_stack && _msg_stack->currentWidget()) {
        return _msg_stack->currentWidget();
    }
    return ui->msg_scroll_content;
}

void chatDialog::ScrollCurrentConversationToBottom()
{
    QWidget *host = CurrentMessageHost();
    if (host) {
        if (auto *layout = host->layout()) {
            layout->activate();
        }
        host->adjustSize();
        host->updateGeometry();
        host->update();
    }

    if (ui->msg_scroll_content) {
        if (auto *layout = ui->msg_scroll_content->layout()) {
            layout->activate();
        }
        ui->msg_scroll_content->adjustSize();
        ui->msg_scroll_content->updateGeometry();
    }

    if (_msg_stack) {
        _msg_stack->adjustSize();
        _msg_stack->updateGeometry();
    }

    if (auto *sb = ui->msg_scroll_area->verticalScrollBar()) {
        sb->setValue(sb->maximum());
    }
}

void chatDialog::ScheduleScrollToBottom()
{
    for (const int delayMs : {0, 16, 48}) {
        QTimer::singleShot(delayMs, this, [this]() {
            ScrollCurrentConversationToBottom();
        });
    }
}

QWidget *chatDialog::EnsureConversationPage(const QString &conversationKey)
{
    if (conversationKey.isEmpty()) {
        return nullptr;
    }

    QWidget *page = _conversation_pages.value(conversationKey, nullptr);
    if (page) {
        return page;
    }

    page = new QWidget(_msg_stack);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addItem(
        new QSpacerItem(20, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    _msg_stack->addWidget(page);
    _conversation_pages.insert(conversationKey, page);
    _conversation_layouts.insert(conversationKey, layout);
    _dirty_conversations.insert(conversationKey);
    return page;
}

QVBoxLayout *chatDialog::EnsureConversationLayout(const QString &conversationKey)
{
    EnsureConversationPage(conversationKey);
    return _conversation_layouts.value(conversationKey, nullptr);
}

void chatDialog::ClearConversationPage(const QString &conversationKey)
{
    QVBoxLayout *layout = EnsureConversationLayout(conversationKey);
    QWidget *page = _conversation_pages.value(conversationKey, nullptr);
    if (!layout || !page) {
        return;
    }

    page->setUpdatesEnabled(false);
    while (layout->count() > 0) {
        QLayoutItem *item = layout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    layout->addItem(
        new QSpacerItem(20, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));
    page->setUpdatesEnabled(true);
}

void chatDialog::RebuildConversationPage(const QString &conversationKey)
{
    QWidget *page = EnsureConversationPage(conversationKey);
    QVBoxLayout *layout = EnsureConversationLayout(conversationKey);
    if (!page || !layout) {
        return;
    }

    ClearConversationPage(conversationKey);
    page->setUpdatesEnabled(false);
    const QVector<MessageInfo> &history = ChatMgr::GetInstance()->GetHistory(conversationKey);
    for (const MessageInfo &msg : history) {
        RenderConversationMessageInto(msg, layout, page, false);
    }
    page->setUpdatesEnabled(true);
    page->update();
    _dirty_conversations.remove(conversationKey);
}

void chatDialog::MarkConversationDirty(const QString &conversationKey)
{
    if (!conversationKey.isEmpty()) {
        _dirty_conversations.insert(conversationKey);
    }
}

void chatDialog::RenderConversationMessageInto(const MessageInfo &msg,
                                               QVBoxLayout *layout,
                                               QWidget *host,
                                               bool autoScroll)
{
    if (!layout || !host) {
        return;
    }

    if (msg.type == MessageInfo::Voice) {
        QString voicePath;
        int durationMs = 0;
        if (!ParseVoiceMessagePayload(msg.content, &voicePath, &durationMs)) {
            auto *bubble = new ChatBubbleItem(
                QString::fromUtf8("[语音消息不可用]"),
                ResolveMessageAvatar(msg),
                msg.isSelf,
                msg.time.isValid() ? msg.time.toString("hh:mm") : QString(),
                host);
            layout->addWidget(bubble);
        } else {
            const int totalSec = qMax(1, durationMs / 1000);
            auto *btn = new QPushButton(FormatVoiceIdleLabel(totalSec), host);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setMinimumHeight(38);
            btn->setMaximumHeight(38);
            btn->setMinimumWidth(VoiceBubbleWidth(totalSec));
            btn->setMaximumWidth(220);
            btn->setProperty("voiceBubble", "true");
            btn->setProperty("bubbleRole", msg.isSelf ? "self" : "peer");
            btn->setProperty("voiceState", "idle");
            btn->setStyleSheet("");

            _voice_btn_duration_sec[btn] = totalSec;
            UpdateVoiceButtonVisual(btn, totalSec, "idle");

            connect(btn, &QPushButton::clicked, this, [this, btn, voicePath, totalSec]() {
                if (!_voice_player) {
                    return;
                }

                if (_playing_voice_btn == btn &&
                    _voice_player->playbackState() == QMediaPlayer::PlayingState) {
                    _voice_player->stop();
                    _playing_voice_btn = nullptr;
                    _playing_voice_path.clear();
                    UpdateVoiceButtonVisual(btn, totalSec, "idle");
                    return;
                }

                if (_playing_voice_btn && _playing_voice_btn != btn) {
                    const int oldDur = _voice_btn_duration_sec.value(_playing_voice_btn, 1);
                    UpdateVoiceButtonVisual(_playing_voice_btn, oldDur, "idle");
                }

                _voice_player->stop();
                _playing_voice_btn = btn;
                _playing_voice_path = voicePath;
                _voice_player->setSource(QUrl::fromLocalFile(voicePath));
                _voice_player->play();
                UpdateVoiceButtonVisual(btn, totalSec, "playing", 0);
            });

            auto *row = new QWidget(host);
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(12, 6, 12, 6);
            h->setSpacing(8);

            auto *avatarLb = new QLabel(row);
            avatarLb->setFixedSize(40, 40);
            avatarLb->setPixmap(ResolveMessageAvatar(msg));

            auto *content = new QVBoxLayout();
            content->setContentsMargins(0, 0, 0, 0);
            content->setSpacing(3);

            if (msg.time.isValid()) {
                auto *timeLb = new QLabel(msg.time.toString("hh:mm"), row);
                timeLb->setObjectName("bubble_time_lb");
                timeLb->setAlignment(msg.isSelf ? Qt::AlignRight : Qt::AlignLeft);
                content->addWidget(timeLb);
            }

            content->addWidget(btn, 0, msg.isSelf ? Qt::AlignRight : Qt::AlignLeft);

            if (msg.isSelf) {
                h->addStretch();
                h->addLayout(content);
                h->addWidget(avatarLb);
            } else {
                h->addWidget(avatarLb);
                h->addLayout(content);
                h->addStretch();
            }

            layout->addWidget(row);
        }

        if (autoScroll) {
            ScheduleScrollToBottom();
        }
        return;
    }

    auto *bubble = new ChatBubbleItem(
        FormatDisplayText(msg),
        ResolveMessageAvatar(msg),
        msg.isSelf,
        msg.time.isValid() ? msg.time.toString("hh:mm") : QString(),
        host);
    layout->addWidget(bubble);

    if (autoScroll) {
        ScheduleScrollToBottom();
    }
}

void chatDialog::RenderConversationMessage(const MessageInfo &msg, bool autoScroll)
{
    RenderConversationMessageInto(msg, _msg_layout, CurrentMessageHost(), autoScroll);
}

void chatDialog::UpdateConversationPreview(const MessageInfo &msg)
{
    if (msg.conversationKey.isEmpty()) {
        return;
    }

    const bool isActiveConversation = (msg.conversationKey == _current_chat_key);
    const QString timeStr = msg.time.isValid()
        ? msg.time.toString("hh:mm")
        : QDateTime::currentDateTime().toString("hh:mm");
    const QString previewText = FormatDisplayText(msg).replace('\n', ' ');
    EnsureConversationEntry(msg.conversationKey, previewText, timeStr);
    _chat_user_list->UpdateLastMsg(msg.conversationKey, previewText, timeStr);
    // Avoid reshuffling QListWidget item widgets for background conversations.
    // The previous MoveToTop path is a high-risk crash point under rapid cross-chat updates.
    if (isActiveConversation || msg.isSelf) {
        _chat_user_list->MoveToTop(msg.conversationKey);
    }
    _chat_user_list->UpdateUnreadCount(
        msg.conversationKey, ChatMgr::GetInstance()->GetUnreadCount(msg.conversationKey));
    if (!isActiveConversation) {
        MarkConversationDirty(msg.conversationKey);
    }
}

void chatDialog::EnsureConversationEntry(const QString &conversationKey,
                                         const QString &previewText,
                                         const QString &timeStr)
{
    if (_chat_user_list->GetChatUserWid(conversationKey)) {
        return;
    }

    QString avatarPath;
    QString name = ResolveConversationName(conversationKey);
    if (IsGroupConversationKey(conversationKey)) {
        const QString groupId = ExtractGroupIdFromConversationKey(conversationKey);
        avatarPath = GroupMgr::GetInstance()->GetGroupAvatarPath(groupId);
        GroupMgr::GetInstance()->EnsureGroupInfo(groupId);
    } else {
        const int uid = ExtractPeerUidFromConversationKey(conversationKey);
        avatarPath = FriendMgr::GetInstance()->HasFriend(uid)
            ? FriendMgr::GetInstance()->GetFriend(uid).avatarPath
            : QString(":/res/head_%1.jpg").arg((uid % 5) + 1);
    }

    _chat_user_list->AddChatUser(
        conversationKey,
        name,
        avatarPath,
        previewText,
        timeStr,
        ChatMgr::GetInstance()->GetUnreadCount(conversationKey));
}

void chatDialog::ShowVideoCallDialog(const QString &peerName)
{
    if (_call_dialog) {
        _call_dialog->SetPeerName(peerName);
        _call_dialog->raise();
        _call_dialog->activateWindow();
        return;
    }

    _call_dialog = new VideoCallDialog(peerName, this);
    _call_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(_call_dialog, &QObject::destroyed, this, [this]() {
        _call_dialog = nullptr;
    });
    _call_dialog->show();
    _call_dialog->raise();
    _call_dialog->activateWindow();
}

void chatDialog::ShowVoiceCallDialog(const QString &peerName)
{
    if (_voice_call_dialog) {
        _voice_call_dialog->SetPeerName(peerName);
        _voice_call_dialog->raise();
        _voice_call_dialog->activateWindow();
        return;
    }

    _voice_call_dialog = new VoiceCallDialog(peerName, this);
    _voice_call_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(_voice_call_dialog, &QObject::destroyed, this, [this]() {
        _voice_call_dialog = nullptr;
    });
    _voice_call_dialog->show();
    _voice_call_dialog->raise();
    _voice_call_dialog->activateWindow();
}

void chatDialog::RequestGroupSync(const QString &groupId)
{
    if (groupId.isEmpty()) {
        return;
    }

    const QString conversationKey = MakeGroupConversationKey(groupId);
    QJsonObject req;
    req["group_id"] = groupId;

    const QString lastMsgId = ChatMgr::GetInstance()->GetLatestServerMsgId(conversationKey);
    if (!lastMsgId.isEmpty()) {
        req["last_msg_id"] = lastMsgId;
    }

    GroupMgr::GetInstance()->EnsureGroupInfo(groupId);
    TcpMgr::GetInstance()->SendMsg(ID_GROUP_SYNC_REQ, req);
}

void chatDialog::RequestKnownGroupSync()
{
    QStringList groupIds;
    const auto conversationKeys = ChatMgr::GetInstance()->GetConversationList();
    for (const QString &conversationKey : conversationKeys) {
        if (!IsGroupConversationKey(conversationKey)) {
            continue;
        }

        const QString groupId = ExtractGroupIdFromConversationKey(conversationKey);
        if (!groupId.isEmpty() && !groupIds.contains(groupId)) {
            groupIds.append(groupId);
        }
    }

    for (const QString &groupId : groupIds) {
        RequestGroupSync(groupId);
    }
}

void chatDialog::FocusActiveCallDialog()
{
    if (_call_dialog) {
        _call_dialog->raise();
        _call_dialog->activateWindow();
        return;
    }
    if (_voice_call_dialog) {
        _voice_call_dialog->raise();
        _voice_call_dialog->activateWindow();
    }
}

bool chatDialog::eventFilter(QObject *watched, QEvent *event)
{
    // 搜索框获得焦�?�?展开搜索列表
    if (watched == ui->search_edit && event->type() == QEvent::FocusIn) {
        slot_search_focus_in();
        return QDialog::eventFilter(watched, event);
    }

    // 左侧头像单击 �?打开个人资料编辑
    if (watched == ui->side_head_lb && event->type() == QEvent::MouseButtonRelease) {
        slot_open_profile_edit();
        return true;
    }

    // Ctrl+Enter 在输入框中发送消�?
    if (watched == ui->input_edit && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Return && (ke->modifiers() & Qt::ControlModifier)) {
            SendMessage();
            return true;
        }
    }

    // 在搜索模式下，鼠标点击到搜索框以外的区域 �?收起搜索列表
    if (_mode == ChatUIMode::SearchMode && event->type() == QEvent::MouseButtonPress) {
        QWidget *clickedWidget = qobject_cast<QWidget*>(watched);
        if (clickedWidget) {
            // 判断点击的控件是否是搜索框或其子控件
            bool insideSearch = false;
            QWidget *w = clickedWidget;
            while (w) {
                if (w == ui->search_edit || w == ui->search_widget_in || w == ui->search_widget) {
                    insideSearch = true;
                    break;
                }
                w = w->parentWidget();
            }
            if (!insideSearch) {
                ui->search_edit->clearFocus();
                ShowSearch(false);
            }
        }
    }

    return QDialog::eventFilter(watched, event);
}

void chatDialog::ShowSearch(bool b_search)
{
    if (b_search) {
        _list_stack->setCurrentIndex(1);
        _mode = ChatUIMode::SearchMode;
    } else if (_state == ChatUIMode::ChatMode) {
        _list_stack->setCurrentIndex(0);
        _mode = ChatUIMode::ChatMode;
    } else if (_state == ChatUIMode::ContactMode) {
        _list_stack->setCurrentIndex(2);
        _mode = ChatUIMode::ContactMode;
    } else {
        _list_stack->setCurrentIndex(0);
        _mode = ChatUIMode::ChatMode;
    }
}

void chatDialog::slot_search_focus_in()
{
    ShowSearch(true);
    // 如果输入框为空，展示提示页而不是空�?
    if (ui->search_edit->text().trimmed().isEmpty()) {
        DoSearch("");
    }
}

void chatDialog::slot_search_focus_out()
{
    if (ui->search_edit->text().isEmpty()) {
        ShowSearch(false);
    }
}

void chatDialog::slot_loading_chat_user()
{
    if (_b_loading) return;
    _b_loading = true;

    QTimer::singleShot(500, this, [this]() {
        AddChatUserList();
        _b_loading = false;
        _chat_user_list->SetLoading(false);
    });
}

void chatDialog::slot_chat_user_clicked(int uid)
{
    slot_chat_user_clicked(MakePeerConversationKey(uid));
}

void chatDialog::slot_chat_user_clicked(const QString &conversationKey)
{
    if (_current_chat_key == conversationKey) {
        return;
    }
    SwitchToChat(conversationKey);
}

void chatDialog::slot_emoji_clicked(const QString& imagePath)
{

    if (_current_chat_uid == 0) return;
    QString key = imagePath;
    QImage img(key);
    if (img.isNull()) return;

    // 注册缩略资源（36x36）
    ui->input_edit->document()->addResource(
        QTextDocument::ImageResource, QUrl(key),
        img.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    // 用 QTextImageFormat 锁定宽高，防止 QTextEdit 把图片拉伸填满整行
    QTextImageFormat imgFmt;
    imgFmt.setName(key);
    imgFmt.setWidth(36);
    imgFmt.setHeight(36);

    QTextCursor cursor = ui->input_edit->textCursor();
    cursor.insertImage(imgFmt);
    ui->input_edit->setTextCursor(cursor);
    ui->input_edit->setFocus();
}



void chatDialog::SendMessage()
{
    QTextDocument *doc = ui->input_edit->document();

    // ── 拆分输入框内容：文本 / 表情 / 待发文件 三种 segment ─────────────
    enum SegType { SEG_TEXT, SEG_EMOJI, SEG_FILE };
    // 【修改点 1】：把原来的 int emoIdx 改成 QString emoPath，直接存真实路径
    struct Seg { SegType type; QString text; QString emoPath; QString pendingUrl; };
    QList<Seg> segs;
    QString curText;

    for (QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            QTextCharFormat fmt = frag.charFormat();

            if (fmt.isImageFormat()) {
                QString imgName = fmt.toImageFormat().name();

                // 【修改点 2】：废弃正则表达式！只要是 ":/res/emo" 开头的，全当做表情包处理
                if (imgName.startsWith(":/res/emo")) {
                    if (!curText.isEmpty()) { segs.append({SEG_TEXT, curText, "", ""}); curText.clear(); }
                    segs.append({SEG_EMOJI, "", imgName, ""});
                }
                else if (imgName.startsWith("file-pending://")) {
                    if (!curText.isEmpty()) { segs.append({SEG_TEXT, curText, "", ""}); curText.clear(); }
                    segs.append({SEG_FILE, "", "", imgName});
                }
            } else {
                curText += frag.text();
            }
        }
        if (b.next() != doc->end()) curText += "\n";
    }
    if (!curText.trimmed().isEmpty()) segs.append({SEG_TEXT, curText, "", ""});
    if (segs.isEmpty()) return;

    ui->input_edit->clear();
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm");

    for (const Seg &seg : segs) {
        if (seg.type == SEG_TEXT) {
            // ── 普通文本 ────────────────────────────────────────────────
            QString t = seg.text.trimmed();
            if (t.isEmpty()) continue;
            AppendMessage(t, _self_avatar, true, timeStr);
            if (!_current_group_id.isEmpty()) {
                QJsonObject msgObj;
                msgObj["group_id"] = _current_group_id;
                msgObj["content"] = t;
                TcpMgr::GetInstance()->SendMsg(ID_GROUP_TEXT_MSG, msgObj);
            } else if (_current_chat_uid != 0) {
                QJsonObject msgObj;
                msgObj["to_uid"]  = _current_chat_uid;
                msgObj["content"] = t;
                TcpMgr::GetInstance()->SendMsg(ID_TEXT_CHAT_MSG, msgObj);
            }

        } else if (seg.type == SEG_EMOJI) {
            // 【修改点 3】：表情包：直接底层文件拷贝，完美保留 GIF/JPG/PNG 原格式 ──────────────
            QString resPath = seg.emoPath;
            QFileInfo fi(resPath);
            if (!fi.exists()) continue;

            // 提取真实的后缀名
            QString ext = fi.suffix();
            if (ext.isEmpty()) ext = "png";

            // 组装临时路径（带上正确后缀）
            QString tmpPath = QString("%1/emo_tmp_%2.%3")
                                  .arg(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                  .arg(QDateTime::currentMSecsSinceEpoch())
                                  .arg(ext);

            // 直接拷贝二进制文件，不重新编码！
            if (QFile::copy(resPath, tmpPath)) {
                QFile::setPermissions(tmpPath, QFile::ReadOwner | QFile::WriteOwner);

                AppendImageMessage(QPixmap(tmpPath), _self_avatar, true, tmpPath, timeStr);

                if (_current_chat_uid != 0) {
                    FileTransferMgr::GetInstance()->SendFileMinio(_current_chat_uid, tmpPath, true);
                }
            } else {
                qDebug() << "[Emoji Send Error] 无法拷贝表情包到临时目录:" << resPath;
            }

        } else if (seg.type == SEG_FILE) {
            // ── 待发文件：查回本地路径，立即显示气泡并上传 ──────────────
            if (!_pending_files.contains(seg.pendingUrl)) continue;
            QString filePath = _pending_files.take(seg.pendingUrl);
            QFileInfo fi(filePath);

            AppendFileMessage(fi.fileName(), fi.size(),
                              /*fileKey=*/QString(),
                              /*downloadUrl=*/QString(),
                              _self_avatar, /*isSelf=*/true, timeStr);

            if (_current_chat_uid != 0) {
                FileTransferMgr::GetInstance()->SendFileMinio(_current_chat_uid, filePath);
            }
        }
    }
    // 清理没有被发出的 pending
    _pending_files.clear();
}

void chatDialog::AppendMessage(const QString &text, const QPixmap &avatar,
                                bool isSelf, const QString &timeStr)
{
    Q_UNUSED(avatar)
    Q_UNUSED(timeStr)
    if (!_msg_layout) {
        return;
    }

    const QString conversationKey = CurrentConversationKey();
    if (conversationKey.isEmpty()) {
        return;
    }

    MessageInfo msg;
    msg.conversationKey = conversationKey;
    msg.time = QDateTime::currentDateTime();
    msg.content = text;
    msg.isSelf = isSelf;
    msg.type = MessageInfo::Text;

    const int selfUid = UserMgr::GetInstance()->GetUid();
    if (IsGroupConversationActive()) {
        msg.conversationType = ConversationType::Group;
        msg.groupId = _current_group_id;
        msg.fromUid = isSelf ? selfUid : 0;
        msg.toUid = 0;
    } else {
        msg.conversationType = ConversationType::Peer;
        msg.fromUid = isSelf ? selfUid : _current_chat_uid;
        msg.toUid = isSelf ? _current_chat_uid : selfUid;
    }

    qDebug() << "[ChatUI] Append local text message conversation=" << conversationKey
             << "isSelf=" << isSelf
             << "from=" << msg.fromUid
             << "to=" << msg.toUid;
    if (!ChatMgr::GetInstance()->AppendMessage(msg)) {
        qWarning() << "[ChatUI] Append local text message rejected conversation="
                   << conversationKey;
        return;
    }
    qDebug() << "[ChatUI] Local text persisted conversation=" << conversationKey;
    UpdateConversationPreview(msg);
    qDebug() << "[ChatUI] Local preview updated conversation=" << conversationKey;
    RenderConversationMessage(msg);
    qDebug() << "[ChatUI] Local message rendered conversation=" << conversationKey;
}

void chatDialog::SaveCurrentMessages()
{
    // 消息已在 AppendMessage 时通过 ChatMgr 实时持久化到 SQLite，此处无需额外操作
}

void chatDialog::SwitchToChat(int uid)
{
    SwitchToChat(MakePeerConversationKey(uid));
}

void chatDialog::SwitchToChat(const QString &conversationKey)
{
    if (!IsValidConversationKey(conversationKey)) {
        qWarning() << "[ChatUI] Ignore invalid conversation key:" << conversationKey;
        return;
    }

    ChatUserWid *wid = _chat_user_list->GetChatUserWid(conversationKey);
    if (!wid) {
        return;
    }

    _current_chat_key = conversationKey;
    _chat_user_list->SetSelectChatUser(conversationKey);
    if (IsGroupConversationKey(conversationKey)) {
        _current_group_id = ExtractGroupIdFromConversationKey(conversationKey);
        _current_chat_uid = 0;
        GroupMgr::GetInstance()->EnsureGroupInfo(_current_group_id);
    } else {
        _current_group_id.clear();
        _current_chat_uid = ExtractPeerUidFromConversationKey(conversationKey);
    }

    ui->titel_lb->setText(wid->GetName());

    const QString avatarPath = wid->GetAvatarPath();
    _peer_avatar = ChatBubbleItem::makeRoundAvatar(
        avatarPath.isEmpty() ? QPixmap(":/res/head_2.jpg") : QPixmap(avatarPath), 40);

    QWidget *page = EnsureConversationPage(conversationKey);
    if (_dirty_conversations.contains(conversationKey)) {
        RebuildConversationPage(conversationKey);
    }
    if (_msg_stack && page) {
        _msg_stack->setCurrentWidget(page);
    }
    _msg_layout = EnsureConversationLayout(conversationKey);
    if (page) {
        page->adjustSize();
    }
    ui->msg_scroll_content->adjustSize();

    ui->input_edit->clear();

    ScheduleScrollToBottom();

    ChatMgr::GetInstance()->SetActiveConversation(conversationKey);
    ChatMgr::GetInstance()->MarkAsRead(conversationKey);
    _chat_user_list->UpdateUnreadCount(conversationKey, 0);
}

void chatDialog::LoadLocalConversations()
{
    const auto keys = ChatMgr::GetInstance()->GetConversationList();
    for (const QString &conversationKey : keys) {
        if (!IsValidConversationKey(conversationKey)) {
            continue;
        }
        const auto &history = ChatMgr::GetInstance()->GetHistory(conversationKey);
        if (history.isEmpty()) {
            continue;
        }

        const MessageInfo &lastMsg = history.last();
        const QString timeStr = lastMsg.time.isValid()
            ? lastMsg.time.toString("hh:mm")
            : QString();
        const QString previewText = FormatDisplayText(lastMsg).replace('\n', ' ');
        EnsureConversationEntry(conversationKey, previewText, timeStr);
        _chat_user_list->UpdateLastMsg(conversationKey, previewText, timeStr);
    }
}

void chatDialog::LoadTestData()
{
    _chat_user_list->AddChatUser(10001, "张三",  ":/res/head_1.jpg", "你好啊，最近怎么样？", "09:30", 2);
    _chat_user_list->AddChatUser(10002, "李四",  ":/res/head_2.jpg", "晚上一起吃饭吧",       "昨天",  0);
    _chat_user_list->AddChatUser(10003, "王五",  ":/res/head_3.jpg", "收到，谢谢",           "12:15", 0);
    _chat_user_list->AddChatUser(10004, "赵六",  ":/res/head_4.jpg", "好的，没问题",         "10:20", 5);
    _chat_user_list->AddChatUser(10005, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);
    _chat_user_list->AddChatUser(10006, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);
    _chat_user_list->AddChatUser(10007, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);
    _chat_user_list->AddChatUser(10008, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);
    _chat_user_list->AddChatUser(10009, "赵六",  ":/res/head_4.jpg", "好的，没问题",         "10:20", 5);
    _chat_user_list->AddChatUser(10010, "赵六",  ":/res/head_4.jpg", "好的，没问题",         "10:20", 5);
    _chat_user_list->AddChatUser(10011, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);
    _chat_user_list->AddChatUser(10012, "孙七",  ":/res/head_5.jpg", "哈哈哈哈",             "08:45", 0);

    // ── 好友列表测试数据 ────────────────────────────────────
    auto addF = [this](int uid, const QString &name, const QString &av, const QString &sig) {
        FriendInfo fi;
        fi.uid        = uid;
        fi.name       = name;
        fi.avatarPath = av;
        fi.signature  = sig;
        FriendMgr::GetInstance()->AddFriend(fi);  // 写入 FriendMgr
        _contact_panel->AddFriend(fi);
    };
    addF(10001, QString::fromUtf8("张三"),   ":/res/head_1.jpg", QString::fromUtf8("生活不止，奉献不止"));
    addF(10002, QString::fromUtf8("李四"),   ":/res/head_2.jpg", QString::fromUtf8("今天天气真不错"));
    addF(10003, QString::fromUtf8("王五"),   ":/res/head_3.jpg", QString::fromUtf8("努力就会有回报"));
    addF(10004, QString::fromUtf8("赵小燕"), ":/res/head_4.jpg", QString::fromUtf8("微笑是最美的爪印"));
    addF(10005, QString::fromUtf8("小红"),   ":/res/head_5.jpg", QString::fromUtf8("努力当一个对社会有用的人"));
    addF(20001, QString::fromUtf8("小明"),   ":/res/head_3.jpg", "");
    addF(20002, QString::fromUtf8("开朗的王五"), ":/res/head_2.jpg", QString::fromUtf8("天无二日，美无再来"));

    // ── 测试好友申请数据 ────────────────────────────────────────────────
    auto makeReq = [](int uid, const QString &name, const QString &msg,
                      const QString &avatar, int minsAgo) -> FriendRequest {
        FriendRequest r;
        r.uid        = uid;
        r.name       = name;
        r.message    = msg;
        r.avatarPath = avatar;
        r.time       = QDateTime::currentDateTime().addSecs(-minsAgo * 60);
        r.status     = FriendRequest::Pending;
        return r;
    };

    _verify_panel->AddRequest(makeReq(20001, QString::fromUtf8("小红"),
        QString::fromUtf8("我是小红，加个好友吧！"),   ":/res/head_3.jpg",  2));
    _verify_panel->AddRequest(makeReq(20002, QString::fromUtf8("小明"),
        QString::fromUtf8("同学，你好~"),               ":/res/head_4.jpg",  30));
    _verify_panel->AddRequest(makeReq(20003, QString::fromUtf8("开朗的王五"),
        QString::fromUtf8(""),                           ":/res/head_2.jpg",  120));
    _verify_panel->AddRequest(makeReq(20004, QString::fromUtf8("赵小燕"),
        QString::fromUtf8("我们在同一个群，认识一下"),   ":/res/head_5.jpg",  1440));

    _current_page = 1;
}

void chatDialog::AddChatUserList()
{
    _current_page++;
    if (_current_page <= 3) {
        // 在添加之前记录当前滚动位�?
        int savedPos = _chat_user_list->verticalScrollBar()->value();

        for (int i = 0; i < 5; i++) {
            int uid = 10000 + _current_page * 10 + i;
            QString name   = QString("用户%1").arg(uid);
            QString avatar = QString(":/res/head_%1.jpg").arg((i % 5) + 1);
            _chat_user_list->AddChatUser(uid, name, avatar, "这是一条测试消息", "7天前", 0);
        }
        qDebug() << "Loaded page" << _current_page;

        // 延迟�?doItemsLayout 完成后再恢复位置
        QTimer::singleShot(0, this, [this, savedPos]() {
            _chat_user_list->verticalScrollBar()->setValue(savedPos);
        });
    } else {
        _chat_user_list->SetHasMoreData(false);
        qDebug() << "No more data";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 发起搜索：向 GateServer 发�?HTTP POST /search_user
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::DoSearch(const QString &keyword)
{
    // 清空旧结果（index 0 = hint，last = stretch，删除中间项�?
    while (_search_layout->count() > 2) {
        QLayoutItem *li = _search_layout->takeAt(1);
        if (li->widget()) li->widget()->deleteLater();
        delete li;
    }

    if (keyword.trimmed().isEmpty()) {
        _search_hint->setVisible(true);
        return;
    }

    _search_hint->setVisible(false);

    // 显示 loading 文字（等待服务器响应时的占位�?
    QLabel *loadingLb = new QLabel(QString::fromUtf8("搜索中..."));
    loadingLb->setObjectName("search_loading_lb");
    loadingLb->setAlignment(Qt::AlignCenter);
    loadingLb->setStyleSheet("color:#aaa; font-size:13px; font-family:'Microsoft YaHei';");
    loadingLb->setFixedHeight(60);
    _search_layout->insertWidget(1, loadingLb);

    // �?GateServer 发送搜索请求（携带 uid + token 供服务端鉴权�?
    QJsonObject req;
    req["uid"]     = TcpMgr::GetInstance()->GetUid();
    req["token"]   = TcpMgr::GetInstance()->GetToken();
    req["keyword"] = keyword;
    HttpMgr::GetInstance()->PostHttpReq(
        QUrl(gate_url_prefix + "/search_user"),
        req,
        ID_SEARCH_USER,
        Modules::SEARCHMOD);
}

// HTTP 搜索结果回调
void chatDialog::slot_search_finished(REQ_ID req_id, QString res, ErrorCodes err)
{
    if (req_id != ID_SEARCH_USER) return;

    // 清除 loading 占位
    while (_search_layout->count() > 2) {
        QLayoutItem *li = _search_layout->takeAt(1);
        if (li->widget()) li->widget()->deleteLater();
        delete li;
    }

    auto addLabel = [this](const QString &text) {
        QLabel *lb = new QLabel(text);
        lb->setAlignment(Qt::AlignCenter);
        lb->setStyleSheet("color:#999; font-size:13px; font-family:'Microsoft YaHei';");
        lb->setFixedHeight(60);
        _search_layout->insertWidget(1, lb);
    };

    if (err != ErrorCodes::Success) {
        addLabel(QString::fromUtf8("网络错误，请检查连接"));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(res.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        addLabel(QString::fromUtf8("服务器响应异常"));
        return;
    }
    QJsonObject obj = doc.object();
    int server_err = obj["error"].toInt();

    if (server_err == ErrorCodes::TokenInvalid) {
        addLabel(QString::fromUtf8("登录状态已过期，请重新登录"));
        return;
    }

    if (server_err == ErrorCodes::SearchNotFound || !obj.contains("users")) {
        addLabel(QString::fromUtf8("未搜索到用户"));
        return;
    }
    if (server_err != ErrorCodes::Success) {
        addLabel(QString::fromUtf8("搜索失败（错误码：%1）").arg(server_err));
        return;
    }

    QJsonArray users = obj["users"].toArray();
    if (users.isEmpty()) {
        addLabel(QString::fromUtf8("未搜索到用户"));
        return;
    }

    int insertPos = 1;
    for (const auto &val : users) {
        QJsonObject u = val.toObject();
        int     uid  = u["uid"].toInt();
        QString name = u["name"].toString();
        // 服务端目前不返回 wechatId，用 uid 字符串代�?
        QString wechatId = "wxid_" + QString::number(uid);
        // avatar 根据 uid % 5 选一张本地头像（后续可扩展为网络地址�?
        QString avatar = QString(":/res/head_%1.jpg").arg((uid % 5) + 1);

        auto *item = new SearchUserItem(uid, name, wechatId, avatar,
                                        SearchUserItem::State::Normal, this);
        connect(item, &SearchUserItem::sig_add_clicked,
                this, &chatDialog::slot_add_friend_clicked);
        _search_layout->insertWidget(insertPos++, item);
    }
}

void chatDialog::slot_add_friend_clicked(int uid, const QString &name, const QString &avatarPath)
{
    AddFriendWidget *dlg = new AddFriendWidget(uid, name, avatarPath, this);
    connect(dlg, &AddFriendWidget::sig_send_request, this, [this](int to_uid, const QString &msg) {
        QJsonObject req;
        req["to_uid"]  = to_uid;
        req["message"] = msg;
        TcpMgr::GetInstance()->SendMsg(ID_ADD_FRIEND_REQ, req);
        qDebug() << "[AddFriend] request sent to uid=" << to_uid;
    });
    dlg->exec();
}

// ─────────────────────────────────────────────────────────────────────────────
// 被动收到好友申请通知 �?加入验证面板，更新角�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_notify_add_friend(int from_uid, QString from_name, QString message)
{
    FriendRequest req;
    req.uid      = from_uid;
    req.name     = from_name;
    req.message  = message;
    req.time     = QDateTime::currentDateTime();
    req.avatarPath = QString(":/res/head_%1.jpg").arg((from_uid % 5) + 1);
    req.status   = FriendRequest::Pending;

    // 更新 FriendMgr
    FriendMgr::GetInstance()->AddPendingRequest(req);

    // 更新验证面板
    _verify_panel->AddRequest(req);
}

void chatDialog::slot_switch_contact_mode()
{
    // 收起搜索�?
    if (_mode == ChatUIMode::SearchMode) {
        ui->search_edit->clearFocus();
        ui->search_edit->clear();
    }
    _state = ChatUIMode::ContactMode;
    _mode  = ChatUIMode::ContactMode;
    _list_stack->setCurrentIndex(2);     // 左侧显示联系人面�?
    _right_stack->setCurrentIndex(0);    // 右侧默认仍为聊天（等待点�?新的朋友"�?
    ui->search_widget->hide();           // 联系人模式不显示搜索�?
}

void chatDialog::slot_switch_chat_mode()
{
    _state = ChatUIMode::ChatMode;
    _mode  = ChatUIMode::ChatMode;
    _list_stack->setCurrentIndex(0);     // 左侧聊天列表
    _right_stack->setCurrentIndex(0);    // 右侧聊天面板
    ui->search_widget->show();           // 恢复搜索�?
}

void chatDialog::slot_friend_req_accepted(int uid)
{
    qDebug() << "[Contact] accepted friend request from uid=" << uid;

    // 更新 FriendMgr 状�?
    FriendMgr::GetInstance()->AcceptRequest(uid);

    // 发�?TCP 消息告知服务端（服务端写�?+ 回馈 ID_ACCEPT_FRIEND_RSP�?
    QJsonObject req;
    req["peer_uid"] = uid;
    TcpMgr::GetInstance()->SendMsg(ID_ACCEPT_FRIEND_REQ, req);
    // 不在此处直接更新 UI：等�?slot_accept_friend_rsp 回调（服务端确认后）
}

void chatDialog::slot_friend_req_rejected(int uid)
{
    qDebug() << "[Contact] rejected friend request from uid=" << uid;
    FriendMgr::GetInstance()->RejectRequest(uid);
    // 可选：向服务端发送拒绝通知（暂 TODO�?
}

// ─────────────────────────────────────────────────────────────────────────────
// 己方同意后服务端确认（写库成功）�?把对方加入联系人列表 + 可以立即发消�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_accept_friend_rsp(int friend_uid, const QString &friend_name)
{
    qDebug() << "[Contact] AcceptFriend confirmed: uid=" << friend_uid
             << " name=" << friend_name;

    // 写入 FriendMgr
    FriendInfo fi;
    fi.uid        = friend_uid;
    fi.name       = friend_name;
    fi.avatarPath = QString(":/res/head_%1.jpg").arg((friend_uid % 5) + 1);
    fi.signature  = "";
    FriendMgr::GetInstance()->AddFriend(fi);

    // 加入联系人列表（好友面板�?
    _contact_panel->AddFriend(fi);

    // 在聊天列表新建一条会话（预置，以便直接开始聊天）
    _chat_user_list->AddChatUser(friend_uid, friend_name, fi.avatarPath,
                                 QString::fromUtf8("我们已经是好友了，开始聊天吧"),
                                 QDateTime::currentDateTime().toString("hh:mm"), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 对方同意了己方发出的好友申请 �?把对方加入联系人列表
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_notify_accept_friend(int from_uid, const QString &from_name)
{
    qDebug() << "[Contact] My friend request was accepted by uid=" << from_uid
             << " name=" << from_name;

    // 写入 FriendMgr
    FriendInfo fi;
    fi.uid        = from_uid;
    fi.name       = from_name;
    fi.avatarPath = QString(":/res/head_%1.jpg").arg((from_uid % 5) + 1);
    fi.signature  = "";
    FriendMgr::GetInstance()->AddFriend(fi);

    _contact_panel->AddFriend(fi);
    _chat_user_list->AddChatUser(from_uid, from_name, fi.avatarPath,
                                 QString::fromUtf8("我们已经是好友了，开始聊天吧"),
                                 QDateTime::currentDateTime().toString("hh:mm"), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 收到对方发来的文本消�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_create_group_clicked()
{
    const QList<FriendInfo> friends = FriendMgr::GetInstance()->GetAllFriends();
    if (friends.isEmpty()) {
        QMessageBox::information(
            this,
            "Create Group",
            "You need at least one friend to create a group.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Create Group");
    dlg.resize(360, 440);

    auto *layout = new QVBoxLayout(&dlg);
    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText("Group name");
    layout->addWidget(new QLabel("Group name", &dlg));
    layout->addWidget(nameEdit);

    layout->addWidget(new QLabel("Select at least one friend", &dlg));

    auto *memberList = new QListWidget(&dlg);
    for (const FriendInfo &friendInfo : friends) {
        auto *item = new QListWidgetItem(
            QString("%1 (%2)").arg(friendInfo.name).arg(friendInfo.uid), memberList);
        item->setData(Qt::UserRole, friendInfo.uid);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        if (!friendInfo.avatarPath.isEmpty()) {
            item->setIcon(QIcon(friendInfo.avatarPath));
        }
    }
    layout->addWidget(memberList, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, "Create Group", "Please enter a group name.");
            return;
        }

        int checkedCount = 0;
        for (int i = 0; i < memberList->count(); ++i) {
            if (memberList->item(i)->checkState() == Qt::Checked) {
                ++checkedCount;
            }
        }
        if (checkedCount <= 0) {
            QMessageBox::warning(&dlg, "Create Group", "Please select at least one friend.");
            return;
        }

        dlg.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    QJsonObject req;
    req["group_name"] = nameEdit->text().trimmed();
    QJsonArray memberUids;
    for (int i = 0; i < memberList->count(); ++i) {
        auto *item = memberList->item(i);
        if (item->checkState() == Qt::Checked) {
            memberUids.append(item->data(Qt::UserRole).toInt());
        }
    }
    req["member_uids"] = memberUids;
    TcpMgr::GetInstance()->SendMsg(ID_CREATE_GROUP_REQ, req);
}

void chatDialog::slot_text_msg_received(int from_uid, QString content, qint64 timestamp)
{
    qDebug() << "[ChatUI] Incoming text message from=" << from_uid
             << "current=" << _current_chat_key
             << "len=" << content.size();
    MessageInfo incomingMsg;
    incomingMsg.conversationKey = MakePeerConversationKey(from_uid);
    incomingMsg.conversationType = ConversationType::Peer;
    incomingMsg.fromUid = from_uid;
    incomingMsg.toUid = UserMgr::GetInstance()->GetUid();
    incomingMsg.content = content;
    incomingMsg.time = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp)
        : QDateTime::currentDateTime();
    incomingMsg.isSelf = false;
    incomingMsg.type = MessageInfo::Text;
    if (!ChatMgr::GetInstance()->AppendMessage(incomingMsg)) {
        qWarning() << "[ChatUI] Incoming text append rejected conversation="
                   << incomingMsg.conversationKey;
        return;
    }
    qDebug() << "[ChatUI] Incoming text persisted conversation="
             << incomingMsg.conversationKey;

    UpdateConversationPreview(incomingMsg);
    qDebug() << "[ChatUI] Incoming preview updated conversation="
             << incomingMsg.conversationKey;
    if (_current_chat_key == incomingMsg.conversationKey) {
        RenderConversationMessage(incomingMsg);
        qDebug() << "[ChatUI] Incoming message rendered conversation="
                 << incomingMsg.conversationKey;
        ChatMgr::GetInstance()->MarkAsRead(incomingMsg.conversationKey);
        _chat_user_list->UpdateUnreadCount(incomingMsg.conversationKey, 0);
    }
    return;

    Q_UNUSED(timestamp)
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm");

    // 获取发送方头像
    ChatUserWid *wid = _chat_user_list->GetChatUserWid(from_uid);
    QPixmap senderAvatar;
    if (wid) {
        senderAvatar = ChatBubbleItem::makeRoundAvatar(QPixmap(wid->GetAvatarPath()), 40);
    } else {
        senderAvatar = ChatBubbleItem::makeRoundAvatar(
            QPixmap(QString(":/res/head_%1.jpg").arg((from_uid % 5) + 1)), 40);
    }

    // 构建 MessageInfo 并通过 ChatMgr 持久化（�?SQLite + 计未读）
    MessageInfo msg;
    msg.fromUid = from_uid;
    msg.toUid   = UserMgr::GetInstance()->GetUid();
    msg.content = content;
    msg.time    = QDateTime::currentDateTime();
    msg.isSelf  = false;
    msg.type    = MessageInfo::Text;
    ChatMgr::GetInstance()->AppendMessage(msg);  // 写入 SQLite + 爎未�?

    // 如果当前正在和这个人聊天，直接显示气泡（不计未读�?
    if (_current_chat_uid == from_uid) {
        // 直接渲染气泡（不再二次写 ChatMgr�?
        ChatBubbleItem *bubble = new ChatBubbleItem(
            content, senderAvatar, false, timeStr, ui->msg_scroll_content);
        _msg_layout->addWidget(bubble);
        ScheduleScrollToBottom();
        ChatMgr::GetInstance()->MarkAsRead(from_uid);
        return;
    }

    // 不在当前会话：更新消息预�?+ 角标
    int unread = ChatMgr::GetInstance()->GetUnreadCount(from_uid);
    if (wid) {
        _chat_user_list->UpdateUnreadCount(from_uid, unread);
    } else {
        // 对方不在聊天列表 �?新建会话条目
        QString name   = FriendMgr::GetInstance()->HasFriend(from_uid)
                         ? FriendMgr::GetInstance()->GetFriend(from_uid).name
                         : QString("用户%1").arg(from_uid);
        QString avatar = QString(":/res/head_%1.jpg").arg((from_uid % 5) + 1);
        _chat_user_list->AddChatUser(from_uid, name, avatar, content, timeStr, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AI 流式回复渲染
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_text_msg_self_echo(int to_uid, QString content, qint64 timestamp)
{
    qDebug() << "[ChatUI] Self echo text message to=" << to_uid
             << "current=" << _current_chat_key
             << "len=" << content.size();
    if (to_uid <= 0) {
        return;
    }

    MessageInfo msg;
    msg.conversationKey = MakePeerConversationKey(to_uid);
    msg.conversationType = ConversationType::Peer;
    msg.fromUid = UserMgr::GetInstance()->GetUid();
    msg.toUid = to_uid;
    msg.content = content;
    msg.time = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp)
        : QDateTime::currentDateTime();
    msg.isSelf = true;
    msg.type = MessageInfo::Text;

    if (!ChatMgr::GetInstance()->AppendMessage(msg)) {
        qWarning() << "[ChatUI] Self echo append rejected conversation="
                   << msg.conversationKey;
        return;
    }
    qDebug() << "[ChatUI] Self echo persisted conversation="
             << msg.conversationKey;

    UpdateConversationPreview(msg);
    qDebug() << "[ChatUI] Self echo preview updated conversation="
             << msg.conversationKey;
    if (_current_chat_key == msg.conversationKey) {
        RenderConversationMessage(msg);
        qDebug() << "[ChatUI] Self echo rendered conversation="
                 << msg.conversationKey;
    }
}

void chatDialog::slot_group_text_msg_received(QString groupId, int senderUid,
                                              QString content, qint64 timestamp, QString msgId)
{
    GroupMgr::GetInstance()->EnsureGroupInfo(groupId);

    MessageInfo msg;
    msg.msgId = msgId;
    msg.groupId = groupId;
    msg.conversationKey = MakeGroupConversationKey(groupId);
    msg.conversationType = ConversationType::Group;
    msg.fromUid = senderUid;
    msg.toUid = 0;
    msg.content = content;
    msg.time = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp)
        : QDateTime::currentDateTime();
    msg.isSelf = senderUid == UserMgr::GetInstance()->GetUid();
    msg.type = MessageInfo::Text;
    if (!ChatMgr::GetInstance()->AppendMessage(msg)) {
        return;
    }
    UpdateConversationPreview(msg);

    if (_current_chat_key == msg.conversationKey) {
        RenderConversationMessage(msg);
        ChatMgr::GetInstance()->MarkAsRead(msg.conversationKey);
        _chat_user_list->UpdateUnreadCount(msg.conversationKey, 0);
    }
}

void chatDialog::slot_ai_text_msg_received(int from_uid, QString content, qint64 timestamp)
{
    qDebug() << "[ChatUI] AI plain message from=" << from_uid
             << "current=" << _current_chat_key
             << "len=" << content.size();

    MessageInfo incomingMsg;
    incomingMsg.conversationKey = MakePeerConversationKey(from_uid);
    incomingMsg.conversationType = ConversationType::Peer;
    incomingMsg.fromUid = from_uid;
    incomingMsg.toUid = UserMgr::GetInstance()->GetUid();
    incomingMsg.content = content;
    incomingMsg.time = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp)
        : QDateTime::currentDateTime();
    incomingMsg.isSelf = false;
    incomingMsg.type = MessageInfo::Text;

    if (!ChatMgr::GetInstance()->AppendMessage(incomingMsg)) {
        qWarning() << "[ChatUI] AI plain append rejected conversation="
                   << incomingMsg.conversationKey;
        return;
    }
    qDebug() << "[ChatUI] AI plain persisted conversation="
             << incomingMsg.conversationKey;

    UpdateConversationPreview(incomingMsg);
    qDebug() << "[ChatUI] AI plain preview updated conversation="
             << incomingMsg.conversationKey;
    if (_current_chat_key == incomingMsg.conversationKey) {
        RenderConversationMessage(incomingMsg);
        qDebug() << "[ChatUI] AI plain rendered conversation="
                 << incomingMsg.conversationKey;
        ChatMgr::GetInstance()->MarkAsRead(incomingMsg.conversationKey);
        _chat_user_list->UpdateUnreadCount(incomingMsg.conversationKey, 0);
    }
}

void chatDialog::slot_group_created(int error, QString groupId, QString groupName,
                                    int memberCount, QString msg)
{
    if (error != ErrorCodes::Success || groupId.isEmpty()) {
        QMessageBox::warning(
            this,
            "Create Group",
            msg.isEmpty() ? QString("Failed to create group.") : msg);
        return;
    }

    GroupInfoLite info;
    info.groupId = groupId;
    info.name = groupName;
    info.memberCount = memberCount;
    GroupMgr::GetInstance()->UpsertGroup(info);
    GroupMgr::GetInstance()->EnsureGroupInfo(groupId);

    const QString conversationKey = MakeGroupConversationKey(groupId);
    const QString now = QDateTime::currentDateTime().toString("hh:mm");
    EnsureConversationEntry(conversationKey, "Group created", now);
    _chat_user_list->MoveToTop(conversationKey);
    slot_chat_user_clicked(conversationKey);
}

void chatDialog::slot_group_text_msg_rsp(int error, QString groupId, QString msgId,
                                         qint64 timestamp, QString msg)
{
    if (error != ErrorCodes::Success) {
        QMessageBox::warning(
            this,
            "Group Message",
            msg.isEmpty() ? QString("Failed to send group message.") : msg);
        return;
    }

    const QString conversationKey = MakeGroupConversationKey(groupId);
    const QDateTime serverTime = timestamp > 0
        ? QDateTime::fromSecsSinceEpoch(timestamp)
        : QDateTime();

    GroupMgr::GetInstance()->MarkLatestMsg(groupId, msgId);
    ChatMgr::GetInstance()->UpdatePendingGroupTextAck(conversationKey, msgId, serverTime);

    const MessageInfo lastMsg = ChatMgr::GetInstance()->GetLastMessage(conversationKey);
    if (!lastMsg.content.isEmpty()) {
        UpdateConversationPreview(lastMsg);
    }
}

void chatDialog::slot_group_sync_received(int error, QString groupId,
                                          QString latestMsgId, QJsonArray messages, QString msg)
{
    if (error != ErrorCodes::Success) {
        qWarning() << "[GroupSync] failed:" << groupId << error << msg;
        return;
    }

    GroupMgr::GetInstance()->EnsureGroupInfo(groupId);
    GroupMgr::GetInstance()->MarkLatestMsg(groupId, latestMsgId);

    const QString conversationKey = MakeGroupConversationKey(groupId);
    const bool isCurrentConversation = (_current_chat_key == conversationKey);

    for (const auto &value : messages) {
        const QJsonObject item = value.toObject();
        MessageInfo msgInfo;
        msgInfo.msgId = JsonIdToString(item.value("msg_id"));
        msgInfo.groupId = groupId;
        msgInfo.conversationKey = conversationKey;
        msgInfo.conversationType = ConversationType::Group;
        msgInfo.fromUid = item.value("sender_uid").toInt();
        msgInfo.toUid = 0;
        msgInfo.content = item.value("content").toString();
        const qint64 itemTimestamp = item.value("timestamp").toVariant().toLongLong();
        msgInfo.time = itemTimestamp > 0
            ? QDateTime::fromSecsSinceEpoch(itemTimestamp)
            : QDateTime::currentDateTime();
        msgInfo.isSelf = msgInfo.fromUid == UserMgr::GetInstance()->GetUid();
        msgInfo.type = MessageInfo::Text;

        if (!ChatMgr::GetInstance()->AppendMessage(msgInfo)) {
            continue;
        }

        UpdateConversationPreview(msgInfo);
        if (isCurrentConversation) {
            RenderConversationMessage(msgInfo);
        }
    }

    if (isCurrentConversation) {
        ChatMgr::GetInstance()->MarkAsRead(conversationKey);
        _chat_user_list->UpdateUnreadCount(conversationKey, 0);
    }
}

void chatDialog::slot_group_updated(QString groupId)
{
    const QString conversationKey = MakeGroupConversationKey(groupId);
    if (!_chat_user_list->GetChatUserWid(conversationKey)) {
        return;
    }

    _chat_user_list->UpdateChatUserInfo(
        conversationKey,
        GroupMgr::GetInstance()->GetGroupName(groupId),
        GroupMgr::GetInstance()->GetGroupAvatarPath(groupId));

    if (_current_chat_key == conversationKey) {
        ui->titel_lb->setText(GroupMgr::GetInstance()->GetGroupName(groupId));
    }
}

void chatDialog::FlushStreamChunks()
{
    _stream_pending_chunks.clear();
}

void chatDialog::slot_stream_start(int from_uid, QString stream_id, qint64 timestamp)
{
    qDebug() << "[ChatUI] AI stream start from=" << from_uid
             << "stream_id=" << stream_id;
    const QString conversationKey = MakePeerConversationKey(from_uid);
    const qint64 ts = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();

    EnsureConversationEntry(
        conversationKey,
        QString::fromUtf8("AI 助手正在回复..."),
        QDateTime::fromSecsSinceEpoch(ts).toString("hh:mm"));
    _stream_text_accum.insert(stream_id, QString());
    _stream_pending_chunks.remove(stream_id);
    _stream_conversation_keys.insert(stream_id, conversationKey);
}

void chatDialog::slot_stream_chunk(int from_uid, QString stream_id, QString piece)
{
    Q_UNUSED(from_uid)
    if (!_stream_text_accum.contains(stream_id)) {
        qWarning() << "[ChatUI] AI stream chunk without start stream_id=" << stream_id;
        return;
    }

    _stream_text_accum[stream_id] += piece;
}

void chatDialog::slot_stream_end(int from_uid, QString stream_id)
{
    qDebug() << "[ChatUI] AI stream end from=" << from_uid
             << "stream_id=" << stream_id;
    const QString conversationKey = _stream_conversation_keys.value(
        stream_id, MakePeerConversationKey(from_uid));
    const QString fullText = _stream_text_accum.value(stream_id);

    if (!fullText.isEmpty()) {
        MessageInfo msg;
        msg.conversationKey = conversationKey;
        msg.conversationType = ConversationType::Peer;
        msg.fromUid = from_uid;
        msg.toUid = UserMgr::GetInstance()->GetUid();
        msg.content = fullText;
        msg.time = QDateTime::currentDateTime();
        msg.isSelf = false;
        msg.type = MessageInfo::Text;

        if (ChatMgr::GetInstance()->AppendMessage(msg)) {
            qDebug() << "[ChatUI] AI stream persisted conversation=" << conversationKey
                     << "len=" << fullText.size();
            UpdateConversationPreview(msg);
            qDebug() << "[ChatUI] AI stream preview updated conversation=" << conversationKey;
            if (_current_chat_key == conversationKey) {
                RenderConversationMessage(msg);
                qDebug() << "[ChatUI] AI stream rendered conversation=" << conversationKey;
                ChatMgr::GetInstance()->MarkAsRead(conversationKey);
                _chat_user_list->UpdateUnreadCount(conversationKey, 0);
            }
        }
    }

    _active_streams.remove(stream_id);
    _stream_text_accum.remove(stream_id);
    _stream_pending_chunks.remove(stream_id);
    _stream_conversation_keys.remove(stream_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// 语音录制入口（占位实现，避免链接错误）
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_toggle_record_voice()
{
    if (_is_recording_voice) {
        if (_record_max_timer) {
            _record_max_timer->stop();
        }
        QAudioSource *recorder = _voice_recorder;
        QFile *recordFile = _voice_record_file;
        const QAudioFormat recordFormat = _voice_fmt;
        if (recorder) {
            recorder->stop();
        }

        const qint64 durationMs = qMax<qint64>(
            0, QDateTime::currentMSecsSinceEpoch() - _record_start_ms);

        _is_recording_voice = false;
        ui->voice_msg_btn->setText(QString::fromUtf8("语音"));
        ui->voice_msg_btn->setToolTip(QString::fromUtf8("发送语音消息"));

        const QString voicePath = _record_voice_path;
        const QString conversationKey = _record_conversation_key;
        const int targetUid = _record_target_uid;

        _record_voice_path.clear();
        _record_conversation_key.clear();
        _record_target_uid = 0;
        _record_start_ms = 0;

        if (voicePath.isEmpty() || conversationKey.isEmpty() || targetUid <= 0) {
            return;
        }
        QTimer::singleShot(150, this, [this,
                                      recorder,
                                      recordFile,
                                      recordFormat,
                                      durationMs,
                                      voicePath,
                                      conversationKey,
                                      targetUid]() {
            if (recordFile) {
                if (recordFile->isOpen()) {
                    recordFile->flush();
                    RewriteWaveHeader(recordFile, recordFormat);
                    recordFile->close();
                }
            }

            if (durationMs < 500) {
                QFile::remove(voicePath);
                QMessageBox::information(this,
                                         QString::fromUtf8("提示"),
                                         QString::fromUtf8("录音时间太短。"));
                return;
            }

            MessageInfo msg;
            msg.conversationKey = conversationKey;
            msg.conversationType = ConversationType::Peer;
            msg.fromUid = UserMgr::GetInstance()->GetUid();
            msg.toUid = targetUid;
            msg.content = BuildVoiceMessagePayload(
                voicePath, static_cast<int>(durationMs));
            msg.time = QDateTime::currentDateTime();
            msg.isSelf = true;
            msg.type = MessageInfo::Voice;

            if (ChatMgr::GetInstance()->AppendMessage(msg)) {
                UpdateConversationPreview(msg);
                if (_current_chat_key == conversationKey) {
                    RenderConversationMessage(msg);
                }
            }

            FileTransferMgr::GetInstance()->SendFileMinio(
                targetUid, voicePath, false, true, static_cast<int>(durationMs));
        });
        return;

        if (durationMs < 500) {
            QFile::remove(voicePath);
            QMessageBox::information(this,
                                     QString::fromUtf8("提示"),
                                     QString::fromUtf8("录音时间太短。"));
            return;
        }

        MessageInfo msg;
        msg.conversationKey = conversationKey;
        msg.conversationType = ConversationType::Peer;
        msg.fromUid = UserMgr::GetInstance()->GetUid();
        msg.toUid = targetUid;
        msg.content = BuildVoiceMessagePayload(
            voicePath, static_cast<int>(durationMs));
        msg.time = QDateTime::currentDateTime();
        msg.isSelf = true;
        msg.type = MessageInfo::Voice;

        if (ChatMgr::GetInstance()->AppendMessage(msg)) {
            UpdateConversationPreview(msg);
            if (_current_chat_key == conversationKey) {
                RenderConversationMessage(msg);
            }
        }

        FileTransferMgr::GetInstance()->SendFileMinio(
            targetUid, voicePath, false, true, static_cast<int>(durationMs));
        return;
    }

    if (CurrentConversationKey().isEmpty()) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个聊天会话"));
        return;
    }
    if (IsGroupConversationActive() || _current_chat_uid == 0) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("语音消息暂时只支持单聊。"));
        return;
    }

    slot_warmup_audio_input();
    if (!_voice_fmt.isValid() || _voice_fmt.sampleRate() <= 0) {
        QMessageBox::warning(this,
                             QString::fromUtf8("提示"),
                             QString::fromUtf8("没有可用的录音设备。"));
        return;
    }

    const QString voiceDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/voice";
    QDir().mkpath(voiceDir);

    _record_voice_path = QString("%1/voice_%2.wav")
                             .arg(voiceDir)
                             .arg(QDateTime::currentMSecsSinceEpoch());
    _record_conversation_key = CurrentConversationKey();
    _record_target_uid = _current_chat_uid;
    _record_start_ms = QDateTime::currentMSecsSinceEpoch();

    if (_voice_record_file) {
        _voice_record_file->close();
        _voice_record_file->deleteLater();
        _voice_record_file = nullptr;
    }

    _voice_record_file = new QFile(_record_voice_path, this);
    if (!_voice_record_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this,
                             QString::fromUtf8("提示"),
                             QString::fromUtf8("无法创建语音文件。"));
        _voice_record_file->deleteLater();
        _voice_record_file = nullptr;
        _record_voice_path.clear();
        _record_conversation_key.clear();
        _record_target_uid = 0;
        return;
    }

    _voice_record_file->write(QByteArray(44, '\0'));

    if (_voice_recorder) {
        _voice_recorder->stop();
        _voice_recorder->deleteLater();
    }
    _voice_recorder = new QAudioSource(
        QMediaDevices::defaultAudioInput(), _voice_fmt, this);
    _voice_recorder->start(_voice_record_file);

    _is_recording_voice = true;
    ui->voice_msg_btn->setText(QString::fromUtf8("停止"));
    ui->voice_msg_btn->setToolTip(QString::fromUtf8("点击结束录音并发送"));
    if (_record_max_timer) {
        _record_max_timer->start(60000);
    }
    return;

    if (CurrentConversationKey().isEmpty()) {
        QMessageBox::information(this,
                                 QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选择一个聊天会话"));
        return;
    }

    QMessageBox::information(this,
                             QString::fromUtf8("提示"),
                             QString::fromUtf8("语音消息录制功能正在接入中。"));
    qDebug() << "[Voice] toggle record (not implemented yet).";
}

void chatDialog::slot_cancel_record_voice()
{
    if (_record_max_timer) {
        _record_max_timer->stop();
    }
    if (_voice_recorder) {
        _voice_recorder->stop();
        _voice_recorder->deleteLater();
        _voice_recorder = nullptr;
    }
    if (_voice_record_file) {
        _voice_record_file->close();
        _voice_record_file->deleteLater();
        _voice_record_file = nullptr;
    }
    if (!_record_voice_path.isEmpty()) {
        QFile::remove(_record_voice_path);
    }

    _is_recording_voice = false;
    _record_voice_path.clear();
    _record_conversation_key.clear();
    _record_target_uid = 0;
    _record_start_ms = 0;
    ui->voice_msg_btn->setText(QString::fromUtf8("语音"));
    ui->voice_msg_btn->setToolTip(QString::fromUtf8("发送语音消息"));
    return;

    qDebug() << "[Voice] cancel record (not implemented yet).";
}

void chatDialog::slot_warmup_audio_input()
{
    const auto inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        _voice_fmt = QAudioFormat();
        return;
    }

    QAudioFormat desired;
    desired.setSampleRate(16000);
    desired.setChannelCount(1);
    desired.setSampleFormat(QAudioFormat::Int16);

    _voice_fmt = inputDevice.isFormatSupported(desired)
        ? desired
        : inputDevice.preferredFormat();
    return;

    qDebug() << "[Voice] warmup audio input (not implemented yet).";
}

// 登录时服务端推送好友列表，用真实数据替换测试占�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_friends_list_received(QJsonArray friends)
{
    qDebug() << "[FriendList] Received" << friends.size() << "friends from server";

    // 清除测试数据
    _current_chat_key.clear();
    _current_chat_uid = 0;
    _current_group_id.clear();
    if (_msg_stack && _empty_msg_page) {
        _msg_stack->setCurrentWidget(_empty_msg_page);
    }
    _chat_user_list->ClearAllItems();
    _contact_panel->ClearFriends();
    FriendMgr::GetInstance()->Clear();
    GroupMgr::GetInstance()->Clear();
    for (QWidget *page : _conversation_pages) {
        if (_msg_stack && page) {
            _msg_stack->removeWidget(page);
        }
        delete page;
    }
    _conversation_pages.clear();
    _conversation_layouts.clear();
    _dirty_conversations.clear();
    _msg_layout = nullptr;
    if (_msg_stack && _empty_msg_page) {
        _msg_stack->setCurrentWidget(_empty_msg_page);
    }

    for (const auto &val : friends) {
        QJsonObject obj = val.toObject();
        FriendInfo fi;
        fi.uid        = obj["uid"].toInt();
        fi.name       = obj["name"].toString();
        fi.signature  = obj["signature"].toString();
        fi.avatarPath = QString(":/res/head_%1.jpg").arg((fi.uid % 5) + 1);

        FriendMgr::GetInstance()->AddFriend(fi);
        _contact_panel->AddFriend(fi);
    }

    qDebug() << "[FriendList] Contact panel updated with" << friends.size() << "friends";
    LoadLocalConversations();
    RequestKnownGroupSync();
}

// ─────────────────────────────────────────────────────────────────────────────
QPixmap chatDialog::makeAvatar(const QString &path, int size)
{
    return ChatBubbleItem::makeRoundAvatar(
        QPixmap(path.isEmpty() ? ":/res/head_1.jpg" : path), size);
}

void chatDialog::slot_friend_profile_clicked(int uid, const QString &name,
                                              const QString &avatarPath,
                                              const QString &signature)
{
    _profile_panel->ShowFriend(uid, name, avatarPath, signature);
    _right_stack->setCurrentIndex(2);
}

void chatDialog::slot_profile_send_msg(int uid, const QString &name, const QString &avatarPath)
{
    // 切换到聊天模�?
    slot_switch_chat_mode();
    ui->side_chat_lb->ResetToNormal();

    // 如果该好友在聊天列表中不存在，先创建一条会话条�?
    if (!_chat_user_list->GetChatUserWid(uid)) {
        _chat_user_list->AddChatUser(uid, name, avatarPath,
                                     QString::fromUtf8("开始聊天"),
                                     QDateTime::currentDateTime().toString("hh:mm"), 0);
    }

    // 切换对话（加载历史消�?+ 更新标题�?
    slot_chat_user_clicked(uid);
}

// ── 断线重连状态横�?─────────────────────────────────────────────────────

void chatDialog::slot_reconnecting(int attempt, int nextDelaySec)
{
    _reconnect_countdown_sec = nextDelaySec;
    _net_status_bar->setStyleSheet(
        "background:#f59a23; color:white; font-size:12px;"
        " font-family:'Microsoft YaHei'; border:none;");
    _net_status_bar->setText(
        QString::fromUtf8("网络已断开，%1 秒后重试（第 %2 次）")
            .arg(nextDelaySec).arg(attempt));
    _net_status_bar->show();
    _countdown_timer->start();
}

void chatDialog::slot_reconnected()
{
    _countdown_timer->stop();
    _net_status_bar->setStyleSheet(
        "background:#52c41a; color:white; font-size:12px;"
        " font-family:'Microsoft YaHei'; border:none;");
    _net_status_bar->setText(QString::fromUtf8("已重新连接"));
    QTimer::singleShot(2000, this, [this]() {
        _net_status_bar->hide();
    });
    RequestKnownGroupSync();
}

// ─────────────────────────────────────────────────────────────────────────────
// 文件气泡 �?AppendFileMessage
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::AppendFileMessage(const QString &filename, qint64 fileSize,
                                    const QString &fileKey, const QString &downloadUrl,
                                    const QPixmap &avatar, bool isSelf,
                                    const QString &timeStr)
{
    AppendFileMessage(filename, fileSize, fileKey, downloadUrl, avatar, isSelf, timeStr, "");
}

void chatDialog::AppendFileMessage(const QString &filename, qint64 fileSize,
                                    const QString &fileKey, const QString &downloadUrl,
                                    const QPixmap &avatar, bool isSelf,
                                    const QString &timeStr,
                                    const QString &localPath)
{
    if (!_msg_layout) return;
    QWidget *host = CurrentMessageHost();
    if (!host) return;
    auto *bubble = new ChatBubbleItem(
        filename, fileSize, fileKey, downloadUrl,
        avatar, isSelf,
        timeStr.isEmpty()
            ? QDateTime::currentDateTime().toString("hh:mm")
            : timeStr,
        localPath,
        host);
    _msg_layout->addWidget(bubble);
    ScheduleScrollToBottom();
}
void chatDialog::AppendVoiceMessage(const QString &voicePath, int durationMs,
                                    const QPixmap &avatar, bool isSelf,
                                    const QString &timeStr)
{
    if (!_msg_layout) return;
    QWidget *host = CurrentMessageHost();
    if (!host) return;

    int totalSec = qMax(1, durationMs / 1000);
    auto *btn = new QPushButton(QString::fromUtf8("▶ 语音 %1s").arg(totalSec), host);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setMinimumHeight(38);
    btn->setMaximumHeight(38);
    btn->setMinimumWidth(VoiceBubbleWidth(totalSec));
    btn->setMaximumWidth(220);
    btn->setProperty("voiceBubble", "true");
    btn->setProperty("bubbleRole", isSelf ? "self" : "peer");
    btn->setProperty("voiceState", "idle");
    btn->setStyleSheet(isSelf
                       ? "QPushButton{background:#95EC69;border:none;border-radius:10px;padding:8px 12px;}"
                       : "QPushButton{background:#ffffff;border:1px solid #e6e6e6;border-radius:10px;padding:8px 12px;}");

    btn->setStyleSheet("");
    _voice_btn_duration_sec[btn] = totalSec;
    UpdateVoiceButtonVisual(btn, totalSec, "idle");

    connect(btn, &QPushButton::clicked, this, [this, btn, voicePath, totalSec]() {
        if (!_voice_player) return;

        // 点同一条：停止
        if (_playing_voice_btn == btn &&
            _voice_player->playbackState() == QMediaPlayer::PlayingState) {
            _voice_player->stop();
            btn->setText(QString::fromUtf8("▶ 语音 %1s").arg(totalSec));
            _playing_voice_btn = nullptr;
            _playing_voice_path.clear();
            UpdateVoiceButtonVisual(btn, totalSec, "idle");
            return;
        }

        // 切换播放：恢复上一条文案
        if (_playing_voice_btn && _playing_voice_btn != btn) {
            int oldDur = _voice_btn_duration_sec.value(_playing_voice_btn, 1);
            UpdateVoiceButtonVisual(_playing_voice_btn, oldDur, "idle");
            _playing_voice_btn->setText(QString::fromUtf8("▶ 语音 %1s").arg(oldDur));
        }

        _voice_player->stop();
        _playing_voice_btn = btn;
        _playing_voice_path = voicePath;
        _voice_player->setSource(QUrl::fromLocalFile(voicePath));
        _voice_player->play();
        UpdateVoiceButtonVisual(btn, totalSec, "playing", 0);
    });

    QWidget *row = new QWidget(host);
    QHBoxLayout *h = new QHBoxLayout(row);
    h->setContentsMargins(12, 6, 12, 6);
    h->setSpacing(8);

    QLabel *avatarLb = new QLabel(row);
    avatarLb->setFixedSize(40, 40);
    avatarLb->setPixmap(ChatBubbleItem::makeRoundAvatar(avatar, 40));

    QVBoxLayout *content = new QVBoxLayout();
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(3);

    if (!timeStr.isEmpty()) {
        QLabel *timeLb = new QLabel(timeStr, row);
        timeLb->setObjectName("bubble_time_lb");
        timeLb->setAlignment(isSelf ? Qt::AlignRight : Qt::AlignLeft);
        content->addWidget(timeLb);
    }

    content->addWidget(btn, 0, isSelf ? Qt::AlignRight : Qt::AlignLeft);

    if (isSelf) {
        h->addStretch();
        h->addLayout(content);
        h->addWidget(avatarLb);
    } else {
        h->addWidget(avatarLb);
        h->addLayout(content);
        h->addStretch();
    }

    _msg_layout->addWidget(row);
    ScheduleScrollToBottom();
}

// ─────────────────────────────────────────────────────────────────────────────
// MinIO 文件通知 (接收�? �?显示文件气泡
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_minio_file_incoming(
    int senderUid, QString fileKey,
    QString filename, qint64 fileSize, QString downloadUrl,
    bool isVoice, int durationMs)
{
    Q_UNUSED(durationMs)

    // 语音已走 sig_voice_ready，这里直接忽略
    if (isVoice) return;

    QPixmap senderAvatar = _peer_avatar;
    ChatUserWid *wid = _chat_user_list->GetChatUserWid(senderUid);
    if (wid && !wid->GetAvatarPath().isEmpty())
        senderAvatar = ChatBubbleItem::makeRoundAvatar(QPixmap(wid->GetAvatarPath()), 40);

    AppendFileMessage(filename, fileSize, fileKey, downloadUrl,
                      senderAvatar, /*isSelf=*/false);

    if (senderUid != _current_chat_uid) {
        int unread = ChatMgr::GetInstance()->GetUnreadCount(senderUid);
        _chat_user_list->UpdateUnreadCount(senderUid, unread + 1);
    }
}
void chatDialog::slot_voice_ready(int senderUid, QString savedPath, int durationMs)
{
    if (senderUid <= 0 || savedPath.isEmpty()) {
        return;
    }

    MessageInfo msg;
    msg.conversationKey = MakePeerConversationKey(senderUid);
    msg.conversationType = ConversationType::Peer;
    msg.fromUid = senderUid;
    msg.toUid = UserMgr::GetInstance()->GetUid();
    msg.content = BuildVoiceMessagePayload(savedPath, qMax(1, durationMs));
    msg.time = QDateTime::currentDateTime();
    msg.isSelf = false;
    msg.type = MessageInfo::Voice;

    if (!ChatMgr::GetInstance()->AppendMessage(msg)) {
        return;
    }

    UpdateConversationPreview(msg);
    if (_current_chat_key == msg.conversationKey) {
        RenderConversationMessage(msg);
        ChatMgr::GetInstance()->MarkAsRead(msg.conversationKey);
        _chat_user_list->UpdateUnreadCount(msg.conversationKey, 0);
    }
    return;

    QPixmap senderAvatar = _peer_avatar;
    ChatUserWid *wid = _chat_user_list->GetChatUserWid(senderUid);
    if (wid && !wid->GetAvatarPath().isEmpty()) {
        senderAvatar = ChatBubbleItem::makeRoundAvatar(QPixmap(wid->GetAvatarPath()), 40);
    }

    AppendVoiceMessage(savedPath, durationMs, senderAvatar, /*isSelf=*/false);

    if (senderUid != _current_chat_uid) {
        int unread = ChatMgr::GetInstance()->GetUnreadCount(senderUid);
        _chat_user_list->UpdateUnreadCount(senderUid, unread + 1);
    }
}
// ─────────────────────────────────────────────────────────────────────────────
// MinIO 上传完成 (发送方) �?显示自身文件气泡
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_minio_upload_done(QString fileKey, QString filename, qint64 fileSize)
{
    // 气泡已在选文件时立即显示，此处只打印日志
    Q_UNUSED(fileKey) Q_UNUSED(filename) Q_UNUSED(fileSize)
    qDebug() << "[File] Upload committed:" << filename;
}

// ─────────────────────────────────────────────────────────────────────────────
// 图片气泡 �?AppendImageMessage
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::AppendImageMessage(const QPixmap &image, const QPixmap &avatar,
                                    bool isSelf, const QString &savedPath,
                                    const QString &timeStr)
{
    if (!_msg_layout) return;
    QWidget *host = CurrentMessageHost();
    if (!host) return;
    auto *bubble = new ChatBubbleItem(image, avatar, isSelf,
                                      timeStr.isEmpty()
                                          ? QDateTime::currentDateTime().toString("hh:mm")
                                          : timeStr,
                                      savedPath, host);
    _msg_layout->addWidget(bubble);
    ScheduleScrollToBottom();
}

// ─────────────────────────────────────────────────────────────────────────────
// 收到图片自动下载完成 �?显示为接收方气泡
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_image_ready(int senderUid, QString savedPath)
{
    QPixmap img(savedPath);
    if (img.isNull()) return;

    // 找到发送方的头�?
    QPixmap senderAvatar = _peer_avatar;
    ChatUserWid *wid = _chat_user_list->GetChatUserWid(senderUid);
    if (wid) {
        QString ap = wid->GetAvatarPath();
        if (!ap.isEmpty())
            senderAvatar = ChatBubbleItem::makeRoundAvatar(QPixmap(ap), 40);
    }

    // 如果不在该会话界面，先切换（或也可只加气泡到该会话的历史，此处简单处理：无论当前在哪都显示）
    AppendImageMessage(img, senderAvatar, false, savedPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// 删除好友：资料卡按钮 �?�?TCP 请求
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_delete_friend(int uid)
{
    auto btn = QMessageBox::question(this,
        QString::fromUtf8("删除好友"),
        QString::fromUtf8("确定要删除好友吗？删除后无法向对方发送消息。"),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    QJsonObject req;
    req["friend_uid"] = uid;
    TcpMgr::GetInstance()->SendMsg(ID_DELETE_FRIEND_REQ, req);
}

// ─────────────────────────────────────────────────────────────────────────────
// 屏蔽好友：资料卡按钮 �?�?TCP 请求
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_block_friend(int uid)
{
    auto btn = QMessageBox::question(this,
        QString::fromUtf8("屏蔽好友"),
        QString::fromUtf8("屏蔽后对方无法向你发消息，同时从你的好友列表移除。"),
        QMessageBox::Yes | QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    QJsonObject req;
    req["friend_uid"] = uid;
    TcpMgr::GetInstance()->SendMsg(ID_BLOCK_FRIEND_REQ, req);
}

// ─────────────────────────────────────────────────────────────────────────────
// 删除好友服务端回�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_delete_friend_rsp(int error, int friendUid)
{
    if (error != 0) {
        QMessageBox::warning(this,
            QString::fromUtf8("操作失败"),
            QString::fromUtf8("删除好友失败，请稍后再试"));
        return;
    }
    // 从联系人面板移除
    _contact_panel->RemoveFriend(friendUid);
    FriendMgr::GetInstance()->RemoveFriend(friendUid);
    // 如果当前正在和此人聊天，重置右侧面板
    if (_current_chat_uid == friendUid) {
        _current_chat_uid = 0;
        _current_chat_key.clear();
        _right_stack->setCurrentIndex(0);
    }
    _profile_panel->Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 屏蔽好友服务端回�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_block_friend_rsp(int error, int friendUid)
{
    if (error != 0) {
        QMessageBox::warning(this,
            QString::fromUtf8("操作失败"),
            QString::fromUtf8("屏蔽操作失败，请稍后再试"));
        return;
    }
    // 从联系人面板移除，FriendMgr 标记为屏�?
    _contact_panel->RemoveFriend(friendUid);
    FriendMgr::GetInstance()->BlockFriend(friendUid);

    if (_current_chat_uid == friendUid) {
        _current_chat_uid = 0;
        _current_chat_key.clear();
        _right_stack->setCurrentIndex(0);
    }
    _profile_panel->Reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// 个人资料编辑对话�?
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_open_profile_edit()
{
    auto *um = UserMgr::GetInstance().get();
    QPixmap currentAvatar(um->GetAvatarPath().isEmpty()
                          ? ":/res/head_1.jpg" : um->GetAvatarPath());

    ProfileEditDialog dlg(um->GetName(), um->GetSignature(), currentAvatar, this);
    if (dlg.exec() != QDialog::Accepted) return;

    QString newName = dlg.GetName();
    QString newSig  = dlg.GetSignature();

    // 构建请求 JSON
    QJsonObject req;
    req["name"]      = newName;
    req["signature"] = newSig;

    // 如果头像变了，保存本地并 base64 附到请求
    if (dlg.AvatarChanged()) {
        QPixmap newPx   = dlg.GetAvatar();
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                          + "/avatars";
        QDir().mkpath(dataDir);
        QString localPath = dataDir + "/self.png";
        newPx.save(localPath, "PNG");

        QFile f(localPath);
        if (f.open(QFile::ReadOnly)) {
            req["avatar_b64"] = QString::fromLatin1(f.readAll().toBase64());
            req["avatar_ext"] = "png";
            f.close();
        }

        // 居先局部更新头像显�?
        int sz = 36;
        QPixmap rounded(sz, sz);
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addEllipse(0, 0, sz, sz);
        p.setClipPath(path);
        QPixmap scaled = newPx.scaled(sz, sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap(0, 0, scaled);
        ui->side_head_lb->setPixmap(rounded);

        UserMgr::GetInstance()->UpdateProfile(newName, newSig, localPath);
        _self_avatar = ChatBubbleItem::makeRoundAvatar(newPx, 40);
    } else {
        UserMgr::GetInstance()->UpdateProfile(newName, newSig, um->GetAvatarPath());
    }

    // 发�?TCP 请求到服务端持久�?
    TcpMgr::GetInstance()->SendMsg(ID_UPDATE_PROFILE_REQ, req);
}

// ─────────────────────────────────────────────────────────────────────────────
// 服务端回执：将最终确认的资料同步�?UserMgr
// ─────────────────────────────────────────────────────────────────────────────
void chatDialog::slot_update_profile_rsp(int error, QString name, QString signature)
{
    if (error != 0) {
        QMessageBox::warning(this,
            QString::fromUtf8("修改失败"),
            QString::fromUtf8("修改资料失败，请稍后重试"));
        return;
    }
    // 用服务端确认�?name/signature 最终同步（avatar 保留本地路径�?
    UserMgr::GetInstance()->UpdateProfile(name, signature,
                                          UserMgr::GetInstance()->GetAvatarPath());
    QMessageBox::information(this,
        QString::fromUtf8("修改成功"),
        QString::fromUtf8("个人资料已更新"));
}

