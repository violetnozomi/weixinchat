#pragma once
#include <QFrame>

/**
 * @brief 表情包选择面板（弹出式）
 *
 * 使用方式：
 *   调用 showAbove(globalBottomLeft) 在工具栏按钮上方弹出面板。
 *   当用户点击某个表情时发出 emojiSelected(index) 信号（index 从 1 开始）。
 *   点击面板以外的区域自动关闭（Qt::Popup 窗口特性）。
 */
class EmojiPickerWidget : public QFrame
{
    Q_OBJECT
public:
    explicit EmojiPickerWidget(QWidget *parent = nullptr);

    /// 在指定屏幕位置（左下角）上方显示面板
    void showAbove(const QPoint &globalBottomLeft);

signals:
    /// 用户点击了第 index 个表情（1 .. kEmojiCount）
    void emojiSelected(const QString& imagePath);

public:
    static constexpr int kEmojiCount = 48;  ///< 表情包总数
};
