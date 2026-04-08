#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QAction>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSource>
#include <QDateTime>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QLabel>
#include <QMap>
#include <QMediaPlayer>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVector>
#include <QVBoxLayout>

#include "ChatMgr.h"
#include "FriendMgr.h"
#include "GroupMgr.h"
#include "UserMgr.h"
#include "addfriendwidget.h"
#include "chat_user_list.h"
#include "chatbubbleitem.h"
#include "contactlistpanel.h"
#include "emojipickerwidget.h"
#include "friendprofilepanel.h"
#include "friendverifypanel.h"
#include "global.h"
#include "searchuseritem.h"

class VideoCallDialog;
class VoiceCallDialog;

namespace Ui {
class chatDialog;
}

class chatDialog : public QDialog
{
    Q_OBJECT

public:
    explicit chatDialog(QWidget *parent = nullptr);
    ~chatDialog();

    void LoadTestData();
    void LoadLocalConversations();

    void AppendMessage(const QString &text, const QPixmap &avatar,
                       bool isSelf, const QString &timeStr = "");

    void AppendImageMessage(const QPixmap &image, const QPixmap &avatar,
                            bool isSelf, const QString &savedPath = "",
                            const QString &timeStr = "");

    void AppendFileMessage(const QString &filename, qint64 fileSize,
                           const QString &fileKey, const QString &downloadUrl,
                           const QPixmap &avatar, bool isSelf,
                           const QString &timeStr = "");

    void AppendFileMessage(const QString &filename, qint64 fileSize,
                           const QString &fileKey, const QString &downloadUrl,
                           const QPixmap &avatar, bool isSelf,
                           const QString &timeStr,
                           const QString &localPath);

    void AppendVoiceMessage(const QString &voicePath, int durationMs,
                            const QPixmap &avatar, bool isSelf,
                            const QString &timeStr = "");

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool IsGroupConversationActive() const;
    QString CurrentConversationKey() const;
    void ShowSearch(bool b_search);
    void AddChatUserList();
    void SendMessage();
    void SwitchToChat(const QString &conversationKey);
    void SwitchToChat(int uid);
    void SaveCurrentMessages();
    void DoSearch(const QString &keyword);
    QString ResolveCallPeerName(int uid) const;
    QString ResolveConversationName(const QString &conversationKey) const;
    void RequestGroupSync(const QString &groupId);
    void RequestKnownGroupSync();
    QString FormatDisplayText(const MessageInfo &msg) const;
    QPixmap ResolveMessageAvatar(const MessageInfo &msg) const;
    QWidget *CurrentMessageHost() const;
    void ScrollCurrentConversationToBottom();
    void ScheduleScrollToBottom();
    QWidget *EnsureConversationPage(const QString &conversationKey);
    QVBoxLayout *EnsureConversationLayout(const QString &conversationKey);
    void ClearConversationPage(const QString &conversationKey);
    void RebuildConversationPage(const QString &conversationKey);
    void MarkConversationDirty(const QString &conversationKey);
    void RenderConversationMessageInto(const MessageInfo &msg,
                                       QVBoxLayout *layout,
                                       QWidget *host,
                                       bool autoScroll);
    void RenderConversationMessage(const MessageInfo &msg, bool autoScroll = true);
    void UpdateConversationPreview(const MessageInfo &msg);
    void EnsureConversationEntry(const QString &conversationKey,
                                 const QString &previewText,
                                 const QString &timeStr);
    void ShowVideoCallDialog(const QString &peerName);
    void ShowVoiceCallDialog(const QString &peerName);
    void FocusActiveCallDialog();
    void FlushStreamChunks();

private slots:
    void slot_loading_chat_user();
    void slot_search_focus_in();
    void slot_search_focus_out();
    void slot_chat_user_clicked(const QString &conversationKey);
    void slot_chat_user_clicked(int uid);
    void slot_add_friend_clicked(int uid, const QString &name, const QString &avatarPath);

    void slot_search_finished(REQ_ID req_id, QString res, ErrorCodes err);
    void slot_notify_add_friend(int from_uid, QString from_name, QString message);
    void slot_switch_contact_mode();
    void slot_switch_chat_mode();
    void slot_friend_req_accepted(int uid);
    void slot_friend_req_rejected(int uid);

    void slot_friend_profile_clicked(int uid, const QString &name,
                                     const QString &avatarPath, const QString &signature);
    void slot_profile_send_msg(int uid, const QString &name, const QString &avatarPath);

    void slot_accept_friend_rsp(int friend_uid, const QString &friend_name);
    void slot_friends_list_received(QJsonArray friends);
    void slot_notify_accept_friend(int from_uid, const QString &from_name);

