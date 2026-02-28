#pragma once
#include <string>

/**
 * @brief 生成随机盐值（十六进制字符串，默认16字节/32字符）
 *        使用 Windows BCrypt API (BCryptGenRandom)
 */
std::string GenerateSalt(size_t bytes = 16);

/**
 * @brief 计算字符串的 SHA-256 哈希，返回十六进制字符串（64字符）
 *        使用 Windows BCrypt API
 */
std::string Sha256Hex(const std::string& data);

/**
 * @brief 对密码 + 盐值进行哈希处理
 *        计算 SHA-256(salt + pwd)，返回十六进制哈希字符串
 */
std::string HashPassword(const std::string& pwd, const std::string& salt);

/**
 * @brief 验证明文密码与数据库中的存储值是否匹配
 *        存储格式为 "salt$sha256hash"（例如: "a3f2b1c0...$d4e5f6a7..."）
 *
 *        兼容旧版明文密码：若 stored 中不含 '$'，则直接对比明文（用于数据库迁移过渡期）
 *
 * @param pwd     用户输入的明文密码
 * @param stored  数据库中存储的密码字段值
 * @return true   密码匹配
 * @return false  密码不匹配
 */
bool VerifyPassword(const std::string& pwd, const std::string& stored);
