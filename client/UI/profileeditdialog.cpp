#include "profileeditdialog.h"
#include "chatbubbleitem.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QEvent>
#include <QMouseEvent>

ProfileEditDialog::ProfileEditDialog(const QString &name,
                                     const QString &signature,
                                     const QPixmap  &avatar,
                                     QWidget *parent)
    : QDialog(parent)
    , _newAvatar(avatar)
    , _avatarChanged(false)
{
    setWindowTitle(QString::fromUtf8("编辑个人资料"));
    setFixedSize(340, 400);
    setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);

    // ── 根布局 ───────────────────────────────────────────────────────────
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(30, 20, 30, 20);
    root->setSpacing(10);

    // ── 头像区 ───────────────────────────────────────────────────────────
    auto *avatarRow = new QHBoxLayout();
    avatarRow->setAlignment(Qt::AlignCenter);

    _avatarLb = new QLabel(this);
    _avatarLb->setFixedSize(80, 80);
    _avatarLb->setAlignment(Qt::AlignCenter);
    _avatarLb->setCursor(Qt::PointingHandCursor);
    _avatarLb->setToolTip(QString::fromUtf8("点击更换头像"));
    _avatarLb->installEventFilter(this);
    updateAvatarLabel();
    avatarRow->addWidget(_avatarLb);

    QLabel *changeLb = new QLabel(QString::fromUtf8("点击头像更换"), this);
    changeLb->setAlignment(Qt::AlignCenter);
    changeLb->setStyleSheet("color:#888; font-size:11px;");

    root->addLayout(avatarRow);
    root->addWidget(changeLb);
    root->addSpacing(6);

    // ── 字段：昵称 ───────────────────────────────────────────────────────
    auto addField = [&](const QString &label, QLineEdit *&edit,
                        const QString &value, const QString &placeholder) {
        QLabel *lb = new QLabel(label, this);
        lb->setStyleSheet("font-size:13px; color:#333; font-family:'Microsoft YaHei';");
        edit = new QLineEdit(value, this);
        edit->setPlaceholderText(placeholder);
        edit->setFixedHeight(36);
        edit->setStyleSheet(
            "QLineEdit { border:1px solid #d9d9d9; border-radius:4px;"
            " padding:0 8px; font-size:13px; font-family:'Microsoft YaHei'; }"
            "QLineEdit:focus { border-color:#07c160; }");
        root->addWidget(lb);
        root->addWidget(edit);
        root->addSpacing(4);
    };

    addField(QString::fromUtf8("昵称"),     _nameEdit, name,
             QString::fromUtf8("请输入昵称"));
    addField(QString::fromUtf8("个性签名"), _sigEdit,  signature,
             QString::fromUtf8("介绍一下自己吧"));

    // ── 按钮行 ───────────────────────────────────────────────────────────
    root->addStretch();

    auto *btnRow    = new QHBoxLayout();
    auto *cancelBtn = new QPushButton(QString::fromUtf8("取消"), this);
    auto *saveBtn   = new QPushButton(QString::fromUtf8("保存"), this);

    cancelBtn->setFixedSize(100, 36);
    saveBtn->setFixedSize(100, 36);
    cancelBtn->setStyleSheet(
        "QPushButton { border:1px solid #d9d9d9; border-radius:4px;"
        " background:#fff; color:#333; font-size:13px;"
        " font-family:'Microsoft YaHei'; }"
        "QPushButton:hover { background:#f5f5f5; }");
    saveBtn->setStyleSheet(
        "QPushButton { border:none; border-radius:4px;"
        " background:#07c160; color:#fff; font-size:13px;"
        " font-family:'Microsoft YaHei'; }"
        "QPushButton:hover { background:#06ad56; }");

    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addSpacing(12);
    btnRow->addWidget(saveBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn,   &QPushButton::clicked, this, [this]() {
        if (_nameEdit->text().trimmed().isEmpty()) {
            _nameEdit->setFocus();
            return;
        }
        accept();
    });
}

bool ProfileEditDialog::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == _avatarLb && ev->type() == QEvent::MouseButtonRelease) {
        static const QString kFilter =
            QString::fromUtf8("图片文件 (*.png *.jpg *.jpeg *.bmp);;所有文件 (*)");
        QString path = QFileDialog::getOpenFileName(
            this,
            QString::fromUtf8("选择头像图片"),
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            kFilter);
        if (!path.isEmpty()) {
            QPixmap px(path);
            if (!px.isNull()) {
                _newAvatar     = px;
                _avatarChanged = true;
                updateAvatarLabel();
            }
        }
        return true;
    }
    return QDialog::eventFilter(obj, ev);
}

void ProfileEditDialog::updateAvatarLabel()
{
    QPixmap rounded = _newAvatar.isNull()
        ? QPixmap(":/res/head_1.jpg")
        : _newAvatar;
    _avatarLb->setPixmap(ChatBubbleItem::makeRoundAvatar(rounded, 80));
}

QString ProfileEditDialog::GetName()      const { return _nameEdit->text().trimmed();  }
QString ProfileEditDialog::GetSignature() const { return _sigEdit->text().trimmed();   }
