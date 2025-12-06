#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <boost/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <random>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>

#include "crypt.hpp"
#include "utils.hpp"

namespace Utils::Crypt {
    ReadKeyResult ReadKey(const std::string& path)
    {
        namespace fs = std::filesystem;
        ReadKeyResult result;

        std::error_code ec;
        if (!fs::exists(path, ec)) {
            result.error = "File does not exist: " + path;
            return result;
        }

        std::ifstream in(path);
        if (!in) {
            result.error = "Failed to open file: " + path;
            return result;
        }

        std::ostringstream ss;
        ss << in.rdbuf();
        result.key = ss.str();
        result.success = true;

        return result;
    }

    DecryptAsyncResult Decrypt(const std::string& privateKey, const std::string& encrypted)
    {
        DecryptAsyncResult result;

        BIO* bio = BIO_new_mem_buf(privateKey.data(), static_cast<int>(privateKey.size()));
        if (!bio) {
            result.error = "BIO_new_mem_buf failed";
            return result;
        }

        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        if (!pkey) {
            result.error = "PEM_read_bio_PrivateKey failed";
            return result;
        }

        RSA* rsa = EVP_PKEY_get1_RSA(pkey);
        EVP_PKEY_free(pkey);

        if (!rsa) {
            result.error = "EVP_PKEY_get1_RSA failed";
            return result;
        }

        std::vector<unsigned char> decrypted(RSA_size(rsa));

        const int len = RSA_private_decrypt(
            static_cast<int>(encrypted.size()),
            reinterpret_cast<const unsigned char*>(encrypted.data()),
            decrypted.data(),
            rsa,
            RSA_PKCS1_PADDING
        );

        RSA_free(rsa);

        if (len == -1) {
            result.error = "RSA_private_decrypt failed";
            return result;
        }

        result.success = true;
        result.decrypted.assign(decrypted.begin(), decrypted.begin() + len);
        return result;
    }


