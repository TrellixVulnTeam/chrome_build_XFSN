// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_INSTALLER_ZUCCHINI_PATCH_UTILS_H_
#define CHROME_INSTALLER_ZUCCHINI_PATCH_UTILS_H_

#include <stdint.h>

#include <type_traits>

#include "base/logging.h"
#include "base/optional.h"
#include "chrome/installer/zucchini/image_utils.h"

namespace zucchini {

// Constants that appear inside a patch.
enum class PatchType : uint32_t {
  kRawPatch = 0,
  kSinglePatch = 1,
  kEnsemblePatch = 2,
  kUnrecognisedPatch,
};

// A Zucchini 'ensemble' patch is the concatenation of a patch header with a
// list of patch 'elements', each containing data for patching individual
// elements.

// Supported by MSVC, g++, and clang++. Ensures no gaps in packing.
#pragma pack(push, 1)

// Header for a Zucchini patch, found at the begining of an ensemble patch.
struct PatchHeader {
  // Magic signature at the beginning of a Zucchini patch file.
  static constexpr uint32_t kMagic = 'Z' | ('u' << 8) | ('c' << 16);

  uint32_t magic = 0;
  uint32_t old_size = 0;
  uint32_t old_crc = 0;
  uint32_t new_size = 0;
  uint32_t new_crc = 0;
};

// Sanity check.
static_assert(sizeof(PatchHeader) == 20, "PatchHeader is 20 bytes");

// Header for a patch element, found at the begining of every patch element.
struct PatchElementHeader {
  uint32_t old_offset;
  uint32_t new_offset;
  uint64_t old_length;
  uint64_t new_length;
  uint32_t exe_type;
};

// Sanity check.
static_assert(sizeof(PatchElementHeader) == 28,
              "PatchElementHeader is 28 bytes");

#pragma pack(pop)

// Descibes a raw FIX operation.
struct RawDeltaUnit {
  offset_t copy_offset;  // Offset in copy regions starting from last delta.
  int8_t diff;           // Bytewise difference.
};

// A Zucchini patch contains data streams encoded using varint format to reduce
// uncompressed size. This is a variable-length encoding for integer quantities
// that strips away leading (most-significant) null bytes.

// Writes |value| as a varint in |dst| and returns an iterator pointing beyond
// the written region. |dst| is assumed to hold enough space. Typically, this
// will write to a vector using back insertion, e.g.:
//   EncodeVarUInt(value, std::back_inserter(vector));
template <class T, class It>
It EncodeVarUInt(T value, It dst) {
  static_assert(std::is_unsigned<T>::value, "Value type must be unsigned");

  while (value >= 128) {
    *dst++ = static_cast<uint8_t>(value) | 128;
    value >>= 7;
  }
  *dst++ = static_cast<uint8_t>(value);
  return dst;
}

// Same as EncodeVarUInt(), but for signed values.
template <class T, class It>
It EncodeVarInt(T value, It dst) {
  static_assert(std::is_signed<T>::value, "Value type must be signed");

  using unsigned_value_type = typename std::make_unsigned<T>::type;
  if (value < 0)
    return EncodeVarUInt((unsigned_value_type(~value) << 1) | 1, dst);
  else
    return EncodeVarUInt(unsigned_value_type(value) << 1, dst);
}

// Tries to read a varint unsigned integer from [|first|, |last|). If
// succesfull, writes result into |value| and returns an iterator pointing
// beyond the formatted varint. Otherwise returns nullopt.
template <class T, class It>
base::Optional<It> DecodeVarUInt(It first, It last, T* value) {
  static_assert(std::is_unsigned<T>::value, "Value type must be unsigned");

  uint8_t sh = 0;
  T val = 0;
  while (first != last) {
    val |= T(*first & 0x7F) << sh;
    if (*(first++) < 0x80) {
      *value = val;
      return first;
    }
    sh += 7;
    if (sh >= sizeof(T) * 8) {  // Overflow!
      LOG(ERROR) << "Overflow encountered.";
      return base::nullopt;
    }
  }
  LOG(ERROR) << "Exhausted data while reading.";
  return base::nullopt;
}

// Same as DecodeVarUInt(), but for signed values.
template <class T, class It>
base::Optional<It> DecodeVarInt(It first, It last, T* value) {
  static_assert(std::is_signed<T>::value, "Value type must be signed");

  typename std::make_unsigned<T>::type tmp = 0;
  auto res = DecodeVarUInt(first, last, &tmp);
  if (!res)
    return res;
  if (tmp & 1)
    *value = ~static_cast<T>(tmp >> 1);
  else
    *value = static_cast<T>(tmp >> 1);
  return res;
}

}  // namespace zucchini

#endif  // CHROME_INSTALLER_ZUCCHINI_PATCH_UTILS_H_
