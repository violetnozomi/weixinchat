#ifndef CLICKEDBTN_H
#define CLICKEDBTN_H
#include<QPushButton>
#include"global.h"
#include <QObject>
class ClickedBtn:public QPushButton
{
    Q_OBJECT
public:
    ClickedBtn(QWidget*);
    ~ClickedBtn();
    void SetState(QString normal,QString hover, QString press);
protected:
    virtual void enterEvent(QEnterEvent *event) override;
    virtual void leaveEvent(QEvent *event) override;
    virtual void mousePressEvent(QMouseEvent *e) override;
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
private:
    QString _normal;
    QString _hover;
    QString _press;
};

#endif // CLICKEDBTN_H
