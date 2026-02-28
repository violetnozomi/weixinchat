#include "timerbtn.h"
#include<QMouseEvent>
TimerBtn::TimerBtn(QWidget* parent):QPushButton(parent),_count(10) {
    _timer = new QTimer(this);
    connect(_timer,&QTimer::timeout,this,[this](){
        _count--;
        if(_count <= 0){
            _timer->stop();          // 停止定时器，别让它继续走了
            _count = 10;           // 重置计数器为 10，为下次点击做准备
            this->setText("获取");    // 把按钮文字改回 "获取"
            this->setEnabled(true);  // 【关键】重新启用按钮，让用户可以再次点击
            return;
        }
        this->setText(QString::number(_count));

    });
}
TimerBtn::~TimerBtn()
{
    _timer->stop();
}
void TimerBtn::mouseReleaseEvent(QMouseEvent* event){
    if (rect().contains(event->pos())){
        if(event->button() == Qt::LeftButton)
        {
            this->setEnabled(false);
            this->setText(QString::number(_count));
            _timer->start(1000);
            emit clicked();
        }
        QPushButton::mouseReleaseEvent(event);
    }
}
