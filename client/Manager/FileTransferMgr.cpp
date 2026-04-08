#include "FileTransferMgr.h"
#include "tcpmgr.h"
#include "global.h"
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QNetworkRequest>
#include <QFile>
#include <QDateTime>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QBuffer>

FileTransferMgr::FileTransferMgr(QObject *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
{
    // 每 60 秒清理 5 分钟无响应的 pending 上传会话（防 map 无限增长）
    auto *gc = new QTimer(this);
    gc->setInterval(60000);
    connect(gc, &QTimer::timeout, this, [this]() {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = _minioUploads.begin(); it != _minioUploads.end(); ) {
            if (it.key().startsWith("minio_pending_")) {
                // key 格式: minio_pending_<filename>_<epochMs>
                const qint64 created = it.key().section('_', -1).toLongLong();
                if (created > 0 && (now - created) > 5 * 60 * 1000) {
                    qWarning() << "[MinIO] GC stale pending upload:" << it.key();
                    it = _minioUploads.erase(it);
                    continue;
                }
            }
            ++it;
        }
    });
    gc->start();
}

static constexpr qint64 MULTIPART_THRESHOLD  = 20LL * 1024 * 1024;  // 20 MB
static constexpr int    MULTIPART_PART_SIZE   = 8 * 1024 * 1024;     // 8 MB per part
static constexpr int    MAX_CONCURRENT_PARTS  = 3;

void FileTransferMgr::SendFileMinio(int receiverUid, const QString &filePath,
                                    bool isImage, bool isVoice, int durationMs)
{
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit sig_upload_error("", QString::fromUtf8("文件不存在：") + filePath);
        return;
    }

    // 大文件走分段上传通道
    if (fi.size() > MULTIPART_THRESHOLD) {
        if (!_tryResumeMultipart(receiverUid, filePath, isImage, isVoice, durationMs))
            _sendFileMinioMultipart(receiverUid, filePath, isImage, isVoice, durationMs);
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
    req["receiver_uid"]     = receiverUid;
    req["filename"]         = fi.fileName();
    req["size"]             = static_cast<qint64>(fi.size());
    req["is_image"]         = isImage;
    req["is_voice"]         = isVoice;
    req["duration_ms"]      = durationMs;
    req["client_upload_id"] = pendingKey;   // P2: deterministic match key

    TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_INIT_REQ, req);
}

