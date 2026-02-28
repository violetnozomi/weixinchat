#include "chatbubbleitem.h"
#include <QApplication>
#include <QScreen>
#include <QDialog>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QDesktopServices>
#include <QUrl>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include "FileTransferMgr.h"

/* ---------- 圆形头像 ---------- */
QPixmap ChatBubbleItem::makeRoundAvatar(const QPixmap &src, int size)
{
    QPixmap scaled = src.scaled(size, size,
                                Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    QPixmap result(size, size);
    result.fill(Qt::transparent);
    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addEllipse(0, 0, size, size);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    return result;
}

/* ---------- 气泡文本 Label（自绘圆角矩形背景） ---------- */
class BubbleLabel : public QWidget
{
public:
    explicit BubbleLabel(const QString &text, bool isSelf, QWidget *parent = nullptr)
        : QWidget(parent), _isSelf(isSelf)
    {
        setContentsMargins(12, 8, 12, 8);
        setMaximumWidth(420);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        setFont(QApplication::font());

        _doc.setDefaultFont(font());

        static const QRegularExpression emoRe(R"(\[emo:(\d+)\])");
        QRegularExpressionMatchIterator mi = emoRe.globalMatch(text);
        while (mi.hasNext()) {
            int n = mi.next().captured(1).toInt();
            QImage img(QString(":/res/emo_%1.png").arg(n));
            if (!img.isNull())
                _doc.addResource(
                    QTextDocument::ImageResource,
                    QUrl(QString("emo:%1").arg(n)),
                    img.scaled(36, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }

        _doc.setHtml(buildHtml(text));
        _doc.setTextWidth(380);
    }

    QSize sizeHint() const override
    {
        QSizeF sz = _doc.size();
        return QSize(
            static_cast<int>(sz.width())  + contentsMargins().left() + contentsMargins().right(),
            static_cast<int>(sz.height()) + contentsMargins().top()  + contentsMargins().bottom());
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QColor bgColor = _isSelf ? QColor(0x95, 0xEC, 0x69)
                                 : QColor(0xFF, 0xFF, 0xFF);
        painter.setBrush(bgColor);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(rect(), 10, 10);

        painter.save();
        painter.translate(contentsMargins().left(), contentsMargins().top());
        _doc.drawContents(&painter);
        painter.restore();
    }

    void resizeEvent(QResizeEvent *e) override
    {
        int w = e->size().width()
                - contentsMargins().left() - contentsMargins().right();
        _doc.setTextWidth(qMax(w, 50));
        updateGeometry();
        update();
    }

private:
    static QString buildHtml(const QString &text)
    {
        QString escaped = text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>"));
        // Replace [emo:N] tags with inline <img> — use manual loop since
        // Qt's QString::replace(QRegularExpression, ...) only takes a QString
        static const QRegularExpression re(R"(\[emo:(\d+)\])");
        {
            QString out;
            int last = 0;
            QRegularExpressionMatchIterator it = re.globalMatch(escaped);
            while (it.hasNext()) {
                QRegularExpressionMatch m = it.next();
                out += escaped.mid(last, m.capturedStart() - last);
                out += QString("<img src='emo:%1' width='36' height='36'/>").arg(m.captured(1));
                last = m.capturedEnd();
            }
            out += escaped.mid(last);
            escaped = out;
        }
        return QString(
            "<span style='font-family:\"%1\"; font-size:%2pt; color:#1a1a1a;'>%3</span>")
            .arg(QApplication::font().family())
            .arg(QApplication::font().pointSize())
            .arg(escaped);
    }

    bool            _isSelf;
    mutable QTextDocument _doc;
};


/* ---------- 图片气泡 Label（可点击放大，完美支持 GIF 动图） ---------- */
class ImageBubbleLabel : public QLabel
{
public:
    explicit ImageBubbleLabel(const QPixmap &px, const QString &imagePath,
                              QWidget *parent = nullptr)
        : QLabel(parent), _path(imagePath), _full(px)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QString::fromUtf8("点击查看大图"));

        int maxSide = 200;

        // 【核心修改 1：判断是否为 GIF】
        if (_path.endsWith(".gif", Qt::CaseInsensitive)) {
            QMovie *movie = new QMovie(_path, QByteArray(), this);
            // 坑点：QMovie 需要先跳到第0帧，才能获取到真实的画面尺寸
            movie->jumpToFrame(0);
            QSize originSize = movie->frameRect().size();
            if(originSize.isEmpty()) originSize = QSize(100, 100); // 兜底防御

            // 按比例计算缩放后的尺寸
            QSize scaledSize = originSize.scaled(maxSide, maxSide, Qt::KeepAspectRatio);
            movie->setScaledSize(scaledSize); // 让动图缩小播放

            setMovie(movie);
            movie->start(); // 动起来！
            setFixedSize(scaledSize);
        } else {
            // 【静态图保留原有逻辑】
            QPixmap preview = px.scaled(maxSide, maxSide,
                                        Qt::KeepAspectRatio, Qt::SmoothTransformation);
            setPixmap(preview);
            setFixedSize(preview.size());
        }
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) { QLabel::mousePressEvent(e); return; }

        // 弹出全屏预览对话框
        QDialog *dlg = new QDialog(window(), Qt::Window);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle(QString::fromUtf8("图片预览"));
        QVBoxLayout *vbox = new QVBoxLayout(dlg);
        vbox->setContentsMargins(0, 0, 0, 0);

        QLabel *fullLb = new QLabel(dlg);

        // 限制到屏幕的 80%
        QSize screen = QApplication::primaryScreen()->availableSize();
        QSize maxDlgSize(screen.width() * 0.8, screen.height() * 0.8);

        // 【核心修改 2：全屏预览也要支持 GIF】
        if (_path.endsWith(".gif", Qt::CaseInsensitive)) {
            QMovie *movie = new QMovie(_path, QByteArray(), fullLb);
            movie->jumpToFrame(0);
            QSize originSize = movie->frameRect().size();
            if(originSize.isEmpty()) originSize = QSize(400, 400);

            QSize scaledSize = originSize.scaled(maxDlgSize, Qt::KeepAspectRatio);
            // 只有当动图比屏幕限制还要大时才缩放，否则保持原汁原味的大小
            if (originSize.width() > scaledSize.width() || originSize.height() > scaledSize.height()) {
                movie->setScaledSize(scaledSize);
            } else {
                scaledSize = originSize;
            }

            fullLb->setMovie(movie);
            movie->start(); // 全屏动起来！
            fullLb->setAlignment(Qt::AlignCenter);
            vbox->addWidget(fullLb);
            dlg->resize(scaledSize);
        } else {
            // 【静态图全屏逻辑】
            QPixmap scaled = _full.scaled(maxDlgSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            fullLb->setPixmap(scaled);
            fullLb->setAlignment(Qt::AlignCenter);
            vbox->addWidget(fullLb);
            dlg->resize(scaled.size());
        }

        dlg->show();
    }

private:
    QString _path;
    QPixmap _full;
};

/* ---------- ChatBubbleItem (文本) ---------- */
ChatBubbleItem::ChatBubbleItem(const QString &text,
                               const QPixmap &avatar,
                               bool isSelf,
                               const QString &timeStr,
                               QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 6, 12, 6);
    root->setSpacing(8);

    // 头像
    QLabel *avatarLb = new QLabel(this);
    avatarLb->setFixedSize(40, 40);
    avatarLb->setPixmap(makeRoundAvatar(avatar, 40));
    avatarLb->setAlignment(Qt::AlignTop);

    // 内容列（时间 + 气泡�?
    QVBoxLayout *contentCol = new QVBoxLayout;
    contentCol->setContentsMargins(0, 0, 0, 0);
    contentCol->setSpacing(3);

    if (!timeStr.isEmpty()) {
        QLabel *timeLb = new QLabel(timeStr, this);
        timeLb->setObjectName("bubble_time_lb");
        timeLb->setAlignment(isSelf ? Qt::AlignRight : Qt::AlignLeft);
        contentCol->addWidget(timeLb);
    }

    BubbleLabel *bubble = new BubbleLabel(text, isSelf, this);
    contentCol->addWidget(bubble);
    contentCol->addStretch();

    if (isSelf) {
        root->addStretch();
        root->addLayout(contentCol);
        root->addWidget(avatarLb, 0, Qt::AlignTop);
    } else {
        root->addWidget(avatarLb, 0, Qt::AlignTop);
        root->addLayout(contentCol);
        root->addStretch();
    }
}

