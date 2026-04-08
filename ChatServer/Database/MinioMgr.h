//
// MinIO 预签名 URL 管理器
// 使用 AWS Signature Version 4（MinIO 完全兼容 S3 协议）
//
#pragma once

#include "Singleton.h"
#include <string>
#include <vector>
#include <utility>  // std::pair

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

    // 生成唯一对象 key
    static std::string MakeFileKey(int sender_uid, const std::string& filename);

    // ── 分段上传 API ──────────────────────────────────────────────────
    // 分段上传的分片信息
    struct PartInfo {
        int         part_number = 0;
        std::string etag;           // 上传响应中 MinIO 返回的 ETag头（含引号）
    };

    // 生成单个分片的预签名 PUT URL（包含 partNumber + uploadId 查询参数）
    std::string GeneratePartPutUrl(const std::string& file_key,
                                   const std::string& upload_id,
                                   int part_number,
                                   int expires = 3600);

    // 分段生命周期（同步 HTTP 调用，应在 DbReadQueue/DbWriteQueue 线程中执行）
    std::string           CreateMultipartUpload(const std::string& file_key);
    bool                  CompleteMultipartUpload(const std::string& file_key,
                                                  const std::string& upload_id,
                                                  const std::vector<PartInfo>& parts);
    std::vector<PartInfo> ListParts(const std::string& file_key,
                                    const std::string& upload_id);
    void                  AbortMultipartUpload(const std::string& file_key,
                                               const std::string& upload_id);

    const std::string& GetBucket() const { return _bucket; }

private:
    MinioMgr();

    // AWS Sig V4 presigned URL 核心（extra_sorted_qs: 已按字母序排列的额外查询参数，无需小写开头的 &）
    std::string BuildPresignedUrl(const std::string& method,
                                  const std::string& file_key,
                                  int expires,
                                  const std::string& extra_sorted_qs = "");

    // 执行带 Authorization 头的同步 HTTP 请求（返回 {http_status, body}）
    std::pair<int, std::string> ExecuteSignedRequest(
        const std::string& method,
        const std::string& file_key,
        const std::string& canonical_qs,
        const std::string& body,
        const std::string& content_type = "");

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