    EncryptAsyncResult Encrypt(const std::string& publicKey, const std::string& data)
    {
        EncryptAsyncResult result;

        BIO* bio = BIO_new_mem_buf(publicKey.data(), publicKey.size());
        if (!bio)
        {
            result.error = "BIO_new_mem_buf failed";
            return result;
        }

        RSA* rsa = PEM_read_bio_RSA_PUBKEY(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        if (!rsa)
        {
            result.error = "PEM_read_bio_RSA_PUBKEY failed";
            return result;
        }

        std::vector<unsigned char> output(RSA_size(rsa));

        const int len = RSA_public_encrypt(
            static_cast<int>(data.size()),
            reinterpret_cast<const unsigned char*>(data.data()),
            output.data(),
            rsa,
            RSA_PKCS1_PADDING
        );

        RSA_free(rsa);

        if (len == -1)
        {
            result.error = "RSA_public_encrypt failed";
            return result;
        }

        result.success = true;
        result.encrypted.assign(output.begin(), output.begin() + len);

        return result;
    }

    namespace {
        constexpr size_t AES_KEY_LEN = 32;
        constexpr size_t AES_IV_LEN  = 12;
        constexpr size_t AES_TAG_LEN = 16;
    }

    EncryptResult EncryptWithKey(const std::string& secreteKey, const std::string& data)
    {
        EncryptResult result;
        if (secreteKey.size() < AES_KEY_LEN) {
            result.error = "Key too short (min 32 bytes)";
            return result;
        }

        unsigned char iv[AES_IV_LEN];
        if (RAND_bytes(iv, AES_IV_LEN) != 1) {
            result.error = "Failed to generate IV";
            return result;
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            result.error = "Failed to allocate cipher context";
            return result;
        }

        const auto* key = reinterpret_cast<const unsigned char*>(secreteKey.data());

        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, iv) != 1) {
            result.error = "EncryptInit failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        std::vector<unsigned char> ciphertext(data.size());
        int len = 0;
        if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                              reinterpret_cast<const unsigned char*>(data.data()), data.size()) != 1) {
            result.error = "EncryptUpdate failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }
        int totalLen = len;

        if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
            result.error = "EncryptFinal failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }
        totalLen += len;

        unsigned char tag[AES_TAG_LEN];
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AES_TAG_LEN, tag) != 1) {
            result.error = "GetTag failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        EVP_CIPHER_CTX_free(ctx);

        std::string_view iv_view(reinterpret_cast<const char*>(iv), AES_IV_LEN);
        std::string_view tag_view(reinterpret_cast<const char*>(tag), AES_TAG_LEN);
        std::string_view ct_view(reinterpret_cast<const char*>(ciphertext.data()), totalLen);

        result.encrypted = std::string(iv_view) + std::string(tag_view) + std::string(ct_view);
        result.success = true;
        return result;
    }

    DecryptResult DecryptWithKey(const std::string& secreteKey, const std::string& encrypted)
    {
        DecryptResult result;
        if (secreteKey.size() < AES_KEY_LEN) {
            result.error = "Key too short (min 32 bytes)";
            return result;
        }

        if (encrypted.size() < AES_IV_LEN + AES_TAG_LEN) {
            result.error = "Encrypted data too short";
            return result;
        }

        const auto* iv  = reinterpret_cast<const unsigned char*>(encrypted.data());
        const auto* tag = reinterpret_cast<const unsigned char*>(encrypted.data() + AES_IV_LEN);
        const auto* ciphertext = reinterpret_cast<const unsigned char*>(encrypted.data() + AES_IV_LEN + AES_TAG_LEN);
        const int ciphertext_len = static_cast<int>(encrypted.size() - AES_IV_LEN - AES_TAG_LEN);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            result.error = "Failed to allocate cipher context";
            return result;
        }

        const auto* key = reinterpret_cast<const unsigned char*>(secreteKey.data());
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, key, iv) != 1) {
            result.error = "DecryptInit failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        std::vector<unsigned char> plaintext(ciphertext_len);
        int len = 0;
        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len) != 1) {
            result.error = "DecryptUpdate failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AES_TAG_LEN, (void*)tag) != 1) {
            result.error = "SetTag failed";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        int finalLen = 0;
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &finalLen) != 1) {
            result.error = "Auth failed: tag mismatch";
            EVP_CIPHER_CTX_free(ctx);
            return result;
        }

        EVP_CIPHER_CTX_free(ctx);

        result.decrypted.assign(reinterpret_cast<char*>(plaintext.data()), len + finalLen);
        result.success = true;
        return result;
    }


    int GenerateTOTP(const std::string& base32_secret, const uint64_t timestep, const uint64_t offset) {
        const std::vector<uint8_t> key = DecodeBase32(base32_secret);
        if (key.empty()) throw std::runtime_error("Invalid secret");

        uint64_t t = (std::time(nullptr) + offset) / timestep;

        uint8_t msg[8];
        for (int i = 7; i >= 0; --i) {
            msg[i] = t & 0xFF;
            t >>= 8;
        }

        unsigned char hmac[EVP_MAX_MD_SIZE];
        unsigned int hmac_len;

        HMAC(EVP_sha1(), key.data(), key.size(), msg, 8, hmac, &hmac_len);

        const int hmacOffset = hmac[hmac_len - 1] & 0x0F;
        const uint32_t bin_code = (hmac[hmacOffset] & 0x7f) << 24 |
                            (hmac[hmacOffset + 1] & 0xff) << 16 |
                            (hmac[hmacOffset + 2] & 0xff) << 8 |
                            (hmac[hmacOffset + 3] & 0xff);

        return bin_code % 1000000; // 6 цифр
    }

    std::vector<uint8_t> DecodeBase32(const std::string& base32) {
        static const std::unordered_map<char, uint8_t> base32_map = {
            {'A', 0},  {'B', 1},  {'C', 2},  {'D', 3},  {'E', 4},  {'F', 5},  {'G', 6},  {'H', 7},
            {'I', 8},  {'J', 9},  {'K',10},  {'L',11},  {'M',12},  {'N',13},  {'O',14},  {'P',15},
            {'Q',16},  {'R',17},  {'S',18},  {'T',19},  {'U',20},  {'V',21},  {'W',22},  {'X',23},
            {'Y',24},  {'Z',25},  {'2',26},  {'3',27},  {'4',28},  {'5',29},  {'6',30},  {'7',31}
        };

        std::vector<uint8_t> result;
        int buffer = 0;
        int bits_left = 0;

        for (char ch : base32) {
            if (ch == '=' || std::isspace(ch)) continue; // пропускаем padding и пробелы

            ch = std::toupper(ch);
            auto it = base32_map.find(ch);
            if (it == base32_map.end()) {
                throw std::runtime_error("Invalid character in Base32 string");
            }

            buffer <<= 5;
            buffer |= it->second;
            bits_left += 5;

            if (bits_left >= 8) {
                bits_left -= 8;
                result.push_back(static_cast<uint8_t>((buffer >> bits_left) & 0xFF));
            }
        }

        return result;
    }

    std::pair<std::string, std::string> GenerateKeyPairPem(const int keyBits) {
        RSA* rsa = RSA_new();
        BIGNUM* exponent = BN_new();
        BN_set_word(exponent, RSA_F4); // 65537

        RSA_generate_key_ex(rsa, keyBits, exponent, nullptr);

        BIO* privateBio = BIO_new(BIO_s_mem());
        PEM_write_bio_RSAPrivateKey(privateBio, rsa, nullptr, nullptr, 0, nullptr, nullptr);
        char* privateData;
        long privateLen = BIO_get_mem_data(privateBio, &privateData);
        std::string privateKey(privateData, privateLen);

        BIO* publicBio = BIO_new(BIO_s_mem());
        PEM_write_bio_RSA_PUBKEY(publicBio, rsa);  // Для PKCS#1: используй PEM_write_bio_RSAPublicKey
        char* publicData;
        long publicLen = BIO_get_mem_data(publicBio, &publicData);
        std::string publicKey(publicData, publicLen);

        BIO_free(privateBio);
        BIO_free(publicBio);
        RSA_free(rsa);
        BN_free(exponent);

        return {privateKey, publicKey};
    }

    // ─────────────── Base64‑URL (без padding) ────────────────
    std::string Base64UrlEncode(const std::string_view data)
    {
        // Обычное Base64 через OpenSSL
        std::string b64(((data.size() + 2) / 3) * 4, '\0');
        const int len = EVP_EncodeBlock(
            reinterpret_cast<unsigned char*>(b64.data()),
            reinterpret_cast<const unsigned char*>(data.data()),
            static_cast<int>(data.size()));
        b64.resize(len);

        // → URL‑safe variant
        for (auto& ch : b64) {
            if (ch == '+') ch = '-';
            else if (ch == '/') ch = '_';
        }
        while (!b64.empty() && b64.back() == '=') b64.pop_back();
        return b64;
    }

    // ─────────────── Base64‑URL decode ────────────────
    std::string Base64UrlDecode(const std::string_view data)
    {
        std::string b64{data};
        for (auto& ch : b64) {
            if (ch == '-')      ch = '+';
            else if (ch == '_') ch = '/';
        }

        const size_t mod = b64.size() % 4;
        const size_t pad = (mod == 0 ? 0 : 4 - mod);

        b64.append(pad, '=');

        std::string out((b64.size() / 4) * 3, '\0');

        int len = EVP_DecodeBlock(
            reinterpret_cast<unsigned char*>(out.data()),
            reinterpret_cast<const unsigned char*>(b64.data()),
            static_cast<int>(b64.size()));
        if (len < 0) {
            throw std::runtime_error("Base64 decode error");
        }

        out.resize(len - static_cast<int>(pad));

        return out;
    }

    // ─────────────── JWT генератор ────────────────
    std::string GenerateJwtHs256(const boost::json::object& customClaims,
                                        const std::string_view            secret,
                                        const std::string_view            issuer,
                                        const std::string_view            subject,
                                        const std::chrono::seconds        lifetime)
    {
        using Clock = std::chrono::system_clock;

        // 1) header
        const boost::json::object header{
            {"alg", "HS256"},
            {"typ", "JWT"}
        };

        // 2) payload (стандартные + пользовательские claim‑ы)
        boost::json::object payload;
        payload["data"] = customClaims;

        const auto  nowSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                  Clock::now().time_since_epoch())
                                  .count();
        const int64_t expSecs = nowSecs + lifetime.count();

        payload["iss"] = issuer;
        payload["sub"] = subject;
        payload["iat"] = nowSecs;
        payload["exp"] = expSecs;

        // 3) Base64URL(header) + "." + Base64URL(payload)
        const std::string headerB64  = Base64UrlEncode(boost::json::serialize(header));
        const std::string payloadB64 = Base64UrlEncode(boost::json::serialize(payload));
        const std::string signingInput = headerB64 + '.' + payloadB64;

        // 4) HMAC‑SHA256(signingInput, secret)
        unsigned char hmac[EVP_MAX_MD_SIZE];
        unsigned int  hmacLen = 0;
        HMAC(EVP_sha256(),
             secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(signingInput.data()),
             signingInput.size(),
             hmac, &hmacLen);

        const std::string signatureB64 =
            Base64UrlEncode({reinterpret_cast<const char*>(hmac), hmacLen});

        // 5) header.payload.signature
        return signingInput + '.' + signatureB64;
    }

    // ─────────────── Проверка JWT ────────────────
    bool ValidateJwtHs256(const std::string&   token,
                                 const std::string_view     secret,
                                 boost::json::object& outPayload,
                                 std::string&         error,
                                 const bool                 checkExpiry)
    {
        // 1) разобрать три части
        const auto dot1 = token.find('.');
        const auto dot2 = token.find('.', dot1 + 1);
        if (dot1 == std::string::npos || dot2 == std::string::npos) {
            error = "Malformed token";
            return false;
        }
        const auto header64  = std::string(token.substr(0, dot1));
        const auto payload64 = std::string(token.substr(dot1 + 1, dot2 - dot1 - 1));
        const auto signature64 = std::string(token.substr(dot2 + 1));
        // 2) Base64URL → JSON
        boost::json::object header, payload;
        try {
            header  = boost::json::parse(Base64UrlDecode(header64)).as_object();
            payload = boost::json::parse(Base64UrlDecode(payload64)).as_object();
        } catch (const std::exception& e) {
            error = "Invalid JSON in token: " + std::string(e.what());
            return false;
        }

        // 3) проверить алгоритм
        if (auto it = header.if_contains("alg"); !it || it->as_string() != "HS256") {
            error = "Unsupported alg (only HS256)";
            return false;
        }

        // 4) пересчитать подпись
        const std::string signingInput = std::string(header64) + '.' + std::string(payload64);

        unsigned char hmac[EVP_MAX_MD_SIZE];
        unsigned int  hmacLen = 0;
        HMAC(EVP_sha256(),
             secret.data(), static_cast<int>(secret.size()),
             reinterpret_cast<const unsigned char*>(signingInput.data()),
             signingInput.size(),
             hmac, &hmacLen);

        const std::string expectedSig = Base64UrlEncode({reinterpret_cast<const char*>(hmac), hmacLen});
        if (expectedSig != signature64) {
            error = "Signature mismatch";
            return false;
        }

        // 5) (опционально) проверить истечение срока
        if (checkExpiry) {
            if (auto it = payload.if_contains("exp")) {
                const auto nowSecs = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
                if (it->is_int64() && nowSecs >= it->as_int64()) {
                    error = "Token expired";
                    return false;
                }
            }
        }

        outPayload = std::move(payload);
        return true;
    }

    bool VerifyTOTP(const std::string& base32_secret, const int code, const uint64_t timestep, const int tolerance)
    {
        const std::vector<uint8_t> key = Utils::Crypt::DecodeBase32(base32_secret);
        if (key.empty()) return false;

        const int targetCode = code;
        const auto now = std::time(nullptr);
        const int64_t currentStep = now / timestep;

        for (int i = -tolerance; i <= tolerance; ++i) {
            int64_t t = currentStep + i;

            uint8_t msg[8];
            for (int j = 7; j >= 0; --j) {
                msg[j] = t & 0xFF;
                t >>= 8;
            }

            unsigned char hmac[EVP_MAX_MD_SIZE];
            unsigned int hmac_len = 0;

            HMAC(EVP_sha1(), key.data(), static_cast<int>(key.size()), msg, sizeof(msg), hmac, &hmac_len);

            const int offset = hmac[hmac_len - 1] & 0x0F;
            const uint32_t bin_code = (hmac[offset] & 0x7f) << 24 |
                                      (hmac[offset + 1] & 0xff) << 16 |
                                      (hmac[offset + 2] & 0xff) << 8 |
                                      (hmac[offset + 3] & 0xff);

            const int totp = bin_code % 1000000;

            if (totp == targetCode)
                return true;
        }

        return false;
    }

    std::string GetSHA256HMAC(const std::string& value, const std::string& key)
    {
        unsigned int len = EVP_MAX_MD_SIZE;
        unsigned char hash[EVP_MAX_MD_SIZE];

        HMAC(
            EVP_sha256(),
            key.data(), key.size(),
            reinterpret_cast<const unsigned char*>(value.data()), value.size(),
            hash, &len
        );

        static const char* hexDigits = "0123456789abcdef";
        std::string hex;
        hex.reserve(len * 2);
        for (unsigned int i = 0; i < len; ++i) {
            hex += hexDigits[(hash[i] >> 4) & 0x0F];
            hex += hexDigits[hash[i] & 0x0F];
        }

        return hex;
    }

    bool VerifySHA256HMAC(const std::string& value, const std::string& key, const std::string& expectedSignature)
    {
        return GetSHA256HMAC(value, key) == expectedSignature;
    }

    std::string GetSHA256(const std::string& value)
    {
        unsigned char hash[SHA256_DIGEST_LENGTH];

        SHA256(
            reinterpret_cast<const unsigned char*>(value.data()),
            value.size(),
            hash
        );

        static const char* hexDigits = "0123456789abcdef";
        std::string hex;
        hex.reserve(SHA256_DIGEST_LENGTH * 2);

        for (const unsigned char i : hash) {
            hex += hexDigits[(i >> 4) & 0x0F];
            hex += hexDigits[i & 0x0F];
        }

        return hex;
    }
}
