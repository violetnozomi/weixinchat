//
// MinioMgr.cpp  —  AWS Signature Version 4 预签名 URL 生成
//
#include "MinioMgr.h"
#include "ConfigMgr.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

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

    // 打印配置信息（敏感字段已脱敏）\n    std::cout << \"[MinIO] ===== MinIO Config ====\" << std::endl;
    std::cout << "[MinIO]   Host       = " << _host << std::endl;
    std::cout << "[MinIO]   Port       = " << _port << std::endl;
    std::cout << "[MinIO]   Bucket     = " << _bucket << std::endl;
    std::cout << "[MinIO]   AccessKey  = " << _access_key << std::endl;
    std::cout << "[MinIO]   SecretKey  = ****" << std::endl;
    std::cout << "[MinIO]   Region     = " << _region << std::endl;
    std::cout << "[MinIO] ==============================" << std::endl;
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
                                         int expires,
                                         const std::string& extra_sorted_qs)
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

    // 额外参数（如 partNumber=N&uploadId=xxx）均以小写字母开头，字典序小于 'X'(大写)，位于所有 X-Amz-* 之后 ✔
    if (!extra_sorted_qs.empty())
        canonical_qs += "&" + extra_sorted_qs;

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
    std::string final_url = "http://" + hostval + canonical_uri + "?" +
                            canonical_qs + "&X-Amz-Signature=" + signature;
    return final_url;
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

// ────────────────────────────────────────────────────────────────────────────
// GeneratePartPutUrl  —  分段上传分片的预签名 PUT URL
// extra_sorted_qs: "partNumber=N&uploadId=xxx"  ('p' < 'u', 均 < 'X', 已排序)
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::GeneratePartPutUrl(const std::string& file_key,
                                          const std::string& upload_id,
                                          int part_number,
                                          int expires) {
    // partNumber='p'(0x70) < uploadId='u'(0x75) ✓, 均在 X-Amz-* 之后 ✓
    std::string extra = "partNumber=" + std::to_string(part_number) +
                        "&uploadId="  + UrlEncode(upload_id, true);
    return BuildPresignedUrl("PUT", file_key, expires, extra);
}

// ────────────────────────────────────────────────────────────────────────────
// XML 解析辅助（仅供此文件内部使用）
// ────────────────────────────────────────────────────────────────────────────
static std::string minio_xml_tag(const std::string& xml, const std::string& tag) {
    auto open  = "<"  + tag + ">";
    auto close = "</" + tag + ">";
    auto a = xml.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    auto b = xml.find(close, a);
    return b == std::string::npos ? "" : xml.substr(a, b - a);
}

static std::string minio_xml_unescape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] == '&') {
            if   (s.substr(i,6)=="&quot;") { r+='"';  i+=6; }
            else if (s.substr(i,5)=="&amp;")  { r+='&';  i+=5; }
            else if (s.substr(i,4)=="&lt;")   { r+='<';  i+=4; }
            else if (s.substr(i,4)=="&gt;")   { r+='>';  i+=4; }
            else { r+=s[i++]; }
        } else { r+=s[i++]; }
    }
    return r;
}

// ────────────────────────────────────────────────────────────────────────────
// ExecuteSignedRequest  —  带 Authorization 头的同步 HTTP 请求
// 在 DbReadQueue/DbWriteQueue 工作线程中调用，不阻塞逻辑线程。
// ────────────────────────────────────────────────────────────────────────────
std::pair<int, std::string> MinioMgr::ExecuteSignedRequest(
        const std::string& method,
        const std::string& file_key,
        const std::string& canonical_qs,
        const std::string& body,
        const std::string& content_type)
{
    namespace beast = boost::beast;
    namespace http  = beast::http;
    namespace net   = boost::asio;
    using tcp       = net::ip::tcp;

    // ── 1. 当前 UTC 时间 ─────────────────────────────────────────────────
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    char datebuf[16], datetimebuf[20];
    strftime(datebuf,     sizeof(datebuf),     "%Y%m%d",         &utc);
    strftime(datetimebuf, sizeof(datetimebuf), "%Y%m%dT%H%M%SZ", &utc);
    std::string date     = datebuf;
    std::string datetime = datetimebuf;

    // ── 2. 路径 & 请求目标 ───────────────────────────────────────────────
    std::string canonical_uri = "/" + _bucket + "/" + UrlEncode(file_key);
    std::string hostval       = _host + ":" + std::to_string(_port);
    std::string target        = canonical_uri;
    if (!canonical_qs.empty()) target += "?" + canonical_qs;

    // ── 3. 载荷哈希 ─────────────────────────────────────────────────────
    std::string payload_hash = Sha256Hex(body);

    // ── 4. 规范头 (host < x-amz-content-sha256 < x-amz-date, 已排序) ──
    std::string canonical_headers =
        "host:" + hostval + "\n" +
        "x-amz-content-sha256:" + payload_hash + "\n" +
        "x-amz-date:" + datetime + "\n";
    std::string signed_headers = "host;x-amz-content-sha256;x-amz-date";

    // ── 5. 规范请求 ──────────────────────────────────────────────────────
    std::string canonical_request =
        method         + "\n" +
        canonical_uri  + "\n" +
        canonical_qs   + "\n" +
        canonical_headers + "\n" +
        signed_headers + "\n" +
        payload_hash;

    // ── 6. 待签名字符串 ──────────────────────────────────────────────────
    std::string scope = date + "/" + _region + "/s3/aws4_request";
    std::string string_to_sign =
        "AWS4-HMAC-SHA256\n" +
        datetime + "\n" +
        scope    + "\n" +
        Sha256Hex(canonical_request);

    // ── 7. 签名密钥链 ────────────────────────────────────────────────────
    std::string k_date    = HmacSha256("AWS4" + _secret_key, date);
    std::string k_region  = HmacSha256(k_date,    _region);
    std::string k_service = HmacSha256(k_region,  "s3");
    std::string k_signing = HmacSha256(k_service, "aws4_request");
    std::string signature = ToHex(HmacSha256(k_signing, string_to_sign));

    // ── 8. Authorization 头 ──────────────────────────────────────────────
    std::string auth_val =
        "AWS4-HMAC-SHA256 Credential=" + _access_key + "/" + scope +
        ", SignedHeaders=" + signed_headers +
        ", Signature=" + signature;

    // ── 9. Boost.Beast 同步 HTTP 调用 ───────────────────────────────────
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(30));

        auto eps = resolver.resolve(_host, std::to_string(_port));
        stream.connect(eps);

        http::verb verb = http::verb::get;
        if      (method == "POST")   verb = http::verb::post;
        else if (method == "PUT")    verb = http::verb::put;
        else if (method == "DELETE") verb = http::verb::delete_;

        http::request<http::string_body> req{verb, target, 11};
        req.set(http::field::host,           hostval);
        req.set("x-amz-date",                datetime);
        req.set("x-amz-content-sha256",      payload_hash);
        req.set(http::field::authorization,  auth_val);
        if (!content_type.empty())
            req.set(http::field::content_type, content_type);
        req.body() = body;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buf;
        http::response<http::string_body> res;
        http::read(stream, buf, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return {static_cast<int>(res.result_int()), std::string(res.body())};

    } catch (const std::exception& e) {
        std::cerr << "[MinIO] ExecuteSignedRequest(" << method
                  << " " << file_key << ") error: " << e.what() << std::endl;
        return {0, ""};
    }
}

