//
// MinioMgr.cpp  —  AWS Signature Version 4 预签名 URL 生成
//
#include "MinioMgr.h"
#include "ConfigMgr.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <random>
#include <iostream>

// ────────────────────────────────────────────────────────────────────────────
// 构造：从 ConfigMgr 读取 [MinIo] 节配置
// ────────────────────────────────────────────────────────────────────────────
MinioMgr::MinioMgr() {
    auto& cfg   = ConfigMgr::Inst();
    _host       = cfg["MinIo"]["Host"];
    _port       = std::stoi(cfg["MinIo"]["Port"]);
    _access_key = cfg["MinIo"]["AccessKey"];
    _secret_key = cfg["MinIo"]["SecretKey"];
    _bucket     = cfg["MinIo"]["Bucket"];

    std::cout << "[MinIO] Initialized: "
              << _host << ":" << _port
              << " bucket=" << _bucket << std::endl;
}

// ────────────────────────────────────────────────────────────────────────────
// Crypto helpers
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::ToHex(const std::string& binary) {
    std::ostringstream ss;
    for (unsigned char c : binary)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return ss.str();
}

std::string MinioMgr::Sha256Hex(const std::string& data) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  len  = 0;
    EVP_MD_CTX*   ctx  = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
    return ToHex(std::string(reinterpret_cast<char*>(hash), len));
}

// 返回原始 bytes（非 hex），用于派生签名密钥链
std::string MinioMgr::HmacSha256(const std::string& key, const std::string& msg) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int  len  = 0;
    HMAC(EVP_sha256(),
         reinterpret_cast<const unsigned char*>(key.data()), (int)key.size(),
         reinterpret_cast<const unsigned char*>(msg.data()), (int)msg.size(),
         hash, &len);
    return std::string(reinterpret_cast<char*>(hash), len);
}

// RFC 3986 URL 编码（encode_slash=true 时也编码"/"）
std::string MinioMgr::UrlEncode(const std::string& str, bool encode_slash) {
    std::ostringstream ss;
    for (unsigned char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            ss << c;
        } else if (c == '/' && !encode_slash) {
            ss << c;
        } else {
            ss << '%' << std::uppercase
               << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return ss.str();
}

// ────────────────────────────────────────────────────────────────────────────
// AWS Signature Version 4  —  Presigned URL 核心实现
// 参考: https://docs.aws.amazon.com/AmazonS3/latest/API/sigv4-query-string-auth.html
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::BuildPresignedUrl(const std::string& method,
                                         const std::string& file_key,
                                         int expires)
{
    // ── 1. 获取当前 UTC 时间 ──────────────────────────────────────────────
    auto now  = std::chrono::system_clock::now();
    auto tt   = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char datebuf[16], datetimebuf[20];
    strftime(datebuf,     sizeof(datebuf),     "%Y%m%d",         &utc);
    strftime(datetimebuf, sizeof(datetimebuf), "%Y%m%dT%H%M%SZ", &utc);
    std::string date     = datebuf;
    std::string datetime = datetimebuf;

    // ── 2. 参数准备 ─────────────────────────────────────────────────────
    const std::string& region  = _region;
    const std::string  service = "s3";
    std::string scope           = date + "/" + region + "/" + service + "/aws4_request";
    std::string hostval         = _host + ":" + std::to_string(_port);

    // ── 3. 规范查询字符串（按字典序排列）────────────────────────────────
    // 凭证中的 "/" 需要编码
    std::string credential_enc = UrlEncode(_access_key + "/" + scope, /*encode_slash=*/true);
    std::string canonical_qs =
        "X-Amz-Algorithm=AWS4-HMAC-SHA256"
        "&X-Amz-Credential=" + credential_enc +
        "&X-Amz-Date="       + datetime +
        "&X-Amz-Expires="    + std::to_string(expires) +
        "&X-Amz-SignedHeaders=host";

    // ── 4. 规范请求（Canonical Request）────────────────────────────────
    // URI = /<bucket>/<file_key>，file_key 中的 "/" 不编码（路径分隔）
    std::string canonical_uri     = "/" + _bucket + "/" + UrlEncode(file_key);
    std::string canonical_headers = "host:" + hostval + "\n";
    std::string signed_headers    = "host";
    std::string payload_hash      = "UNSIGNED-PAYLOAD";  // 预签名模式固定值

    std::string canonical_request =
        method           + "\n" +
        canonical_uri    + "\n" +
        canonical_qs     + "\n" +
        canonical_headers + "\n" +
        signed_headers   + "\n" +
        payload_hash;

    // ── 5. 待签名字符串（String to Sign）────────────────────────────────
    std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" +
        datetime            + "\n" +
        scope               + "\n" +
        Sha256Hex(canonical_request);

    // ── 6. 派生签名密钥（Signing Key 链式 HMAC）────────────────────────
    std::string k_date    = HmacSha256("AWS4" + _secret_key, date);
    std::string k_region  = HmacSha256(k_date,    region);
    std::string k_service = HmacSha256(k_region,  service);
    std::string k_signing = HmacSha256(k_service, "aws4_request");

    std::string signature = ToHex(HmacSha256(k_signing, string_to_sign));

    // ── 7. 最终 URL ──────────────────────────────────────────────────────
    return "http://" + hostval + canonical_uri + "?" +
           canonical_qs + "&X-Amz-Signature=" + signature;
}

// ────────────────────────────────────────────────────────────────────────────
// 公开接口
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::GeneratePutUrl(const std::string& file_key, int expires) {
    return BuildPresignedUrl("PUT", file_key, expires);
}

std::string MinioMgr::GenerateGetUrl(const std::string& file_key, int expires) {
    return BuildPresignedUrl("GET", file_key, expires);
}

std::string MinioMgr::GetPublicUrl(const std::string& file_key) {
    return "http://" + _host + ":" + std::to_string(_port) +
           "/" + _bucket + "/" + UrlEncode(file_key);
}

// ────────────────────────────────────────────────────────────────────────────
// 生成唯一对象 key
// 格式: user_<uid>/<timestamp_us>_<random8>_<filename>
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::MakeFileKey(int sender_uid, const std::string& filename) {
    // 时间戳（微秒）
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // 8 位随机十六进制
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    std::ostringstream hex;
    hex << std::hex << std::setw(8) << std::setfill('0') << dist(rng);

    return "user_" + std::to_string(sender_uid) + "/" +
           std::to_string(us) + "_" + hex.str() + "_" + filename;
}
