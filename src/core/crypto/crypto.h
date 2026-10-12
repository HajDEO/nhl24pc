// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include "common/types.h"

// PS4 package (PKG / PFS) cryptography on top of LibreSSL; the algorithms follow the code that
// used to live in shadPS4 (Crypto++ based).
class Crypto {
public:
    /// RSAES-PKCS1-v1_5 decryption of a 256-byte block with the package key set (dk3 or the
    /// "fake" key set of fPKGs); the first 32 bytes of the message are the key.
    bool RSA2048Decrypt(std::span<u8, 32> dec_key, std::span<const u8, 256> ciphertext,
                        bool is_dk3);
    void ivKeyHASH256(std::span<const u8, 64> cipher_input, std::span<u8, 32> ivkey_result);
    /// AES-128-CBC decryption, key = ivkey[16..32), iv = ivkey[0..16); sizes multiple of 16.
    void aesCbcCfb128Decrypt(std::span<const u8, 32> ivkey, std::span<const u8> ciphertext,
                             std::span<u8> decrypted);
    void PfsGenCryptoKey(std::span<const u8, 32> ekpfs, std::span<const u8, 16> seed,
                         std::span<u8, 16> dataKey, std::span<u8, 16> tweakKey);
    /// AES-128-XTS decryption of a PFS image in 0x1000-byte sectors starting at `sector`.
    void decryptPFS(std::span<const u8, 16> dataKey, std::span<const u8, 16> tweakKey,
                    std::span<const u8> src_image, std::span<u8> dst_image, u64 sector);
};
