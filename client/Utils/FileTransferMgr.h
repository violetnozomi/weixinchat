#ifndef FILETRANSFERMGR_H
#define FILETRANSFERMGR_H

#include <QObject>
#include <QFile>
#include <QMap>
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

    QNetworkAccessManager* _nam = nullptr;

    QMap<QString, FileTransfer::MinioUploadSession>   _minioUploads;
    QMap<QString, FileTransfer::MinioDownloadSession> _minioDownloads;
};

#endif // FILETRANSFERMGR_H