# ==============================================================================
# Qt 模块配置
# ==============================================================================
QT       += core gui network sql multimedia multimediawidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++20
RC_ICONS = icon.ico

# ==============================================================================
# 【核心修复】：解决 LNK2038 /MT 与 /MD 的终极冲突
# 使用 replace 强行拦截并替换 Qt 底层的 MD/MDd 为 MT/MTd
# ==============================================================================
QMAKE_CFLAGS_RELEASE = $$replace(QMAKE_CFLAGS_RELEASE, -MD, -MT)
QMAKE_CXXFLAGS_RELEASE = $$replace(QMAKE_CXXFLAGS_RELEASE, -MD, -MT)
QMAKE_CFLAGS_DEBUG = $$replace(QMAKE_CFLAGS_DEBUG, -MDd, -MTd)
QMAKE_CXXFLAGS_DEBUG = $$replace(QMAKE_CXXFLAGS_DEBUG, -MDd, -MTd)

# 屏蔽默认库冲突警告
QMAKE_LFLAGS_RELEASE += /NODEFAULTLIB:msvcrt.lib
QMAKE_LFLAGS_DEBUG += /NODEFAULTLIB:msvcrtd.lib

# ==============================================================================
# WebRTC 核心引擎配置 (通过 vcpkg 引入)
# ==============================================================================
win32 {
    VCPKG_ROOT = C:/vcpkg/installed/x64-windows-static-md

    INCLUDEPATH += $$VCPKG_ROOT/include \
                   $$VCPKG_ROOT/include/third_party/abseil-cpp

    LIBS += -L$$VCPKG_ROOT/lib -lwebrtc

    # 【已去重并整理】Windows 原生系统依赖库 (WebRTC 刚需)
    LIBS += -lsecur32 -lwinmm -ldmoguids -lwmcodecdspuuid -lamstrmid \
            -lmsdmo -lstrmiids -liphlpapi -lcrypt32 -lshlwapi -lole32 \
            -loleaut32 -luser32 -lgdi32 -ladvapi32

    DEFINES += WEBRTC_WIN NOMINMAX
}

# ── 头文件搜索路径（各功能子目录）──────────────────────────────────────────
INCLUDEPATH += \
    $$PWD/UI \
    $$PWD/Network \
    $$PWD/Manager \
    $$PWD/Model \
    $$PWD/Utils

# ── UI 层 ──────────────────────────────────────────────────────────────────
SOURCES += \
    Manager/cameracapturer.cpp \
    Manager/webrtcmgr.cpp \
    UI/VideoRender.cpp \
    UI/profileeditdialog.cpp \
    UI/addfriendwidget.cpp \
    UI/chatbubbleitem.cpp \
    UI/chatdialog.cpp \
    UI/chat_user_list.cpp \
    UI/chat_user_wid.cpp \
    UI/clickedbtn.cpp \
    UI/clickedlable.cpp \
    UI/contactlistpanel.cpp \
    UI/customizeedit.cpp \
    UI/forgetdialog.cpp \
    UI/friendprofilepanel.cpp \
    UI/friendrequestitem.cpp \
    UI/friendverifypanel.cpp \
    UI/login_dialog.cpp \
    UI/mainwindow.cpp \
    UI/register_dialog.cpp \
    UI/searchuseritem.cpp \
    UI/emojipickerwidget.cpp \
    UI/timerbtn.cpp \
    UI/videocalldialog.cpp

HEADERS += \
    Manager/cameracapturer.h \
    Manager/webrtcmgr.h \
    UI/profileeditdialog.h \
    UI/addfriendwidget.h \
    UI/chatbubbleitem.h \
    UI/chatdialog.h \
    UI/chat_user_list.h \
    UI/chat_user_wid.h \
    UI/clickedbtn.h \
    UI/clickedlable.h \
    UI/contactlistpanel.h \
    UI/customizeedit.h \
    UI/forgetdialog.h \
    UI/friendprofilepanel.h \
    UI/friendrequestitem.h \
    UI/friendverifypanel.h \
    UI/login_dialog.h \
    UI/mainwindow.h \
    UI/register_dialog.h \
    UI/searchuseritem.h \
    UI/emojipickerwidget.h \
    UI/timerbtn.h \
    UI/videocalldialog.h \
    UI/videorender.h

# 【已修复】：去掉了多余的 UI 目录，防止 uic.exe 报错
FORMS += \
    UI/chatdialog.ui \
    UI/forgetdialog.ui \
    UI/login_dialog.ui \
    UI/mainwindow.ui \
    UI/register_dialog.ui \
    UI/videocalldialog.ui

# ── Network 层 ────────────────────────────────────────────────────────────
SOURCES += \
    Network/httpmgr.cpp \
    Network/tcpmgr.cpp

HEADERS += \
    Network/httpmgr.h \
    Network/tcpmgr.h

# ── Manager 层 ────────────────────────────────────────────────────────────
SOURCES += \
    Manager/UserMgr.cpp \
    Manager/FriendMgr.cpp \
    Manager/ChatMgr.cpp

HEADERS += \
    Manager/UserMgr.h \
    Manager/FriendMgr.h \
    Manager/ChatMgr.h

# ── Model 层 ──────────────────────────────────────────────────────────────
HEADERS += \
    Model/UserInfo.h \
    Model/FriendInfo.h \
    Model/FriendRequest.h \
    Model/MessageInfo.h

# ── Utils 层 ──────────────────────────────────────────────────────────────
SOURCES += \
    Utils/global.cpp \
    Utils/ChatStorage.cpp \
    Utils/FileTransferMgr.cpp

HEADERS += \
    Utils/Singleton.h \
    Utils/global.h \
    Utils/ChatStorage.h \
    Utils/FileTransferMgr.h

# ── Entry point ───────────────────────────────────────────────────────────
SOURCES += \
    main.cpp

# 配置文件复制
win32 {
    CONFIG(debug, debug|release) {
        config_copy.files = $$PWD/config.ini
        config_copy.path  = $$OUT_PWD/debug
        COPIES += config_copy
    } else {
        config_copy.files = $$PWD/config.ini
        config_copy.path  = $$OUT_PWD/release
        COPIES += config_copy
    }
}

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    RC.qrc

DISTFILES += \
    style/stylesheet.qss