void FileTransferMgr::OnMinioInitRsp(const QJsonObject &json)
{
    QString fileKey        = json["file_key"].toString();
    QString uploadUrl      = json["upload_url"].toString();
    // P2: use client_upload_id for deterministic session matching
    QString clientUploadId = json["client_upload_id"].toString();

    QString pendingKey;
    if (!clientUploadId.isEmpty() && _minioUploads.contains(clientUploadId)) {
        pendingKey = clientUploadId;
    } else {
        // Fallback: scan for any pending key (handles old server without client_upload_id)
        for (auto it = _minioUploads.begin(); it != _minioUploads.end(); ++it) {
            if (it.key().startsWith("minio_pending_")) {
                pendingKey = it.key();
                break;
            }
        }
    }
    if (pendingKey.isEmpty()) {
        qWarning() << "[MinIO] OnMinioInitRsp: no pending session for file_key=" << fileKey;
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

    // 使用 QFile* 流式上传（避免大文件占用大量内存），并显式禁用 Expect:100-continue。
    // 不使用 QByteArray 方式：Qt 内部会将大 QByteArray 封装为 QBuffer，
    // 在大文件场景下 Qt HTTP 分块逻辑会导致 MinIO 在收到第一包后 RST 连接。
    // QFile 流式上传（语音文件已验证可行）+ 明确禁用 Expect 头是正确做法。
    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "[MinIO] FAILED to open file:" << filePath;
        delete file;
        emit sig_upload_error(fileKey, QString::fromUtf8("无法读取文件：") + filePath);
        return;
    }
    qint64 fileSize = file->size();

    QNetworkRequest req{QUrl(uploadUrl)};
    // 注意：不设置 Content-Type，预签名 URL 的 SignedHeaders 仅含 "host"。
    req.setHeader(QNetworkRequest::ContentLengthHeader, fileSize);
    req.setRawHeader("Expect", "");  // 显式禁用 100-continue
    req.setTransferTimeout(30000);   // 弱网 30 s 超时保护

    QNetworkReply *reply = _nam->put(req, file);
    file->setParent(reply); // QFile 随 reply 一起释放，避免内存泄漏

    connect(reply, &QNetworkReply::uploadProgress,
            this, [this, fileKey](qint64 sent, qint64 total) {
        emit sig_upload_progress(fileKey, static_cast<int>(sent / 1024), static_cast<int>(total / 1024));
    });

    connect(reply, &QNetworkReply::finished,
            this, [this, reply, fileKey]() {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray errBody = reply->readAll().left(512);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[MinIO] PUT failed: httpStatus=" << httpStatus
                       << reply->errorString()
                       << QString::fromUtf8(errBody);
            emit sig_upload_error(fileKey, reply->errorString() + " | " + QString::fromUtf8(errBody));
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

    if (isImage || isVoice) {
        const QString baseDir = isVoice
            ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            : QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString subDir = isImage ? "weixinchat_images" : "weixinchat_voice";
        const QString targetDir = baseDir + "/" + subDir;
        QDir().mkpath(targetDir);

        const QString savePath = targetDir + "/" + fileKey.split('/').last() + "_" + filename;
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
    req.setTransferTimeout(30000);   // 弱网 30 s 超时保护
    QNetworkReply *reply = _nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress,
            this, [this, fileKey](qint64 recv, qint64 total) {
        if (total > 0) emit sig_download_progress(fileKey, static_cast<int>(recv / 1024), static_cast<int>(total / 1024));
    });

    auto *file = new QFile(savePath, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[MinIO] FAILED to create save file:" << savePath;
        emit sig_download_error(fileKey, QString::fromUtf8("无法创建文件：") + savePath);
        reply->abort();        // 文件建失败了，直接掐断网络请求
        reply->deleteLater();
        file->deleteLater();
        return;
    }
    file->setParent(reply); // 极其关键！让文件的生命周期和网络请求绑定

    // ==========================================
    // 🔪 手术二：接水管！利用 readyRead 边下边存
    // ==========================================
    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        // 只要网卡收到了一小块数据（哪怕只有几 KB），这个信号就会触发
        if (file->isOpen()) {
            file->write(reply->readAll()); // 榨干当前缓冲区的水，立刻写进硬盘！
        }
    });

    // 进度条逻辑已在上方连接，此处不再重复

    // ==========================================
    // 🔪 手术三：安全收尾
    // ==========================================
    connect(reply, &QNetworkReply::finished,
            this, [this, reply, fileKey, file, savePath]() {

                if (file->isOpen()) {
                    file->close();
                }

                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    emit sig_download_error(fileKey, reply->errorString());
                    _minioDownloads.remove(fileKey);
                    return;
                }

                // --- 下面是你极其完美的路由分发逻辑（完全不用动） ---
                bool isImage = false; bool isVoice = false; int senderUid = 0; int durationMs = 0;
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

// ═══════════════════════════════════════════════════════════════════════════
// 分段上传（Multipart Upload）— 大文件（> 20 MB）专用逻辑
// ═══════════════════════════════════════════════════════════════════════════

// ── 辅助：临时目录路径 ─────────────────────────────────────────────────────
static QString mpResumeDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + "/weixinchat_multipart";
}

// ── 辅助：计算文件头尾各 1 MB 的 MD5 签名（防同名异体文件导致错误续传）────────
static QString computeFileMd5Signature(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    const qint64 SAMPLE   = 1LL * 1024 * 1024;
    const qint64 fileSize = file.size();
    QCryptographicHash hash(QCryptographicHash::Md5);

    hash.addData(file.read(qMin(SAMPLE, fileSize)));  // 头部 1 MB
    if (fileSize > 2 * SAMPLE) {                      // 尾部 1 MB（不与头部重叠）
        file.seek(fileSize - SAMPLE);
        hash.addData(file.read(SAMPLE));
    } else if (file.pos() < fileSize) {               // 1–2 MB 之间：读取剩余部分
        hash.addData(file.readAll());
    }
    return QString::fromLatin1(hash.result().toHex());
}

// ── 尝试恢复上次未完成的分段上传 ────────────────────────────────────────────
// 扫描 resume JSON 文件；若找到匹配项（local_path + file_size + MD5）则发送 RESUME_REQ
bool FileTransferMgr::_tryResumeMultipart(int receiverUid, const QString &filePath,
                                           bool isImage, bool isVoice, int durationMs)
{
    QDir dir(mpResumeDir());
    if (!dir.exists()) return false;

    const auto entries = dir.entryInfoList(QStringList{"*.json"}, QDir::Files);
    for (const auto &entry : entries) {
        QFile f(entry.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isNull()) continue;

        QJsonObject obj = doc.object();
        if (obj["local_path"].toString() != filePath) continue;
        if (obj["file_size"].toVariant().toLongLong() != QFileInfo(filePath).size()) continue;

        // MD5 签名校验：防止同名异体文件导致错误的分片拼接
        const QString storedMd5 = obj["file_md5"].toString();
        if (!storedMd5.isEmpty() && computeFileMd5Signature(filePath) != storedMd5) continue;

        // 找到匹配项，重建状态并请求续传
        FileTransfer::MultipartState st;
        st.file_key    = obj["file_key"].toString();
        st.upload_id   = obj["upload_id"].toString();
        st.local_path  = filePath;
        st.filename    = obj["filename"].toString();
        st.file_size   = obj["file_size"].toVariant().toLongLong();
        st.part_size   = obj["part_size"].toInt(MULTIPART_PART_SIZE);
        st.total_parts = obj["total_parts"].toInt();
        st.receiver_uid = receiverUid;
        st.is_image    = isImage;
        st.is_voice    = isVoice;
        st.duration_ms = durationMs;
        st.resume_file = entry.absoluteFilePath();
        // done_parts 不从文件加载，以服务端 ListParts 为准

        _multiparts[st.file_key] = st;

        QJsonObject req;
        req["file_key"]    = st.file_key;
        req["upload_id"]   = st.upload_id;
        req["total_parts"] = st.total_parts;
        TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_MP_RESUME_REQ, req);
        return true;
    }
    return false;
}