/* ---------- ChatBubbleItem (图片) ---------- */
ChatBubbleItem::ChatBubbleItem(const QPixmap &image,
                               const QPixmap &avatar,
                               bool isSelf,
                               const QString &timeStr,
                               const QString &imagePath,
                               QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 6, 12, 6);
    root->setSpacing(8);

    QLabel *avatarLb = new QLabel(this);
    avatarLb->setFixedSize(40, 40);
    avatarLb->setPixmap(makeRoundAvatar(avatar, 40));
    avatarLb->setAlignment(Qt::AlignTop);

    QVBoxLayout *contentCol = new QVBoxLayout;
    contentCol->setContentsMargins(0, 0, 0, 0);
    contentCol->setSpacing(3);

    if (!timeStr.isEmpty()) {
        QLabel *timeLb = new QLabel(timeStr, this);
        timeLb->setObjectName("bubble_time_lb");
        timeLb->setAlignment(isSelf ? Qt::AlignRight : Qt::AlignLeft);
        contentCol->addWidget(timeLb);
    }

    auto *imgLb = new ImageBubbleLabel(image, imagePath, this);
    contentCol->addWidget(imgLb);
    contentCol->addStretch();

    if (isSelf) {
        root->addStretch();
        root->addLayout(contentCol);
        root->addWidget(avatarLb, 0, Qt::AlignTop);
    } else {
        root->addWidget(avatarLb, 0, Qt::AlignTop);
        root->addLayout(contentCol);
        root->addStretch();
    }
}

