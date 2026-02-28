//
// MinIO 预签名 URL 管理器
// 使用 AWS Signature Version 4（MinIO 完全兼容 S3 协议）
//
#pragma once

#include "Singleton.h"
#include <string>

// ────────────────────────────────────────────────────────────────────────────
// MinioMgr — 负责生成预签名 PUT / GET URL
//   - PUT URL：供发送方把文件直接上传到 MinIO（服务端不走流量）
//   - GET URL：供接收方直接从 MinIO 下载文件
// ────────────────────────────────────────────────────────────────────────────
class MinioMgr : public Singleton<MinioMgr> {
    friend class Singleton<MinioMgr>;

public:
    // 生成预签名 PUT URL（客户端直传）
    // file_key : 对象路径，如 "user_42/1700000000_a1b2c3d4_photo.jpg"
    // expires  : URL 有效期（秒），上传场景默认 1 小时
    std::string GeneratePutUrl(const std::string& file_key, int expires = 3600);

    // 生成预签名 GET URL（下载链接）
    // expires  : 默认 7 天，足够离线用户延迟下载
    std::string GenerateGetUrl(const std::string& file_key, int expires = 604800);

    // 无认证直连 URL（适用于 bucket 设置为 public-read 策略时）
    std::string GetPublicUrl(const std::string& file_key);

    const std::string& GetBucket() const { return _bucket; }

    // 生成唯一对象 key：user_<uid>/<timestamp_us>_<random>_<filename>
    static std::string MakeFileKey(int sender_uid, const std::string& filename);

private:
    MinioMgr();

    // AWS Sig V4 presigned URL 核心
    std::string BuildPresignedUrl(const std::string& method,
                                  const std::string& file_key,
                                  int expires);

    // Crypto helpers
    static std::string Sha256Hex(const std::string& data);
    static std::string HmacSha256(const std::string& key, const std::string& msg);
    static std::string ToHex(const std::string& binary);
    static std::string UrlEncode(const std::string& str, bool encode_slash = false);

    std::string _host;
    int         _port       = 9000;
    std::string _access_key;
    std::string _secret_key;
    std::string _bucket;
    std::string _region     = "us-east-1";  // MinIO 默认 region
};