// ── 新建分段上传（无可续传记录时） ──────────────────────────────────────────
void FileTransferMgr::_sendFileMinioMultipart(int receiverUid, const QString &filePath,
                                               bool isImage, bool isVoice, int durationMs)
{
    QFileInfo fi(filePath);
    int totalParts = static_cast<int>(
        (fi.size() + MULTIPART_PART_SIZE - 1) / MULTIPART_PART_SIZE);

    FileTransfer::MultipartState st;
    st.local_path       = filePath;
    st.filename         = fi.fileName();
    st.file_size        = fi.size();
    st.part_size        = MULTIPART_PART_SIZE;
    st.total_parts      = totalParts;
    st.receiver_uid     = receiverUid;
    st.is_image         = isImage;
    st.is_voice         = isVoice;
    st.duration_ms      = durationMs;
    st.file_md5         = computeFileMd5Signature(filePath);  // MD5 签名，防同名异体
    st.client_upload_id = QString("mp_%1_%2")
            .arg(fi.fileName())
            .arg(QDateTime::currentMSecsSinceEpoch());

    _mpPending[st.client_upload_id] = st;

    QJsonObject req;
    req["receiver_uid"]     = receiverUid;
    req["filename"]         = fi.fileName();
    req["size"]             = fi.size();
    req["total_parts"]      = totalParts;
    req["part_size"]        = MULTIPART_PART_SIZE;
    req["is_image"]         = isImage;
    req["is_voice"]         = isVoice;
    req["duration_ms"]      = durationMs;
    req["client_upload_id"] = st.client_upload_id;
    TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_MP_INIT_REQ, req);
}

// ── 收到 INIT_RSP：保存 part URL，启动并发上传 ───────────────────────────────
void FileTransferMgr::OnMpInitRsp(const QJsonObject &json)
{
    int     error        = json["error"].toInt(0);
    QString fileKey      = json["file_key"].toString();
    QString uploadId     = json["upload_id"].toString();
    int     partSize     = json["part_size"].toInt(MULTIPART_PART_SIZE);
    int     totalParts   = json["total_parts"].toInt();
    QString clientUpId   = json["client_upload_id"].toString();
    QJsonObject partUrls = json["part_urls"].toObject();

    if (error != 0) {
        _mpPending.remove(clientUpId);
        emit sig_upload_error(fileKey, json["msg"].toString("Multipart init failed"));
        return;
    }

    if (!_mpPending.contains(clientUpId)) {
        qWarning() << "[MP] OnMpInitRsp: no pending state for client_upload_id=" << clientUpId;
        return;
    }

    FileTransfer::MultipartState st = _mpPending.take(clientUpId);
    st.file_key    = fileKey;
    st.upload_id   = uploadId;
    st.part_size   = partSize;
    st.total_parts = totalParts;

    // 生成续传文件路径
    QDir().mkpath(mpResumeDir());
    st.resume_file = mpResumeDir() + "/"
                     + fileKey.split('/').last()
                     + "_" + uploadId.left(8) + ".json";

    for (auto it = partUrls.begin(); it != partUrls.end(); ++it)
        st.pending_urls[it.key().toInt()] = it.value().toString();

    _multiparts[fileKey] = st;
    _saveResumeState(fileKey);
    _startNextParts(fileKey);
}

