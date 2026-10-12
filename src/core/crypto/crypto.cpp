// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstring>
#include <openssl/aes.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "core/crypto/crypto.h"
#include "core/crypto/keys.h"

namespace {

template <typename Keyset>
RSA* MakePrivateKey() {
    RSA* rsa = RSA_new();
    if (!rsa) {
        return nullptr;
    }
    auto bn = [](const u8* data, size_t size) { return BN_bin2bn(data, static_cast<int>(size), nullptr); };
    if (!RSA_set0_key(rsa, bn(Keyset::Modulus, sizeof(Keyset::Modulus)),
                      bn(Keyset::PublicExponent, sizeof(Keyset::PublicExponent)),
                      bn(Keyset::PrivateExponent, sizeof(Keyset::PrivateExponent))) ||
        !RSA_set0_factors(rsa, bn(Keyset::Prime1, sizeof(Keyset::Prime1)),
                          bn(Keyset::Prime2, sizeof(Keyset::Prime2))) ||
        !RSA_set0_crt_params(rsa, bn(Keyset::Exponent1, sizeof(Keyset::Exponent1)),
                             bn(Keyset::Exponent2, sizeof(Keyset::Exponent2)),
                             bn(Keyset::Coefficient, sizeof(Keyset::Coefficient)))) {
        RSA_free(rsa);
        return nullptr;
    }
    return rsa;
}

} // namespace

bool Crypto::RSA2048Decrypt(std::span<u8, 32> dec_key, std::span<const u8, 256> ciphertext,
                            bool is_dk3) {
    RSA* rsa = is_dk3 ? MakePrivateKey<PkgDerivedKey3Keyset>() : MakePrivateKey<FakeKeyset>();
    if (!rsa) {
        return false;
    }
    std::array<u8, 256> decrypted{};
    const int len = RSA_private_decrypt(static_cast<int>(ciphertext.size()), ciphertext.data(),
                                        decrypted.data(), rsa, RSA_PKCS1_PADDING);
    RSA_free(rsa);
    if (len < static_cast<int>(dec_key.size())) {
        return false;
    }
    std::memcpy(dec_key.data(), decrypted.data(), dec_key.size());
    return true;
}

void Crypto::ivKeyHASH256(std::span<const u8, 64> cipher_input, std::span<u8, 32> ivkey_result) {
    std::array<u8, SHA256_DIGEST_LENGTH> digest{};
    SHA256(cipher_input.data(), cipher_input.size(), digest.data());
    std::memcpy(ivkey_result.data(), digest.data(), ivkey_result.size());
}

void Crypto::aesCbcCfb128Decrypt(std::span<const u8, 32> ivkey, std::span<const u8> ciphertext,
                                 std::span<u8> decrypted) {
    AES_KEY key{};
    AES_set_decrypt_key(ivkey.data() + 16, 128, &key);
    std::array<u8, 16> iv{};
    std::memcpy(iv.data(), ivkey.data(), iv.size());
    const size_t size = std::min(ciphertext.size(), decrypted.size()) & ~size_t{15};
    AES_cbc_encrypt(ciphertext.data(), decrypted.data(), size, &key, iv.data(), AES_DECRYPT);
}

void Crypto::PfsGenCryptoKey(std::span<const u8, 32> ekpfs, std::span<const u8, 16> seed,
                             std::span<u8, 16> dataKey, std::span<u8, 16> tweakKey) {
    std::array<u8, 20> d{};
    const u32 index = 1;
    std::memcpy(d.data(), &index, sizeof(index));
    std::memcpy(d.data() + sizeof(index), seed.data(), seed.size());
    std::array<u8, SHA256_DIGEST_LENGTH> digest{};
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), ekpfs.data(), static_cast<int>(ekpfs.size()), d.data(), d.size(),
         digest.data(), &digest_len);
    std::memcpy(tweakKey.data(), digest.data(), tweakKey.size());
    std::memcpy(dataKey.data(), digest.data() + tweakKey.size(), dataKey.size());
}

void Crypto::decryptPFS(std::span<const u8, 16> dataKey, std::span<const u8, 16> tweakKey,
                        std::span<const u8> src_image, std::span<u8> dst_image, u64 sector) {
    std::array<u8, 32> key{};
    std::memcpy(key.data(), dataKey.data(), 16);
    std::memcpy(key.data() + 16, tweakKey.data(), 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    const size_t size = std::min(src_image.size(), dst_image.size());
    for (size_t offset = 0; offset < size; offset += 0x1000) {
        const size_t chunk = std::min<size_t>(0x1000, size - offset) & ~size_t{15};
        if (chunk < 16) {
            break;
        }
        const u64 current_sector = sector + offset / 0x1000;
        std::array<u8, 16> tweak{};
        std::memcpy(tweak.data(), &current_sector, sizeof(current_sector));
        int out_len = 0;
        EVP_DecryptInit_ex(ctx, EVP_aes_128_xts(), nullptr, key.data(), tweak.data());
        EVP_CIPHER_CTX_set_padding(ctx, 0);
        EVP_DecryptUpdate(ctx, dst_image.data() + offset, &out_len, src_image.data() + offset,
                          static_cast<int>(chunk));
        EVP_DecryptFinal_ex(ctx, dst_image.data() + offset + out_len, &out_len);
    }
    EVP_CIPHER_CTX_free(ctx);
}
