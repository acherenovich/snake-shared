#pragma once

#include "coroutine.hpp"

#include <boost/json.hpp>

namespace Utils::Crypt {
    struct DecryptAsyncResult
    {
        std::string decrypted;
        bool success = false;
        std::string error;
    };

    struct EncryptAsyncResult
    {
        std::string encrypted;
        bool success = false;
        std::string error;
    };

    struct DecryptResult
    {
        std::string decrypted;
        bool success = false;
        std::string error;
    };

    struct EncryptResult
    {
        std::string encrypted;
        bool success = false;
        std::string error;
    };

    struct ReadKeyResult {
        std::string key;
        bool success = false;
        std::string error;
    };

    ReadKeyResult ReadKey(const std::string& path);

    DecryptAsyncResult Decrypt(const std::string& privateKey, const std::string& encrypted);

    EncryptAsyncResult Encrypt(const std::string& publicKey, const std::string& data);

    DecryptResult DecryptWithKey(const std::string& secreteKey, const std::string& encrypted);

    EncryptResult EncryptWithKey(const std::string& secreteKey, const std::string& data);

    int GenerateTOTP(const std::string& base32_secret, uint64_t timestep = 30, uint64_t offset = 0);

    std::vector<uint8_t> DecodeBase32(const std::string& base32);

    std::pair<std::string, std::string> GenerateKeyPairPem(int keyBits = 2048);

    std::string Base64UrlEncode(std::string_view data);

    std::string Base64UrlDecode(std::string_view data);

    std::string GenerateJwtHs256(
        const boost::json::object&  customClaims,
        std::string_view            secret,
        std::string_view            issuer = "default_issuer",
        std::string_view            subject = "default_subject",
        std::chrono::seconds        lifetime = std::chrono::minutes{30});

    bool ValidateJwtHs256(
        const std::string&   token,
        std::string_view     secret,
        boost::json::object& outPayload,
        std::string&         error,
        bool                 checkExpiry = true);

    bool VerifyTOTP(const std::string& base32_secret, int code, uint64_t timestep = 30, int tolerance = 1);

    std::string GetSHA256HMAC(const std::string& value, const std::string& key);

    bool VerifySHA256HMAC(const std::string& value, const std::string& key, const std::string& expectedSignature);

    std::string GetSHA256(const std::string& value);
}