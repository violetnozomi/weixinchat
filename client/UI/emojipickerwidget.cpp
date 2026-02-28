#include "emojipickerwidget.h"
#include <QPushButton>
#include <QScrollArea>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QPixmap>
#include <QApplication>
#include <QScreen>
#include <QDebug>

static constexpr int kColCount  = 5;    // 每行显示几个
static constexpr int kThumbSize = 56;   // 单个表情显示尺寸（px）
static constexpr int kSpacing   = 4;    // 格子间距

EmojiPickerWidget::EmojiPickerWidget(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_DeleteOnClose, false);   // 隐藏而不销毁，可复用
    setAttribute(Qt::WA_TranslucentBackground);

    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Raised);
    setStyleSheet(
        "EmojiPickerWidget {"
        "  background: white;"
        "  border: 1px solid #d5d5d5;"
        "  border-radius: 8px;"
        "}"
        );

    // 主布局
    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(0);

    // 滚动区（高度显示 3 行）
    const int scrollW = kColCount * (kThumbSize + kSpacing) + kSpacing + 12;
    const int rows    = (kEmojiCount + kColCount - 1) / kColCount;
    const int scrollH = qMin(rows, 3) * (kThumbSize + kSpacing) + kSpacing + 12;

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(
        rows > 3 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setFixedSize(scrollW, scrollH);

    // 内容 widget + 格子布局
    QWidget *content = new QWidget;
    content->setStyleSheet("background: white;");
    QGridLayout *grid = new QGridLayout(content);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(kSpacing);

    // 【核心新增】：独立计数器。专门用来记录“真正加载成功的表情个数”，保证网格绝对紧凑，不留空洞
    int validCount = 0;

    for (int i = 1; i <= kEmojiCount; ++i) {

        QStringList extensions = {".gif", ".png", ".jpg", ".jpeg", ".GIF", ".PNG", ".JPG", ".JPEG"};

        // 【核心大改】：遍历所有后缀
        for (const QString &ext : extensions) {
            QString path = QString(":/res/emo (%1)%2").arg(i).arg(ext);
            QPixmap px;

            // 只要成功 load 到了这张图（不管是 png 还是 jpeg），立刻就地给它建一个专属按钮！
            if (px.load(path)) {

                // 1. 创建按钮
                QPushButton *btn = new QPushButton(content);
                btn->setFixedSize(kThumbSize, kThumbSize);
                btn->setFlat(true);
                btn->setCursor(Qt::PointingHandCursor);
                // 提示可以稍微改下，把后缀也带上，方便你区分
                btn->setToolTip(QString::fromUtf8("表情 %1%2").arg(i).arg(ext));

                // 2. 设置图标
                btn->setIcon(QIcon(px));
                btn->setIconSize(QSize(kThumbSize - 6, kThumbSize - 6));

                // 3. 设置样式
                btn->setStyleSheet(
                    "QPushButton { border: none; border-radius: 6px; background: transparent; }"
                    "QPushButton:hover { background: #f0f0f0; }"
                    "QPushButton:pressed { background: #e0e0e0; }"
                    );

                // 4. 绑定点击事件，发送它自己的专属绝对路径
                connect(btn, &QPushButton::clicked, this, [this, path]() {
                    emit emojiSelected(path);
                    hide();
                });

                // 5. 放入网格并推进格子
                int row = validCount / kColCount;
                int col = validCount % kColCount;
                grid->addWidget(btn, row, col);

                validCount++; // 每建好一个真实存在的图，格子数才加一
            }
        }
    }
    scroll->setWidget(content);
    mainLay->addWidget(scroll);
    adjustSize();
}

void EmojiPickerWidget::showAbove(const QPoint &globalBottomLeft)
{
    adjustSize();
    QSize sz = sizeHint();

    // 默认：紧贴按钮上方，左对齐
    QPoint pos(globalBottomLeft.x(), globalBottomLeft.y() - sz.height() - 4);

    // 防止超出屏幕边界
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    if (pos.x() + sz.width() > screen.right())
        pos.setX(screen.right() - sz.width());
    if (pos.x() < screen.left())
        pos.setX(screen.left());
    if (pos.y() < screen.top())
        pos.setY(globalBottomLeft.y() + 4);  // 向下弹出（空间不足时）

    move(pos);
    show();
    raise();
    activateWindow();
}
