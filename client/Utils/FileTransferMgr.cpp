#include "FileTransferMgr.h"
#include "tcpmgr.h"
#include "global.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QNetworkRequest>
#include <QFile>
#include <QDateTime>

FileTransferMgr::FileTransferMgr(QObject *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
{}

void FileTransferMgr::SendFileMinio(int receiverUid, const QString &filePath,
                                    bool isImage, bool isVoice, int durationMs)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit sig_upload_error("", QString::fromUtf8("文件不存在：") + filePath);
        return;
    }

    FileTransfer::MinioUploadSession sess;
    sess.receiver_uid = receiverUid;
    sess.file_path    = filePath;
    sess.filename     = fi.fileName();
    sess.file_size    = fi.size();
    sess.is_image     = isImage;
    sess.is_voice     = isVoice;
    sess.duration_ms  = durationMs;

    QString pendingKey = QString("minio_pending_%1_%2")
            .arg(fi.fileName())
            .arg(QDateTime::currentMSecsSinceEpoch());
    _minioUploads[pendingKey] = sess;

    QJsonObject req;
    req["receiver_uid"] = receiverUid;
    req["filename"]     = fi.fileName();
    req["size"]         = static_cast<qint64>(fi.size());
    req["is_image"]     = isImage;
    req["is_voice"]     = isVoice;
    req["duration_ms"]  = durationMs;

    TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_INIT_REQ, req);

    qDebug() << "[MinIO] INIT_REQ sent:" << fi.fileName()
             << "size=" << fi.size()
             << "isImage=" << isImage
             << "isVoice=" << isVoice
             << "durationMs=" << durationMs;
}

void FileTransferMgr::OnMinioInitRsp(const QJsonObject &json)
{
    QString fileKey   = json["file_key"].toString();
    QString uploadUrl = json["upload_url"].toString();

    QString pendingKey;
    for (auto it = _minioUploads.begin(); it != _minioUploads.end(); ++it) {
        if (it.key().startsWith("minio_pending_")) {
            pendingKey = it.key();
            break;
        }
    }
    if (pendingKey.isEmpty()) {
        qWarning() << "[MinIO] OnMinioInitRsp: no pending session";
        return;
    }

    FileTransfer::MinioUploadSession sess = _minioUploads.take(pendingKey);
    sess.file_key = fileKey;
    _minioUploads[fileKey] = sess;

    doMinioUpload(fileKey, uploadUrl, sess.file_path);
}

void FileTransferMgr::doMinioUpload(const QString &fileKey,
                                    const QString &uploadUrl,
                                    const QString &filePath)
{
    auto *file = new QFile(filePath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        emit sig_upload_error(fileKey, QString::fromUtf8("无法读取文件：") + filePath);
        file->deleteLater();
        return;
    }

    QNetworkRequest req{QUrl(uploadUrl)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setHeader(QNetworkRequest::ContentLengthHeader, file->size());

    QNetworkReply *reply = _nam->put(req, file);
    file->setParent(reply);

    connect(reply, &QNetworkReply::uploadProgress,
            this, [this, fileKey](qint64 sent, qint64 total) {
        emit sig_upload_progress(fileKey, static_cast<int>(sent / 1024), static_cast<int>(total / 1024));
    });

    connect(reply, &QNetworkReply::finished,
            this, [this, reply, fileKey]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit sig_upload_error(fileKey, reply->errorString());
            _minioUploads.remove(fileKey);
            return;
        }

        if (!_minioUploads.contains(fileKey)) return;
        const auto &sess = _minioUploads[fileKey];

        QJsonObject commit;
        commit["file_key"]     = fileKey;
        commit["filename"]     = sess.filename;
        commit["size"]         = static_cast<qint64>(sess.file_size);
        commit["receiver_uid"] = sess.receiver_uid;
        commit["is_image"]     = sess.is_image;
        commit["is_voice"]     = sess.is_voice;
        commit["duration_ms"]  = sess.duration_ms;

        TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_COMMIT, commit);
    });
}

void FileTransferMgr::OnMinioCommitRsp(const QJsonObject &json)
{
    QString fileKey = json["file_key"].toString();
    int error = json["error"].toInt(-1);

    QString filename;
    qint64 fileSize = 0;
    if (_minioUploads.contains(fileKey)) {
        filename = _minioUploads[fileKey].filename;
        fileSize = _minioUploads[fileKey].file_size;
    }

    _minioUploads.remove(fileKey);

    if (error == 0) {
        emit sig_upload_done(fileKey);
        if (!filename.isEmpty()) {
            emit sig_minio_upload_done(fileKey, filename, fileSize);
        }
    } else {
        emit sig_upload_error(fileKey, QString::fromUtf8("服务端提交失败 error=") + QString::number(error));
    }
}

