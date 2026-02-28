#include "HashUtils.h"

// Windows BCrypt API（无需额外依赖，链接 Bcrypt.lib 系统库即可）
#include <windows.h>
#include <bcrypt.h>

#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>

// ----------------------------------------------------------------
// GenerateSalt
// ----------------------------------------------------------------
std::string GenerateSalt(size_t bytes) {
    std::vector<BYTE> buf(bytes);

    NTSTATUS status = BCryptGenRandom(
        nullptr,
        buf.data(),
        static_cast<ULONG>(bytes),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("BCryptGenRandom failed with status: " +
                                 std::to_string(status));
    }

    // 转换为小写十六进制字符串
    std::ostringstream oss;
    for (auto b : buf) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

// ----------------------------------------------------------------
// Sha256Hex
// ----------------------------------------------------------------
std::string Sha256Hex(const std::string& data) {
    BCRYPT_ALG_HANDLE  hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    // 1. 打开 SHA-256 算法提供者
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }

    // 2. 查询哈希输出长度（SHA-256 固定为 32 字节）
    DWORD hashLen = 0, cbResult = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                      reinterpret_cast<PBYTE>(&hashLen), sizeof(DWORD),
                      &cbResult, 0);

    // 3. 创建哈希对象
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }

    // 4. 输入数据
    BCryptHashData(hHash,
                   reinterpret_cast<PBYTE>(const_cast<char*>(data.data())),
                   static_cast<ULONG>(data.size()), 0);

    // 5. 取出哈希值
    std::vector<BYTE> digest(hashLen);
    BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);

    // 6. 清理资源
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    // 7. 转十六进制字符串
    std::ostringstream oss;
    for (auto b : digest) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

// ----------------------------------------------------------------
// HashPassword
// ----------------------------------------------------------------
std::string HashPassword(const std::string& pwd, const std::string& salt) {
    // 盐值拼接在前，防止长度扩展攻击
    return Sha256Hex(salt + pwd);
}

// ----------------------------------------------------------------
// VerifyPassword
// ----------------------------------------------------------------
bool VerifyPassword(const std::string& pwd, const std::string& stored) {
    const auto sep = stored.find('$');

    if (sep == std::string::npos) {
        // ---- 兼容旧版：数据库中是明文密码（迁移过渡期）----
        // 匹配成功后建议调用方立即将密码迁移为哈希格式
        return stored == pwd;
    }

    // ---- 新格式：salt$sha256hash ----
    const std::string salt         = stored.substr(0, sep);
    const std::string expectedHash = stored.substr(sep + 1);

    return HashPassword(pwd, salt) == expectedHash;
}
