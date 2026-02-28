#include "customizeedit.h"

CustomizeEdit::CustomizeEdit(QWidget* parent):QLineEdit(parent) {
    connect(this, &QLineEdit::textChanged, this, &CustomizeEdit::limitTextLength);

}

CustomizeEdit::~CustomizeEdit() {
    // 析构函数实现
}

void CustomizeEdit::focusOutEvent(QFocusEvent* event){
    QLineEdit::focusOutEvent(event);
     emit sig_focus_out();
}
void CustomizeEdit::SetMaxLength(int max_len){
    _max_len= max_len;
}
#include <QSignalBlocker>

void CustomizeEdit::limitTextLength(const QString &text) {
    if (_max_len <= 0) {
        return;
    }
    QByteArray byteArray = text.toUtf8();
    if (byteArray.size() <= _max_len) {
        return;
    }
    byteArray = byteArray.left(_max_len);
    int len = byteArray.length();
    for (int i = 0; i < qMin(3, len); ++i) {
        unsigned char byte = static_cast<unsigned char>(byteArray.at(len - 1 - i));

        if (byte < 0x80) {
            break;
        }

        if ((byte & 0xC0) == 0xC0) {
            byteArray.truncate(len - 1 - i);
            break;
        }

    }
    QString validText = QString::fromUtf8(byteArray);
    if (this->text() != validText) {
        int cursor = this->cursorPosition();
        {
            const QSignalBlocker blocker(this);
            this->setText(validText);
        }
        this->setCursorPosition(qMin(cursor, validText.length()));
    }
}
