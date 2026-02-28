#ifndef CHATBUBBLEITEM_H
#define CHATBUBBLEITEM_H

#include <QWidget>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QMovie>
// 单条消息气泡 Widget
// isSelf=true  → 右侧（自己发送，绿色气泡）
// isSelf=false → 左侧（对方发送，白色气泡）
class ChatBubbleItem : public QWidget
{
    Q_OBJECT
public:
    // 文本气泡
    explicit ChatBubbleItem(const QString &text,
                            const QPixmap &avatar,
                            bool isSelf,
                            const QString &timeStr = "",
                            QWidget *parent = nullptr);

    // 图片气泡（clickable）
    explicit ChatBubbleItem(const QPixmap &image,
                            const QPixmap &avatar,
                            bool isSelf,
                            const QString &timeStr = "",
                            const QString &imagePath = "",
                            QWidget *parent = nullptr);

    // 文件气泡（右键菜单可保存 / 另存为）
    explicit ChatBubbleItem(const QString &filename,
                            qint64 fileSize,
                            const QString &fileKey,
                            const QString &downloadUrl,
                            const QPixmap &avatar,
                            bool isSelf,
                            const QString &timeStr = "",
                            QWidget *parent = nullptr);

    static QPixmap makeRoundAvatar(const QPixmap &src, int size);

private:
};

#endif // CHATBUBBLEITEM_H