/* ---------- �ļ����� Label���Ҽ��˵����� / ����Ϊ�� ---------- */
class FileBubbleLabel : public QWidget
{
public:
    explicit FileBubbleLabel(const QString &filename, qint64 fileSize,
                             const QString &fileKey, const QString &downloadUrl,
                             bool isSelf, QWidget *parent = nullptr)
        : QWidget(parent), _fileKey(fileKey), _downloadUrl(downloadUrl), _filename(filename)
    {
        // ��ʽ���ļ���С
        QString sizeStr;
        if (fileSize >= 1024LL * 1024 * 1024)
            sizeStr = QString::number(fileSize / 1024 / 1024 / 1024) + " GB";
        else if (fileSize >= 1024 * 1024)
            sizeStr = QString::number(fileSize / 1024 / 1024) + " MB";
        else if (fileSize >= 1024)
            sizeStr = QString::number(fileSize / 1024) + " KB";
        else
            sizeStr = QString::number(fileSize) + " B";

        QHBoxLayout *hbox = new QHBoxLayout(this);
        hbox->setContentsMargins(10, 8, 10, 8);
        hbox->setSpacing(10);

        // �ļ�ͼ�꣨��ɫ���飩
        QLabel *iconLb = new QLabel(this);
        iconLb->setFixedSize(36, 36);
        iconLb->setAlignment(Qt::AlignCenter);
        iconLb->setStyleSheet(
            "background:#4A90D9; color:white; font-size:11px; font-weight:bold;"
            " border-radius:4px;");
        iconLb->setText("FILE");

        // �ļ��� + ��С�����ţ�
        QVBoxLayout *info = new QVBoxLayout;
        info->setSpacing(2);
        QLabel *nameLb = new QLabel(this);
        nameLb->setTextInteractionFlags(Qt::NoTextInteraction);
        QString displayName = filename.length() > 28
            ? filename.left(25) + "..."
            : filename;
        nameLb->setText(displayName);
        nameLb->setToolTip(filename);
        QLabel *sizeLb = new QLabel(sizeStr, this);
        sizeLb->setStyleSheet("color:#888; font-size:11px;");

        info->addWidget(nameLb);
        info->addWidget(sizeLb);

        hbox->addWidget(iconLb);
        hbox->addLayout(info);
        hbox->addStretch();

        // ���ݱ���ɫ
        _bgColor = isSelf ? QColor(0x95, 0xEC, 0x69) : QColor(0xFF, 0xFF, 0xFF);
        setMinimumWidth(220);
        setMaximumWidth(360);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(_bgColor);
        p.setPen(QPen(QColor(0xE0, 0xE0, 0xE0), 1));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        // ���ͷ��ࣺfileKey Ϊ�գ���δ�н��ղ�Ự���������˵�
        if (_fileKey.isEmpty()) return;

        QMenu menu(this);
        QAction *actSave   = menu.addAction(QString::fromUtf8("���浽����Ŀ¼"));
        QAction *actSaveAs = menu.addAction(QString::fromUtf8("����Ϊ��"));

        QAction *chosen = menu.exec(event->globalPos());
        if (!chosen) return;

        QString savePath;
        if (chosen == actSave) {
            savePath = QStandardPaths::writableLocation(
                           QStandardPaths::DownloadLocation) + "/" + _filename;
        } else if (chosen == actSaveAs) {
            savePath = QFileDialog::getSaveFileName(
                window(),
                QString::fromUtf8("����Ϊ"),
                QStandardPaths::writableLocation(
                    QStandardPaths::DownloadLocation) + "/" + _filename);
        }
        if (savePath.isEmpty()) return;

        FileTransferMgr::GetInstance()->AcceptMinioDownload(_fileKey, savePath);
    }

private:
    QString _fileKey;
    QString _downloadUrl;
    QString _filename;
    QColor  _bgColor;
};

