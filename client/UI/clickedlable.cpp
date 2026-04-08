#include "clickedlable.h"

ClickedLable::ClickedLable(QWidget *parent):QLabel(parent),_curstate(ClickLbState::Normal) {
    this->setCursor(Qt::PointingHandCursor);
}
void ClickedLable::mousePressEvent(QMouseEvent* event){
    if (event->button() == Qt::LeftButton) {
        if(_curstate == ClickLbState::Normal){
            setProperty("state", _normal_press);
        }else{
            setProperty("state", _selected_press);
        }
        repolish(this);
        update();
    }

    QLabel::mousePressEvent(event);
}
void ClickedLable::enterEvent(QEnterEvent* event) {
    // 在这里处理鼠标悬停进入的逻辑
    if(_curstate == ClickLbState::Normal){
        qDebug()<<"enter , change to normal hover: "<< _normal_hover;
        setProperty("state",_normal_hover);
        repolish(this);
        update();

    }else{
        qDebug()<<"enter , change to selected hover: "<< _selected_hover;
        setProperty("state",_selected_hover);
        repolish(this);
        update();
    }

    QLabel::enterEvent(event);
}
void ClickedLable::leaveEvent(QEvent* event)
{
    if(_curstate == ClickLbState::Normal){
        qDebug()<<"leave , change to normal : "<< _normal;
        setProperty("state",_normal);
        repolish(this);
        update();

    }else{
        qDebug()<<"leave , change to normal hover: "<< _selected;
        setProperty("state",_selected);
        repolish(this);
        update();
    }
    QLabel::leaveEvent(event);
}
void ClickedLable::mouseReleaseEvent(QMouseEvent* event) {

    if (event->button() == Qt::LeftButton) {

        // 2. 【关键】判断鼠标松开时，光标是否还在标签范围内
        // 只有在范围内松开，才算一次有效的“点击”
        if (rect().contains(event->pos())) {

            // 3. 切换状态逻辑
            if (_curstate == ClickLbState::Normal) {
                _curstate = ClickLbState::Selected;
                // 因为鼠标还在上面，所以切换到 Selected 的悬停样式
                setProperty("state", _selected_hover);
            }
            else {
                _curstate = ClickLbState::Normal;
                // 同样，切换到 Normal 的悬停样式
                setProperty("state", _normal_hover);
            }

            // 4. 刷新样式
            repolish(this);
            update();

            // 5. 发射信号
            emit clicked();
        }
    }

    // 6. 调用父类处理（保持兼容性）
    QLabel::mouseReleaseEvent(event);
}
void ClickedLable::SetState(QString normal, QString hover, QString press,QString select, QString select_hover, QString select_press){
    _normal = normal;
    _normal_hover = hover;
    _normal_press = press;
    _selected = select;
    _selected_hover = select_hover;
    _selected_press = select_press;
    setProperty("state",normal);
    repolish(this);

}
ClickLbState ClickedLable::GetCurState(){
    return _curstate;
}

void ClickedLable::ResetToNormal()
{
    _curstate = ClickLbState::Normal;
    setProperty("state", _normal);
    repolish(this);
    update();
}
