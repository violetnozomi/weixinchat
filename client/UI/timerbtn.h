#ifndef TIMERBTN_H
#define TIMERBTN_H
#include<QPushButton>
#include<qtimer.h>
class TimerBtn:public QPushButton
{
    Q_OBJECT
public:
    TimerBtn(QWidget* parent);
    ~TimerBtn();
    virtual void mouseReleaseEvent(QMouseEvent *e) override;
private:
    QTimer* _timer;
    int _count;
};

#endif // TIMERBTN_H
