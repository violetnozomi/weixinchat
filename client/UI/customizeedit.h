#ifndef CUSTOMIZEEDIT_H
#define CUSTOMIZEEDIT_H
#include<QLineEdit>
#include<QString>
class CustomizeEdit:public QLineEdit
{   Q_OBJECT
public:
    void SetMaxLength(int max_len);
    CustomizeEdit(QWidget* parent = nullptr);
    ~CustomizeEdit();
protected:
    void focusOutEvent(QFocusEvent *) override;
private:
    void limitTextLength(const QString &text);

    int _max_len;
signals:
    void sig_focus_out();
};

#endif // CUSTOMIZEEDIT_H
