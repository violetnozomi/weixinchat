/********************************************************************************
** Form generated from reading UI file 'chatdialog.ui'
**
** Created by: Qt User Interface Compiler version 6.10.2
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CHATDIALOG_H
#define UI_CHATDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <clickedlable.h>
#include "customizeedit.h"

QT_BEGIN_NAMESPACE

class Ui_chatDialog
{
public:
    QHBoxLayout *horizontalLayout;
    QWidget *side_bar;
    QVBoxLayout *verticalLayout;
    QWidget *side_bar_in;
    QVBoxLayout *verticalLayout_2;
    QLabel *side_head_lb;
    QSpacerItem *verticalSpacer_2;
    ClickedLable *side_chat_lb;
    QSpacerItem *verticalSpacer_3;
    ClickedLable *side_contact_lb;
    QSpacerItem *verticalSpacer_group;
    ClickedLable *side_group_lb;
    QSpacerItem *verticalSpacer;
    QWidget *chat_user_widget;
    QVBoxLayout *verticalLayout_3;
    QWidget *search_widget;
    QHBoxLayout *horizontalLayout_2;
    QWidget *search_widget_in;
    QHBoxLayout *horizontalLayout_3;
    CustomizeEdit *search_edit;
    QPushButton *search_btn;
    QWidget *chat_data_widget;
    QVBoxLayout *verticalLayout_4;
    QWidget *widget_13;
    QHBoxLayout *horizontalLayout_4;
    QLabel *titel_lb;
    QSpacerItem *horizontalSpacer_2;
    QScrollArea *msg_scroll_area;
    QWidget *msg_scroll_content;
    QVBoxLayout *msg_content_layout;
    QSpacerItem *msg_top_spacer;
    QWidget *widget_9;
    QHBoxLayout *horizontalLayout_5;
    QSpacerItem *horizontalSpacer_5;
    ClickedLable *emo_lb;
    QSpacerItem *horizontalSpacer_3;
    ClickedLable *file_lb;
    QSpacerItem *horizontalSpacer;
    ClickedLable *video_label;
    QSpacerItem *horizontalSpacer_9;
    ClickedLable *audio_label;
    QSpacerItem *horizontalSpacer_4;
    QWidget *widget_11;
    QVBoxLayout *input_area_layout;
    QTextEdit *input_edit;
    QWidget *widget_10;
    QHBoxLayout *horizontalLayout_6;
    QSpacerItem *horizontalSpacer_6;
    QPushButton *pushButton_2;
    QSpacerItem *horizontalSpacer_8;
    QPushButton *pushButton_3;
    QSpacerItem *horizontalSpacer_7;

    void setupUi(QDialog *chatDialog)
    {
        if (chatDialog->objectName().isEmpty())
            chatDialog->setObjectName("chatDialog");
        chatDialog->resize(900, 650);
        chatDialog->setMinimumSize(QSize(800, 600));
        horizontalLayout = new QHBoxLayout(chatDialog);
        horizontalLayout->setSpacing(0);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        side_bar = new QWidget(chatDialog);
        side_bar->setObjectName("side_bar");
        side_bar->setMinimumSize(QSize(56, 0));
        side_bar->setMaximumSize(QSize(56, 16777215));
        verticalLayout = new QVBoxLayout(side_bar);
        verticalLayout->setObjectName("verticalLayout");
        side_bar_in = new QWidget(side_bar);
        side_bar_in->setObjectName("side_bar_in");
        verticalLayout_2 = new QVBoxLayout(side_bar_in);
        verticalLayout_2->setSpacing(0);
        verticalLayout_2->setObjectName("verticalLayout_2");
        verticalLayout_2->setContentsMargins(0, 8, 0, 8);
        side_head_lb = new QLabel(side_bar_in);
        side_head_lb->setObjectName("side_head_lb");
        side_head_lb->setMinimumSize(QSize(40, 40));
        side_head_lb->setMaximumSize(QSize(40, 40));
        side_head_lb->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayout_2->addWidget(side_head_lb);

        verticalSpacer_2 = new QSpacerItem(20, 12, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Fixed);

        verticalLayout_2->addItem(verticalSpacer_2);

        side_chat_lb = new ClickedLable(side_bar_in);
        side_chat_lb->setObjectName("side_chat_lb");
        side_chat_lb->setMinimumSize(QSize(28, 28));
        side_chat_lb->setMaximumSize(QSize(28, 28));

        verticalLayout_2->addWidget(side_chat_lb);

        verticalSpacer_3 = new QSpacerItem(20, 8, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Fixed);

        verticalLayout_2->addItem(verticalSpacer_3);

        side_contact_lb = new ClickedLable(side_bar_in);
        side_contact_lb->setObjectName("side_contact_lb");
        side_contact_lb->setMinimumSize(QSize(28, 28));
        side_contact_lb->setMaximumSize(QSize(28, 28));

        verticalLayout_2->addWidget(side_contact_lb);

        verticalSpacer_group = new QSpacerItem(20, 8, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Fixed);

        verticalLayout_2->addItem(verticalSpacer_group);

        side_group_lb = new ClickedLable(side_bar_in);
        side_group_lb->setObjectName("side_group_lb");
        side_group_lb->setMinimumSize(QSize(28, 28));
        side_group_lb->setMaximumSize(QSize(28, 28));
        side_group_lb->setAlignment(Qt::AlignmentFlag::AlignCenter);

        verticalLayout_2->addWidget(side_group_lb);


        verticalLayout->addWidget(side_bar_in);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        verticalLayout->addItem(verticalSpacer);


        horizontalLayout->addWidget(side_bar);

        chat_user_widget = new QWidget(chatDialog);
        chat_user_widget->setObjectName("chat_user_widget");
        chat_user_widget->setMinimumSize(QSize(300, 0));
        chat_user_widget->setMaximumSize(QSize(300, 16777215));
        verticalLayout_3 = new QVBoxLayout(chat_user_widget);
        verticalLayout_3->setSpacing(0);
        verticalLayout_3->setObjectName("verticalLayout_3");
        verticalLayout_3->setContentsMargins(0, 0, 0, 0);
        search_widget = new QWidget(chat_user_widget);
        search_widget->setObjectName("search_widget");
        search_widget->setMinimumSize(QSize(0, 60));
        search_widget->setMaximumSize(QSize(16777215, 60));
        horizontalLayout_2 = new QHBoxLayout(search_widget);
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(8, -1, 8, -1);
        search_widget_in = new QWidget(search_widget);
        search_widget_in->setObjectName("search_widget_in");
        horizontalLayout_3 = new QHBoxLayout(search_widget_in);
        horizontalLayout_3->setSpacing(6);
        horizontalLayout_3->setObjectName("horizontalLayout_3");
        horizontalLayout_3->setContentsMargins(0, -1, 0, -1);
        search_edit = new CustomizeEdit(search_widget_in);
        search_edit->setObjectName("search_edit");
        search_edit->setMinimumSize(QSize(0, 36));
        search_edit->setMaximumSize(QSize(16777215, 36));

        horizontalLayout_3->addWidget(search_edit);

        search_btn = new QPushButton(search_widget_in);
        search_btn->setObjectName("search_btn");
        search_btn->setMinimumSize(QSize(52, 32));
        search_btn->setMaximumSize(QSize(52, 32));

        horizontalLayout_3->addWidget(search_btn);


        horizontalLayout_2->addWidget(search_widget_in);


        verticalLayout_3->addWidget(search_widget);


        horizontalLayout->addWidget(chat_user_widget);

        chat_data_widget = new QWidget(chatDialog);
        chat_data_widget->setObjectName("chat_data_widget");
        chat_data_widget->setMinimumSize(QSize(300, 0));
        verticalLayout_4 = new QVBoxLayout(chat_data_widget);
        verticalLayout_4->setSpacing(0);
        verticalLayout_4->setObjectName("verticalLayout_4");
        verticalLayout_4->setContentsMargins(0, 0, 0, 0);
        widget_13 = new QWidget(chat_data_widget);
        widget_13->setObjectName("widget_13");
        widget_13->setMinimumSize(QSize(0, 48));
        widget_13->setMaximumSize(QSize(16777215, 48));
        horizontalLayout_4 = new QHBoxLayout(widget_13);
        horizontalLayout_4->setObjectName("horizontalLayout_4");
        titel_lb = new QLabel(widget_13);
        titel_lb->setObjectName("titel_lb");
        titel_lb->setAlignment(Qt::AlignmentFlag::AlignLeading|Qt::AlignmentFlag::AlignLeft|Qt::AlignmentFlag::AlignVCenter);

        horizontalLayout_4->addWidget(titel_lb);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_2);


        verticalLayout_4->addWidget(widget_13);

        msg_scroll_area = new QScrollArea(chat_data_widget);
        msg_scroll_area->setObjectName("msg_scroll_area");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(1);
        sizePolicy.setHeightForWidth(msg_scroll_area->sizePolicy().hasHeightForWidth());
        msg_scroll_area->setSizePolicy(sizePolicy);
        msg_scroll_area->setFrameShape(QFrame::Shape::NoFrame);
        msg_scroll_area->setWidgetResizable(true);
        msg_scroll_content = new QWidget();
        msg_scroll_content->setObjectName("msg_scroll_content");
        msg_scroll_content->setGeometry(QRect(0, 0, 544, 378));
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(msg_scroll_content->sizePolicy().hasHeightForWidth());
        msg_scroll_content->setSizePolicy(sizePolicy1);
        msg_content_layout = new QVBoxLayout(msg_scroll_content);
        msg_content_layout->setSpacing(4);
        msg_content_layout->setObjectName("msg_content_layout");
        msg_content_layout->setContentsMargins(0, 8, 0, 8);
        msg_top_spacer = new QSpacerItem(20, 0, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        msg_content_layout->addItem(msg_top_spacer);

        msg_scroll_area->setWidget(msg_scroll_content);

        verticalLayout_4->addWidget(msg_scroll_area);

        widget_9 = new QWidget(chat_data_widget);
        widget_9->setObjectName("widget_9");
        widget_9->setMinimumSize(QSize(0, 40));
        widget_9->setMaximumSize(QSize(16777215, 40));
        horizontalLayout_5 = new QHBoxLayout(widget_9);
        horizontalLayout_5->setObjectName("horizontalLayout_5");
        horizontalSpacer_5 = new QSpacerItem(5, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_5);

        emo_lb = new ClickedLable(widget_9);
        emo_lb->setObjectName("emo_lb");
        emo_lb->setMinimumSize(QSize(30, 30));
        emo_lb->setMaximumSize(QSize(30, 30));

        horizontalLayout_5->addWidget(emo_lb);

        horizontalSpacer_3 = new QSpacerItem(8, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_3);

        file_lb = new ClickedLable(widget_9);
        file_lb->setObjectName("file_lb");
        file_lb->setMinimumSize(QSize(30, 30));
        file_lb->setMaximumSize(QSize(30, 30));

        horizontalLayout_5->addWidget(file_lb);

        horizontalSpacer = new QSpacerItem(5, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer);

        video_label = new ClickedLable(widget_9);
        video_label->setObjectName("video_label");
        video_label->setMinimumSize(QSize(30, 30));
        video_label->setMaximumSize(QSize(30, 30));

        horizontalLayout_5->addWidget(video_label);

        horizontalSpacer_9 = new QSpacerItem(5, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_9);

        audio_label = new ClickedLable(widget_9);
        audio_label->setObjectName("audio_label");
        audio_label->setMinimumSize(QSize(30, 30));
        audio_label->setMaximumSize(QSize(30, 30));

        horizontalLayout_5->addWidget(audio_label);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_5->addItem(horizontalSpacer_4);


        verticalLayout_4->addWidget(widget_9);

        widget_11 = new QWidget(chat_data_widget);
        widget_11->setObjectName("widget_11");
        widget_11->setMinimumSize(QSize(0, 100));
        widget_11->setMaximumSize(QSize(16777215, 140));
        input_area_layout = new QVBoxLayout(widget_11);
        input_area_layout->setSpacing(0);
        input_area_layout->setObjectName("input_area_layout");
        input_area_layout->setContentsMargins(0, 0, 0, 0);
        input_edit = new QTextEdit(widget_11);
        input_edit->setObjectName("input_edit");
        input_edit->setFrameShape(QFrame::Shape::NoFrame);

        input_area_layout->addWidget(input_edit);


        verticalLayout_4->addWidget(widget_11);

        widget_10 = new QWidget(chat_data_widget);
        widget_10->setObjectName("widget_10");
        widget_10->setMinimumSize(QSize(0, 44));
        widget_10->setMaximumSize(QSize(16777215, 44));
        horizontalLayout_6 = new QHBoxLayout(widget_10);
        horizontalLayout_6->setObjectName("horizontalLayout_6");
        horizontalSpacer_6 = new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_6);

        pushButton_2 = new QPushButton(widget_10);
        pushButton_2->setObjectName("pushButton_2");
        pushButton_2->setMinimumSize(QSize(92, 34));
        pushButton_2->setMaximumSize(QSize(92, 34));

        horizontalLayout_6->addWidget(pushButton_2);

        horizontalSpacer_8 = new QSpacerItem(8, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_8);

        pushButton_3 = new QPushButton(widget_10);
        pushButton_3->setObjectName("pushButton_3");
        pushButton_3->setMinimumSize(QSize(72, 34));
        pushButton_3->setMaximumSize(QSize(72, 34));

        horizontalLayout_6->addWidget(pushButton_3);

        horizontalSpacer_7 = new QSpacerItem(5, 20, QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Minimum);

        horizontalLayout_6->addItem(horizontalSpacer_7);


        verticalLayout_4->addWidget(widget_10);


        horizontalLayout->addWidget(chat_data_widget);


        retranslateUi(chatDialog);

        QMetaObject::connectSlotsByName(chatDialog);
    } // setupUi

    void retranslateUi(QDialog *chatDialog)
    {
        chatDialog->setWindowTitle(QCoreApplication::translate("chatDialog", "Dialog", nullptr));
        side_head_lb->setText(QString());
        side_chat_lb->setText(QString());
        side_contact_lb->setText(QString());
        side_group_lb->setText(QCoreApplication::translate("chatDialog", "\347\276\244", nullptr));
        search_btn->setText(QCoreApplication::translate("chatDialog", "\346\220\234\347\264\242", nullptr));
        titel_lb->setText(QCoreApplication::translate("chatDialog", "zhanba!", nullptr));
        emo_lb->setText(QString());
        file_lb->setText(QString());
        video_label->setText(QString());
        audio_label->setText(QString());
        input_edit->setPlaceholderText(QCoreApplication::translate("chatDialog", "\345\234\250\346\255\244\350\276\223\345\205\245\346\266\210\346\201\257\342\200\246", nullptr));
        pushButton_2->setText(QCoreApplication::translate("chatDialog", "\346\216\245\346\224\266", nullptr));
        pushButton_3->setText(QCoreApplication::translate("chatDialog", "\345\217\221\351\200\201", nullptr));
    } // retranslateUi

};

namespace Ui {
    class chatDialog: public Ui_chatDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CHATDIALOG_H
