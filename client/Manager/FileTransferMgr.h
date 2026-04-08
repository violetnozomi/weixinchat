#ifndef FILETRANSFERMGR_H
#define FILETRANSFERMGR_H

#include <QObject>
#include <QFile>
#include <QMap>
#include <QSet>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "Singleton.h"

class QJsonObject;

namespace FileTransfer {

struct MinioUploadSession {
    int     receiver_uid = 0;
    QString file_key;
    QString file_path;
    QString filename;
    qint64  file_size    = 0;
    bool    is_image     = false;
    bool    is_voice     = false;
    int     duration_ms  = 0;
};

struct MinioDownloadSession {
    QString file_key;
    QString filename;
    qint64  file_size    = 0;
    QString download_url;
    QString save_path;
    bool    is_image     = false;
    bool    is_voice     = false;
    int     duration_ms  = 0;
    int     sender_uid   = 0;
};

// 分段上传状态（用于 > 20 MB 的大文件，支持断点续传）
struct MultipartState {
    QString file_key;          // 由 INIT_RSP / RESUME_RSP 赋値
    QString upload_id;         // MinIO multipart upload ID
    QString local_path;        // 本地文件路径
    QString filename;
    QString client_upload_id;  // _mpPending map 的 key
    QString resume_file;       // 断点续传 JSON 文件路径
    qint64  file_size    = 0;
    int     part_size    = 8 * 1024 * 1024;
    int     total_parts  = 0;
    int     receiver_uid = 0;
    int     duration_ms  = 0;
    bool    is_image     = false;
    bool    is_voice     = false;
    QString file_md5;                 // 头尾各 1 MB 的 MD5 签名，防同名异体续传
    QMap<int, QString> pending_urls;  // part_number → presigned PUT URL
    QMap<int, QString> done_parts;    // part_number → ETag
    QSet<int>          active_parts;  // 正在上传中的分片号
};

}

class FileTransferMgr : public QObject, public Singleton<FileTransferMgr>
{
    Q_OBJECT
    friend class Singleton<FileTransferMgr>;

public:
    void OnMinioInitRsp   (const QJsonObject &json);
    void OnMinioCommitRsp (const QJsonObject &json);
    void OnMinioNotify    (const QJsonObject &json);
    void OnMinioRefreshRsp(const QJsonObject &json);

    // 分段上传响应处理
    void OnMpInitRsp   (const QJsonObject &json);
    void OnMpCompleteRsp(const QJsonObject &json);
    void OnMpResumeRsp (const QJsonObject &json);

public slots:
    void SendFileMinio(int receiverUid, const QString &filePath,
                       bool isImage = false, bool isVoice = false,
                       int durationMs = 0);
    void AcceptMinioDownload(const QString &fileKey, const QString &savePath);

signals:
    void sig_upload_progress(QString fileId, int sent, int total);
    void sig_upload_done(QString fileId);
    void sig_upload_error(QString fileId, QString reason);

    void sig_image_ready(int senderUid, QString savedPath);
    void sig_voice_ready(int senderUid, QString savedPath, int durationMs);

    void sig_download_progress(QString fileId, int recv, int total);
    void sig_download_done(QString fileId, QString savedPath);
    void sig_download_error(QString fileId, QString reason);

    // 普通文件通知（语音/图片不在这里交付 UI）
    void sig_minio_file_incoming(int senderUid, QString fileKey,
                                 QString filename, qint64 size,
                                 QString downloadUrl,
                                 bool isVoice, int durationMs);

    void sig_minio_upload_done(QString fileKey, QString filename, qint64 fileSize);

private:
    explicit FileTransferMgr(QObject *parent = nullptr);

    void doMinioUpload(const QString &fileKey, const QString &uploadUrl,
                       const QString &filePath);
    void doMinioDownload(const QString &fileKey, const QString &downloadUrl,
                         const QString &savePath);

    // 分段上传辅助方法
    void _sendFileMinioMultipart(int receiverUid, const QString &filePath,
                                 bool isImage, bool isVoice, int durationMs);
    bool _tryResumeMultipart    (int receiverUid, const QString &filePath,
                                 bool isImage, bool isVoice, int durationMs);
    void _startNextParts        (const QString &fileKey);
    void _uploadOnePart         (const QString &fileKey, int partNum);
    void _sendCompleteReq       (const QString &fileKey);
    void _saveResumeState       (const QString &fileKey);

    QNetworkAccessManager* _nam = nullptr;

    QMap<QString, FileTransfer::MinioUploadSession>   _minioUploads;
    QMap<QString, FileTransfer::MinioDownloadSession> _minioDownloads;
    QMap<QString, FileTransfer::MultipartState>       _multiparts;  // keyed by file_key
    QMap<QString, FileTransfer::MultipartState>       _mpPending;   // keyed by client_upload_id
};

#endif // FILETRANSFERMGR_H