void FileTransferMgr::OnMinioNotify(const QJsonObject &json)
{
    int senderUid       = json["sender_uid"].toInt();
    QString fileKey     = json["file_key"].toString();
    QString filename    = json["filename"].toString();
    qint64 size         = json["size"].toVariant().toLongLong();
    QString downloadUrl = json["download_url"].toString();
    bool isImage        = json["is_image"].toBool(false);
    bool isVoice        = json["is_voice"].toBool(false);
    int durationMs      = json["duration_ms"].toInt(0);

    FileTransfer::MinioDownloadSession ds;
    ds.file_key     = fileKey;
    ds.filename     = filename;
    ds.file_size    = size;
    ds.download_url = downloadUrl;
    ds.is_image     = isImage;
    ds.is_voice     = isVoice;
    ds.duration_ms  = durationMs;
    ds.sender_uid   = senderUid;
    _minioDownloads[fileKey] = ds;

    qDebug() << "[MinIO] Incoming"
             << (isVoice ? "voice" : (isImage ? "image" : "file"))
             << ":" << filename
             << "from uid=" << senderUid
             << "size=" << size
             << "durationMs=" << durationMs;

    if (isImage || isVoice) {
        QString subDir = isImage ? "weixinchat_images" : "weixinchat_voice";
        QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/" + subDir;
        QDir().mkpath(tmpDir);

        QString savePath = tmpDir + "/" + fileKey.split('/').last() + "_" + filename;
        AcceptMinioDownload(fileKey, savePath);
        return; // 图片/语音由下载完成信号交付 UI
    }

    // 普通文件：交给 UI 自己决定何时下载
    emit sig_minio_file_incoming(senderUid, fileKey, filename, size, downloadUrl, false, 0);
}

void FileTransferMgr::AcceptMinioDownload(const QString &fileKey, const QString &savePath)
{
    if (!_minioDownloads.contains(fileKey)) {
        emit sig_download_error(fileKey, "No incoming session for: " + fileKey);
        return;
    }

    auto &sess = _minioDownloads[fileKey];
    sess.save_path = savePath;

    doMinioDownload(fileKey, sess.download_url, savePath);
}

void FileTransferMgr::doMinioDownload(const QString &fileKey,
                                      const QString &downloadUrl,
                                      const QString &savePath)
{
    QNetworkRequest req{QUrl(downloadUrl)};
    QNetworkReply *reply = _nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress,
            this, [this, fileKey](qint64 recv, qint64 total) {
        if (total > 0) emit sig_download_progress(fileKey, static_cast<int>(recv / 1024), static_cast<int>(total / 1024));
    });

    connect(reply, &QNetworkReply::finished,
            this, [this, reply, fileKey, savePath]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit sig_download_error(fileKey, reply->errorString());
            _minioDownloads.remove(fileKey);
            return;
        }

        QByteArray data = reply->readAll();
        QFile f(savePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit sig_download_error(fileKey, QString::fromUtf8("无法写入文件：") + savePath);
            _minioDownloads.remove(fileKey);
            return;
        }
        f.write(data);
        f.close();

        bool isImage = false;
        bool isVoice = false;
        int senderUid = 0;
        int durationMs = 0;
        if (_minioDownloads.contains(fileKey)) {
            isImage = _minioDownloads[fileKey].is_image;
            isVoice = _minioDownloads[fileKey].is_voice;
            senderUid = _minioDownloads[fileKey].sender_uid;
            durationMs = _minioDownloads[fileKey].duration_ms;
            _minioDownloads.remove(fileKey);
        }

        if (isImage) {
            emit sig_image_ready(senderUid, savePath);
        } else if (isVoice) {
            emit sig_voice_ready(senderUid, savePath, durationMs);
        } else {
            emit sig_download_done(fileKey, savePath);
        }
    });
}

void FileTransferMgr::OnMinioRefreshRsp(const QJsonObject &json)
{
    QString fileKey     = json["file_key"].toString();
    QString downloadUrl = json["download_url"].toString();

    if (!_minioDownloads.contains(fileKey)) {
        qWarning() << "[MinIO] OnMinioRefreshRsp: no session for key=" << fileKey;
        return;
    }

    auto &sess = _minioDownloads[fileKey];
    sess.download_url = downloadUrl;

    doMinioDownload(fileKey, downloadUrl, sess.save_path);
}