// ── 收到 RESUME_RSP：合并已完成分片，续传剩余部分 ────────────────────────────
void FileTransferMgr::OnMpResumeRsp(const QJsonObject &json)
{
    QString     fileKey       = json["file_key"].toString();
    QJsonArray  doneParts     = json["done_parts"].toArray();
    QJsonObject remainingUrls = json["remaining_part_urls"].toObject();

    if (!_multiparts.contains(fileKey)) {
        qWarning() << "[MP] OnMpResumeRsp: no state for file_key=" << fileKey;
        return;
    }

    auto &st = _multiparts[fileKey];

    // 以服务端 ListParts 结果为准
    st.done_parts.clear();
    for (const auto &p : doneParts) {
        QJsonObject part = p.toObject();
        st.done_parts[part["part_number"].toInt()] = part["etag"].toString();
    }

    st.pending_urls.clear();
    for (auto it = remainingUrls.begin(); it != remainingUrls.end(); ++it)
        st.pending_urls[it.key().toInt()] = it.value().toString();

    if (st.pending_urls.isEmpty()) {
        // 全部分片已上传，直接提交
        _sendCompleteReq(fileKey);
        return;
    }

    _startNextParts(fileKey);
}

// ── 启动最多 MAX_CONCURRENT_PARTS 个并发分片上传 ──────────────────────────────
void FileTransferMgr::_startNextParts(const QString &fileKey)
{
    if (!_multiparts.contains(fileKey)) return;
    auto &st = _multiparts[fileKey];

    while (st.active_parts.size() < MAX_CONCURRENT_PARTS
           && !st.pending_urls.isEmpty())
    {
        int partNum = st.pending_urls.firstKey();
        _uploadOnePart(fileKey, partNum);
    }
}

// ── 上传单个分片（后台线程读文件 + 主线程 PUT + QBuffer 对象树托管）──────────────
void FileTransferMgr::_uploadOnePart(const QString &fileKey, int partNum)
{
    if (!_multiparts.contains(fileKey)) return;
    auto &st = _multiparts[fileKey];
    if (!st.pending_urls.contains(partNum)) return;

    st.active_parts.insert(partNum);

    // 跨线程数据全部按值捕获，消除悬垂引用风险
    const qint64  offset       = static_cast<qint64>(partNum - 1) * st.part_size;
    const qint64  readSize     = qMin(static_cast<qint64>(st.part_size), st.file_size - offset);
    const QString localPath    = st.local_path;
    const QString presignedUrl = st.pending_urls[partNum];

    // ── 步骤 1：线程池异步读取分片，彻底解绑主线程 I/O ──────────────────────────
    auto *watcher = new QFutureWatcher<QByteArray>(this);

    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher, fileKey, partNum, presignedUrl, readSize]() {

        watcher->deleteLater();
        const QByteArray data = watcher->result();

        if (!_multiparts.contains(fileKey)) return;
        auto &st = _multiparts[fileKey];

        if (data.size() != readSize) {
            st.active_parts.remove(partNum);
            emit sig_upload_error(fileKey,
                QString("Failed to read part %1 (expected %2 bytes, got %3)")
                    .arg(partNum).arg(readSize).arg(data.size()));
            return;
        }

        // ── 步骤 2：主线程发起 HTTP PUT ──────────────────────────────────────────
        QNetworkRequest req{QUrl(presignedUrl)};
        req.setHeader(QNetworkRequest::ContentLengthHeader, data.size());
        req.setRawHeader("Expect", "");   // 禁用 100-continue
        req.setTransferTimeout(30000);    // 弱网 30 s 超时保护

        // QBuffer 持有数据所有权，以 reply 为 parent 挂入对象树，随 deleteLater 自动销毁
        auto *buffer = new QBuffer();
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);

        QNetworkReply *reply = _nam->put(req, buffer);
        buffer->setParent(reply);         // 对象树托管，避免手动 delete

        connect(reply, &QNetworkReply::finished, this,
                [this, reply, fileKey, partNum]() {

            reply->deleteLater();
            if (!_multiparts.contains(fileKey)) return;

            auto &st = _multiparts[fileKey];
            st.active_parts.remove(partNum);

            if (reply->error() != QNetworkReply::NoError) {
                int httpStatus = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                qWarning() << "[MP] Part" << partNum << "upload failed:"
                           << reply->errorString() << "HTTP" << httpStatus;
                emit sig_upload_error(fileKey,
                    QString("Part %1 failed: ").arg(partNum) + reply->errorString());
                return;
            }

            // 提取 ETag（MinIO 返回带引号的值，如 "\"abc123\""）
            QString etag = QString::fromLatin1(reply->rawHeader("ETag"));
            if (etag.isEmpty())
                etag = QString::fromLatin1(reply->rawHeader("Etag"));

            st.done_parts[partNum] = etag;
            st.pending_urls.remove(partNum);
            _saveResumeState(fileKey);

            emit sig_upload_progress(fileKey, st.done_parts.size(), st.total_parts);

            if (st.done_parts.size() == st.total_parts && st.active_parts.isEmpty())
                _sendCompleteReq(fileKey);
            else
                _startNextParts(fileKey);
        });
    });

    // localPath / offset / readSize 全部按值拷贝进 lambda，线程安全
    watcher->setFuture(QtConcurrent::run([localPath, offset, readSize]() -> QByteArray {
        QFile file(localPath);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset))
            return {};
        return file.read(readSize);
    }));
}

