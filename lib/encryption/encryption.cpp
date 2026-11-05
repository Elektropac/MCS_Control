#include "encryption.h"

#include <Arduino.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <esp_random.h>
#include <vector>

String Encrypt(String data_in, String key)
{
    uint8_t aesKey[32], iv[12], tag[16];

    mbedtls_md(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        (const uint8_t *)key.c_str(), key.length(), aesKey);

    esp_fill_random(iv, sizeof(iv));

    std::vector<uint8_t> encrypted(data_in.length() + 1);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int rc = mbedtls_gcm_setkey(
        &ctx, MBEDTLS_CIPHER_ID_AES, aesKey, 256);

    if (rc == 0)
        rc = mbedtls_gcm_crypt_and_tag(
            &ctx, MBEDTLS_GCM_ENCRYPT, data_in.length(),
            iv, 12, nullptr, 0,
            (const uint8_t *)data_in.c_str(),
            encrypted.data(), 16, tag);

    mbedtls_gcm_free(&ctx);
    if (rc != 0)
        return "";

    const char *hex = "0123456789ABCDEF";
    String result;
    result.reserve((data_in.length() + 28) * 2);

    auto appendHex = [&](const uint8_t *p, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            result += hex[p[i] >> 4];
            result += hex[p[i] & 15];
        }
    };

    appendHex(iv, 12);
    appendHex(tag, 16);
    appendHex(encrypted.data(), data_in.length());

    return result;
}

String Decrypt(String data_in, String key)
{
    if (data_in.length() < 56 || data_in.length() % 2)
        return "";

    auto nibble = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };

    std::vector<uint8_t> raw(data_in.length() / 2);

    for (size_t i = 0; i < raw.size(); i++)
    {
        int a = nibble(data_in[i * 2]);
        int b = nibble(data_in[i * 2 + 1]);
        if (a < 0 || b < 0)
            return "";
        raw[i] = (a << 4) | b;
    }

    uint8_t aesKey[32];

    mbedtls_md(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
        (const uint8_t *)key.c_str(), key.length(), aesKey);

    size_t len = raw.size() - 28;
    std::vector<uint8_t> decrypted(len + 1, 0);

    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);

    int rc = mbedtls_gcm_setkey(
        &ctx, MBEDTLS_CIPHER_ID_AES, aesKey, 256);

    if (rc == 0)
        rc = mbedtls_gcm_auth_decrypt(
            &ctx, len,
            raw.data(), 12,
            nullptr, 0,
            raw.data() + 12, 16,
            raw.data() + 28,
            decrypted.data());

    mbedtls_gcm_free(&ctx);
    if (rc != 0)
        return "";

    return String((char *)decrypted.data());
}

String Encrypt_Json(JsonDocument &doc, String key)
{
    String json;
    serializeJson(doc, json);
    return Encrypt(json, key);
}

JsonDocument Decrypt_Json(String data_in, String key)
{
    String decrypted = Decrypt(data_in, key);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, decrypted);
    if (error)
    {
        return JsonDocument();
    }
    return doc;
}