/* ---------- ChatBubbleItem (�ļ�����) ---------- */
ChatBubbleItem::ChatBubbleItem(const QString &filename,
                               qint64 fileSize,
                               const QString &fileKey,
                               const QString &downloadUrl,
                               const QPixmap &avatar,
                               bool isSelf,
                               const QString &timeStr,
                               QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(12, 6, 12, 6);
    root->setSpacing(8);

    QLabel *avatarLb = new QLabel(this);
    avatarLb->setFixedSize(40, 40);
    avatarLb->setPixmap(makeRoundAvatar(avatar, 40));
    avatarLb->setAlignment(Qt::AlignTop);

    QVBoxLayout *contentCol = new QVBoxLayout;
    contentCol->setContentsMargins(0, 0, 0, 0);
    contentCol->setSpacing(3);

    if (!timeStr.isEmpty()) {
        QLabel *timeLb = new QLabel(timeStr, this);
        timeLb->setObjectName("bubble_time_lb");
        timeLb->setAlignment(isSelf ? Qt::AlignRight : Qt::AlignLeft);
        contentCol->addWidget(timeLb);
    }

    auto *fileLb = new FileBubbleLabel(filename, fileSize, fileKey, downloadUrl,
                                       isSelf, this);
    contentCol->addWidget(fileLb);
    contentCol->addStretch();

    if (isSelf) {
        root->addStretch();
        root->addLayout(contentCol);
        root->addWidget(avatarLb, 0, Qt::AlignTop);
    } else {
        root->addWidget(avatarLb, 0, Qt::AlignTop);
        root->addLayout(contentCol);
        root->addStretch();
    }
}