// ── 发送分段上传完成请求 ──────────────────────────────────────────────────────
void FileTransferMgr::_sendCompleteReq(const QString &fileKey)
{
    if (!_multiparts.contains(fileKey)) return;
    const auto &st = _multiparts[fileKey];

    QJsonObject req;
    req["file_key"]    = st.file_key;
    req["upload_id"]   = st.upload_id;
    req["receiver_uid"]= st.receiver_uid;
    req["filename"]    = st.filename;
    req["size"]        = st.file_size;
    req["is_image"]    = st.is_image;
    req["is_voice"]    = st.is_voice;
    req["duration_ms"] = st.duration_ms;

    QJsonArray parts;
    for (auto it = st.done_parts.constBegin(); it != st.done_parts.constEnd(); ++it) {
        QJsonObject p;
        p["part_number"] = it.key();
        p["etag"]        = it.value();
        parts.append(p);
    }
    req["parts"] = parts;

    TcpMgr::GetInstance()->SendMsg(ID_FILE_MINIO_MP_COMPLETE_REQ, req);
}

// ── 收到 COMPLETE_RSP ─────────────────────────────────────────────────────────
void FileTransferMgr::OnMpCompleteRsp(const QJsonObject &json)
{
    QString fileKey = json["file_key"].toString();
    int     error   = json["error"].toInt(-1);

    QString resumeFile;
    QString filename;
    qint64  fileSize = 0;

    if (_multiparts.contains(fileKey)) {
        resumeFile = _multiparts[fileKey].resume_file;
        filename   = _multiparts[fileKey].filename;
        fileSize   = _multiparts[fileKey].file_size;
        _multiparts.remove(fileKey);
    }

    if (error == 0) {
        if (!resumeFile.isEmpty())
            QFile::remove(resumeFile);  // 成功后删除续传文件
        emit sig_upload_done(fileKey);
        if (!filename.isEmpty())
            emit sig_minio_upload_done(fileKey, filename, fileSize);
    } else {
        // 失败时保留续传文件，供下次重启时断点续传
        emit sig_upload_error(fileKey,
            QString("Multipart complete failed, error=%1").arg(error));
    }
}

// ── 持久化断点续传状态到磁盘 ──────────────────────────────────────────────────
void FileTransferMgr::_saveResumeState(const QString &fileKey)
{
    if (!_multiparts.contains(fileKey)) return;
    const auto &st = _multiparts[fileKey];
    if (st.resume_file.isEmpty()) return;

    QJsonObject obj;
    obj["file_key"]    = st.file_key;
    obj["upload_id"]   = st.upload_id;
    obj["local_path"]  = st.local_path;
    obj["filename"]    = st.filename;
    obj["file_size"]   = st.file_size;
    obj["part_size"]   = st.part_size;
    obj["total_parts"] = st.total_parts;
    obj["receiver_uid"]= st.receiver_uid;
    obj["is_image"]    = st.is_image;
    obj["is_voice"]    = st.is_voice;
    obj["duration_ms"] = st.duration_ms;

    // 仅保存已完成的分片（断点续传只需知道哪些已完成）
    QJsonObject dp;
    for (auto it = st.done_parts.constBegin(); it != st.done_parts.constEnd(); ++it)
        dp[QString::number(it.key())] = it.value();
    obj["done_parts"] = dp;
    obj["file_md5"]   = st.file_md5;  // MD5 签名存盘，续传时校验文件完整性

    // QSaveFile 原子写入：先写临时文件再重命名，崩溃/断电不会损坏断点续传记录
    QSaveFile f(st.resume_file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson());
        f.commit();
    }
}
