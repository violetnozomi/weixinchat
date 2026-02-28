#ifndef USERMGR_H
#define USERMGR_H

#include "Singleton.h"
#include "UserInfo.h"
#include <QString>

// ── 当前登录用户的会话管理（单例）──────────────────────────────────────────
// 集中存储登录成功后的 uid / name / token / 服务器地址，
// 供 TcpMgr、HttpMgr、各 UI 组件统一读取，避免数据散落各处。
class UserMgr : public QObject, public Singleton<UserMgr>,
                public std::enable_shared_from_this<UserMgr>
{
    Q_OBJECT
    friend class Singleton<UserMgr>;

public:
    ~UserMgr() = default;

    // ── 登录成功后调用一次 ──────────────────────────────────────────────
    void SetSelfInfo(int uid, const QString &name,
                     const QString &token,
                     const QString &host, const QString &port,
                     const QString &avatarPath = ":/res/head_1.png",
                     const QString &signature = "");

    // ── 资料修改（局部更新，无需重登）──────────────────────────────────
    void UpdateProfile(const QString &name,
                       const QString &signature,
                       const QString &avatarPath);

    // ── 读取接口 ────────────────────────────────────────────────────────
    int     GetUid()        const { return _self.uid;        }
    QString GetName()       const { return _self.name;       }
    QString GetToken()      const { return _self.token;      }
    QString GetHost()       const { return _self.host;       }
    QString GetPort()       const { return _self.port;       }
    QString GetAvatarPath() const { return _self.avatarPath; }
    QString GetSignature()  const { return _self.signature;  }
    const SelfInfo &GetSelfInfo() const { return _self; }

    bool IsLoggedIn() const { return _self.uid > 0; }

    // ── 登出时清空 ──────────────────────────────────────────────────────
    void Clear();

signals:
    void sig_login_success();
    void sig_logout();
    void sig_profile_updated();  // 资料修改完成

private:
    UserMgr() = default;

    SelfInfo _self;
};

#endif // USERMGR_H
