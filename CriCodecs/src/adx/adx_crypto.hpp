#pragma once
/**
 * @file adx_crypto.hpp
 * @brief ADX/AHX key derivation helpers.
 *
 * Type-8 and type-9 derivation follows the ADX-family behavior cross-checked
 * against VGAudio/vgmstream references and CRI tooling evidence.
 */

#include <cstdint>
#include <string_view>

#include "../utilities/primes.hpp"

namespace cricodecs::adx {

    inline constexpr auto KEY8_PRIMES = cricodecs::util::generate_primes_in_range<uint16_t, 0x401B, 0x683A>();

    struct AdxKeyState {
        uint16_t xor_value = 0;
        uint16_t mult = 0;
        uint16_t add = 0;

        void advance() {
            xor_value = static_cast<uint16_t>((xor_value * mult + add) & 0x7FFF);
        }
    };

    inline void key8_derive(std::string_view key_string, uint16_t& out_xor, uint16_t& out_mult, uint16_t& out_add) {
        if (key_string.empty()) {
            out_xor = out_mult = out_add = 0;
            return;
        }

        size_t len = key_string.size();
        uint16_t start = KEY8_PRIMES[0x100];
        uint16_t mult = KEY8_PRIMES[0x200];
        uint16_t add = KEY8_PRIMES[0x300];

        for (size_t i = 0; i < len; ++i) {
            uint8_t c = (uint8_t)key_string[i];
            
            uint32_t p = KEY8_PRIMES[c + 0x80];
            
            start = KEY8_PRIMES[(start * p) % 0x400];
            mult = KEY8_PRIMES[(mult * p) % 0x400];
            add = KEY8_PRIMES[(add * p) % 0x400];
        }

        out_xor = start;
        out_mult = mult;
        out_add = add;
    }

    inline void key9_derive(uint64_t key, uint16_t subkey, uint16_t& out_xor, uint16_t& out_mult, uint16_t& out_add) {
        out_xor  = static_cast<uint16_t>(((key >> 48) & 0xFFFF) ^ ((key >> 16) & 0xFFFF));
        out_mult = static_cast<uint16_t>(((key >> 32) & 0xFFFF) ^ ((key >>  0) & 0xFFFF));
        out_add  = static_cast<uint16_t>((key >> 16) & 0xFFFF);

        if (subkey != 0) {
            uint16_t mul = ((subkey >> 8) | (subkey << 8)) & 0xFFFF;
            uint16_t add = subkey;
            out_xor = static_cast<uint16_t>((out_xor * mul + add) & 0x7FFF);
            out_mult = static_cast<uint16_t>((out_mult * mul + add) & 0x7FFF);
            out_add = static_cast<uint16_t>((out_add * mul + add) & 0x7FFF);
        }

        if (out_xor == 0) out_xor = 1;
        if (out_mult == 0) out_mult = 1;
        if (out_add == 0) out_add = 1;
    }

} // namespace cricodecs::adx