// ────────────────────────────────────────────────────────────────────────────
// CreateMultipartUpload  —  POST /?uploads  返回 upload_id
// ────────────────────────────────────────────────────────────────────────────
std::string MinioMgr::CreateMultipartUpload(const std::string& file_key) {
    // canonical_qs: "uploads=" (key 无值时规范形式仍含 '=')
    auto [status, body] = ExecuteSignedRequest("POST", file_key, "uploads=", "");
    if (status != 200) {
        std::cerr << "[MinIO] CreateMultipartUpload failed status=" << status
                  << " body=" << body.substr(0, 256) << std::endl;
        return "";
    }
    return minio_xml_tag(body, "UploadId");
}

// ────────────────────────────────────────────────────────────────────────────
// CompleteMultipartUpload  —  POST /?uploadId=xxx with XML parts list
// ────────────────────────────────────────────────────────────────────────────
bool MinioMgr::CompleteMultipartUpload(const std::string& file_key,
                                        const std::string& upload_id,
                                        const std::vector<PartInfo>& parts) {
    std::string xml = "<CompleteMultipartUpload>";
    for (const auto& p : parts) {
        xml += "<Part>"
               "<PartNumber>" + std::to_string(p.part_number) + "</PartNumber>"
               "<ETag>" + p.etag + "</ETag>"
               "</Part>";
    }
    xml += "</CompleteMultipartUpload>";

    // canonical_qs: uploadId (sorted: single param)
    std::string qs = "uploadId=" + UrlEncode(upload_id, true);
    auto [status, body] = ExecuteSignedRequest("POST", file_key, qs, xml,
                                               "application/xml");
    if (status != 200) {
        std::cerr << "[MinIO] CompleteMultipartUpload failed status=" << status
                  << " body=" << body.substr(0, 256) << std::endl;
        return false;
    }
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// ListParts  —  GET /?max-parts=1000&uploadId=xxx  解析已上传分片
// ────────────────────────────────────────────────────────────────────────────
std::vector<MinioMgr::PartInfo> MinioMgr::ListParts(const std::string& file_key,
                                                      const std::string& upload_id) {
    // max-parts ('m') < uploadId ('u'), 已排序 ✓
    std::string qs = "max-parts=1000&uploadId=" + UrlEncode(upload_id, true);
    auto [status, body] = ExecuteSignedRequest("GET", file_key, qs, "");

    std::vector<PartInfo> parts;
    if (status != 200) {
        std::cerr << "[MinIO] ListParts failed status=" << status << std::endl;
        return parts;
    }

    const std::string open_tag  = "<Part>";
    const std::string close_tag = "</Part>";
    size_t pos = 0;
    while ((pos = body.find(open_tag, pos)) != std::string::npos) {
        pos += open_tag.size();
        auto end = body.find(close_tag, pos);
        if (end == std::string::npos) break;
        std::string block = body.substr(pos, end - pos);
        PartInfo pi;
        pi.part_number = std::stoi(minio_xml_tag(block, "PartNumber"));
        pi.etag        = minio_xml_unescape(minio_xml_tag(block, "ETag"));
        parts.push_back(pi);
        pos = end + close_tag.size();
    }
    return parts;
}

// ────────────────────────────────────────────────────────────────────────────
// AbortMultipartUpload  —  DELETE /?uploadId=xxx  fire-and-forget
// ────────────────────────────────────────────────────────────────────────────
void MinioMgr::AbortMultipartUpload(const std::string& file_key,
                                     const std::string& upload_id) {
    std::string qs = "uploadId=" + UrlEncode(upload_id, true);
    auto [status, body] = ExecuteSignedRequest("DELETE", file_key, qs, "");
    if (status != 204 && status != 200) {
        std::cerr << "[MinIO] AbortMultipartUpload status=" << status
                  << " key=" << file_key << std::endl;
    }
}