    void slot_reconnecting(int attempt, int nextDelaySec);
    void slot_reconnected();
    void slot_create_group_clicked();
    void slot_text_msg_received(int from_uid, QString content, qint64 timestamp);
    void slot_text_msg_self_echo(int to_uid, QString content, qint64 timestamp);
    void slot_ai_text_msg_received(int from_uid, QString content, qint64 timestamp);
    void slot_group_created(int error, QString groupId, QString groupName,
                            int memberCount, QString msg);
    void slot_group_text_msg_received(QString groupId, int senderUid,
                                      QString content, qint64 timestamp, QString msgId);
    void slot_group_text_msg_rsp(int error, QString groupId, QString msgId,
                                 qint64 timestamp, QString msg);
    void slot_group_sync_received(int error, QString groupId,
                                  QString latestMsgId, QJsonArray messages, QString msg);
    void slot_group_updated(QString groupId);
    void slot_stream_start(int from_uid, QString stream_id, qint64 timestamp);
    void slot_stream_chunk(int from_uid, QString stream_id, QString piece);
    void slot_stream_end(int from_uid, QString stream_id);

    void slot_delete_friend(int uid);
    void slot_block_friend(int uid);
    void slot_delete_friend_rsp(int error, int friendUid);
    void slot_block_friend_rsp(int error, int friendUid);

    void slot_image_ready(int senderUid, QString savedPath);
    void slot_emoji_clicked(const QString &imagePath);

    void slot_minio_file_incoming(int senderUid, QString fileKey,
                                  QString filename, qint64 fileSize,
                                  QString downloadUrl,
                                  bool isVoice, int durationMs);

    void slot_minio_upload_done(QString fileKey, QString filename, qint64 fileSize);

    void slot_open_profile_edit();
    void slot_update_profile_rsp(int error, QString name, QString signature);

    void slot_toggle_record_voice();
    void slot_cancel_record_voice();
    void slot_warmup_audio_input();
    void slot_voice_ready(int senderUid, QString savedPath, int durationMs);

    QPixmap makeAvatar(const QString &path, int size = 40);

private:
    Ui::chatDialog *ui = nullptr;

    ChatUIMode _mode = ChatMode;
    ChatUIMode _state = ChatMode;
    bool _b_loading = false;
    int _current_page = 0;
    QString _current_chat_key;
    int _current_chat_uid = 0;
    QString _current_group_id;

    QStackedWidget *_list_stack = nullptr;
    Chat_User_List *_chat_user_list = nullptr;
    ContactListPanel *_contact_panel = nullptr;

    QStackedWidget *_right_stack = nullptr;
    FriendVerifyPanel *_verify_panel = nullptr;
    FriendProfilePanel *_profile_panel = nullptr;

    QWidget *_search_list_page = nullptr;
    QScrollArea *_search_scroll = nullptr;
    QWidget *_search_content = nullptr;
    QVBoxLayout *_search_layout = nullptr;
    QWidget *_search_hint = nullptr;

    QVBoxLayout *_msg_host_layout = nullptr;
    QVBoxLayout *_msg_layout = nullptr;
    QStackedWidget *_msg_stack = nullptr;
    QWidget *_empty_msg_page = nullptr;
    QMap<QString, QWidget*> _conversation_pages;
    QMap<QString, QVBoxLayout*> _conversation_layouts;
    QSet<QString> _dirty_conversations;

    QPixmap _self_avatar;
    QPixmap _peer_avatar;

    QLabel *_net_status_bar = nullptr;
    QTimer *_countdown_timer = nullptr;
    int _reconnect_countdown_sec = 0;

    EmojiPickerWidget *_emoji_picker = nullptr;

    QMap<QString, QString> _pending_files;
    QMap<QString, QPointer<ChatBubbleItem>> _active_streams;
    QMap<QString, QString> _stream_text_accum;
    QMap<QString, QString> _stream_pending_chunks;
    QMap<QString, QString> _stream_conversation_keys;
    QTimer *_stream_flush_timer = nullptr;

    VideoCallDialog *_call_dialog = nullptr;
    VoiceCallDialog *_voice_call_dialog = nullptr;

    QMediaPlayer *_voice_player = nullptr;
    QAudioOutput *_voice_audio_output = nullptr;
    QPushButton *_playing_voice_btn = nullptr;
    QString _playing_voice_path;
    QHash<QPushButton*, int> _voice_btn_duration_sec;

    bool _is_recording_voice = false;
    QAudioSource *_voice_recorder = nullptr;
    QAudioFormat _voice_fmt;
    QFile *_voice_record_file = nullptr;
    QString _record_voice_path;
    QString _record_conversation_key;
    int _record_target_uid = 0;
    qint64 _record_start_ms = 0;
    QTimer *_record_max_timer = nullptr;
};

#endif // CHATDIALOG_H
