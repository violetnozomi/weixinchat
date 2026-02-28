#ifndef CHATDIALOG_H
#define CHATDIALOG_H

#include <QDialog>
#include <QAction>
#include <QIcon>
#include <QEvent>
#include <QLabel>
#include <QTimer>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTextEdit>
#include <QDateTime>
#include <QMap>
#include <QVector>
#include <QJsonArray>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QPushButton>
#include <QHash>

#include "global.h"
#include "chat_user_list.h"
#include "chatbubbleitem.h"
#include "searchuseritem.h"
#include "addfriendwidget.h"
#include "contactlistpanel.h"
#include "friendverifypanel.h"
#include "friendprofilepanel.h"
#include "ChatMgr.h"
#include "FriendMgr.h"
#include "UserMgr.h"
#include "emojipickerwidget.h"

class VideoCallDialog;

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

    void AppendMessage(const QString &text, const QPixmap &avatar,
                       bool isSelf, const QString &timeStr = "");

    void AppendImageMessage(const QPixmap &image, const QPixmap &avatar,
                            bool isSelf, const QString &savedPath = "",
                            const QString &timeStr = "");

    void AppendFileMessage(const QString &filename, qint64 fileSize,
                           const QString &fileKey, const QString &downloadUrl,
                           const QPixmap &avatar, bool isSelf,
                           const QString &timeStr = "");

    void AppendVoiceMessage(const QString &voicePath, int durationMs,
                            const QPixmap &avatar, bool isSelf,
                            const QString &timeStr = "");

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    ChatUIMode _mode;
    ChatUIMode _state;
    bool _b_loading;
    int _current_page;
    int _current_chat_uid;

    void ShowSearch(bool b_search);
    void AddChatUserList();
    void SendMessage();
    void SwitchToChat(int uid);
    void SaveCurrentMessages();
    void DoSearch(const QString &keyword);

private slots:
    void slot_loading_chat_user();
    void slot_search_focus_in();
    void slot_search_focus_out();
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
    void slot_text_msg_received(int from_uid, QString content, qint64 timestamp);

    void slot_delete_friend(int uid);
    void slot_block_friend (int uid);
    void slot_delete_friend_rsp(int error, int friendUid);
    void slot_block_friend_rsp (int error, int friendUid);

    void slot_image_ready(int senderUid, QString savedPath);
    void slot_emoji_clicked(const QString& imagePath);

    void slot_minio_file_incoming(int senderUid, QString fileKey,
                                  QString filename, qint64 fileSize,
                                  QString downloadUrl,
                                  bool isVoice, int durationMs);

    void slot_minio_upload_done(QString fileKey, QString filename, qint64 fileSize);

    void slot_open_profile_edit();
    void slot_update_profile_rsp(int error, QString name, QString signature);
    
    QPixmap makeAvatar(const QString &path, int size = 40);

private:
    Ui::chatDialog *ui = nullptr;

    QStackedWidget  *_list_stack = nullptr;
    Chat_User_List  *_chat_user_list = nullptr;
    ContactListPanel *_contact_panel = nullptr;

    QStackedWidget     *_right_stack = nullptr;
    FriendVerifyPanel  *_verify_panel = nullptr;
    FriendProfilePanel *_profile_panel = nullptr;

    QWidget         *_search_list_page = nullptr;
    QScrollArea     *_search_scroll = nullptr;
    QWidget         *_search_content = nullptr;
    QVBoxLayout     *_search_layout = nullptr;
    QWidget         *_search_hint = nullptr;

    QVBoxLayout     *_msg_layout = nullptr;

    QPixmap          _self_avatar;
    QPixmap          _peer_avatar;

    QLabel          *_net_status_bar = nullptr;
    QTimer          *_countdown_timer = nullptr;
    int              _reconnect_countdown_sec = 0;

    EmojiPickerWidget *_emoji_picker = nullptr;

    QMap<QString, QString> _pending_files;

    VideoCallDialog *_call_dialog = nullptr;

    QMediaPlayer *_voice_player = nullptr;
    QAudioOutput *_voice_audio_output = nullptr;
    QPushButton  *_playing_voice_btn = nullptr;
    QString       _playing_voice_path;
    QHash<QPushButton*, int> _voice_btn_duration_sec;
};

#endif // CHATDIALOG_H
