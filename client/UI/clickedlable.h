#ifndef CLICKEDLABLE_H
#define CLICKEDLABLE_H
#include<QLabel>
#include<QMouseEvent>
#include"global.h"
#include<qstring.h>
class ClickedLable:public QLabel
{
    Q_OBJECT
public:
    ClickedLable(QWidget *parent);
    virtual void mousePressEvent(QMouseEvent *ev) override;
    virtual void mouseReleaseEvent(QMouseEvent *ev) override;
    virtual void enterEvent(QEnterEvent *ev) override;
    virtual void leaveEvent(QEvent *event) override;
    ClickLbState GetCurState();
    void SetState(QString normal="", QString hover="", QString press="",
                  QString select="", QString select_hover="", QString select_press="");
    void ResetToNormal();  // 强制重置回 Normal 状态（用于互斥组）

private:

    QString _normal;
    QString _normal_hover;
    QString _normal_press;

    QString _selected;
    QString _selected_hover;
    QString _selected_press;

    ClickLbState _curstate;
signals:
    void clicked();
};

#endif // CLICKEDLABLE_H
