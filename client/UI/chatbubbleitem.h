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

// 前向声明，供 appendText() 使用
class BubbleLabel;

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

    // 流式文字追加（仅文本气泡支持）
    void appendText(const QString &piece);

    // 图片气泡（clickable）
    explicit ChatBubbleItem(const QPixmap &image,
                            const QPixmap &avatar,
                            bool isSelf,
                            const QString &timeStr = "",
                            const QString &imagePath = "",
                            QWidget *parent = nullptr);

    // 文件气泡（右键菜单可保存 / 另存为；发送方传 localPath 可打开/在文件夹中显示）
    explicit ChatBubbleItem(const QString &filename,
                            qint64 fileSize,
                            const QString &fileKey,
                            const QString &downloadUrl,
                            const QPixmap &avatar,
                            bool isSelf,
                            const QString &timeStr = "",
                            const QString &localPath = "",
                            QWidget *parent = nullptr);

    static QPixmap makeRoundAvatar(const QPixmap &src, int size);

private:
    BubbleLabel *_bubble_label = nullptr;  ///< 仅文本气泡时有效，以支持 appendText()
};

#endif // CHATBUBBLEITEM_H
