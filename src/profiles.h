#pragma once
#include <stddef.h>
#include <stdint.h>
#include <cc1101.h>

struct RadioConfig {
  CC1101::Modulation modulation;
  float freq_mhz;
  float datarate_kbaud;
  float rx_bw_khz;
  uint8_t fixed_len;
};

// Declare before its used in struct
using DecodeFn = bool (*)(const uint8_t* data, size_t len, char* out, size_t out_len);

struct RemoteControlProfile {
  const char* name;
  RadioConfig radio;
  DecodeFn decode;
};

bool decode_irange_remote(const uint8_t* data, size_t len, char* out, size_t out_len);
bool decode_non_irange_remote(const uint8_t* data, size_t len, char* out, size_t out_len);

// Pick our active profile at compile time
#if defined(REMOTE_PROFILE_IRANGE)
static const RemoteControlProfile  ACTIVE_PROFILE = {
  "iRange Remote (FSK)",
  { CC1101::MOD_2FSK, 433.913f, 20.0f, 58.0f, 64 },
  decode_irange_remote
};
#elif defined(REMOTE_PROFILE_NON_IRANGE)
static const RemoteControlProfile ACTIVE_PROFILE = {
  "Non iRange Remote (ASK/OOK)",
  { CC1101::MOD_ASK_OOK, 433.92f, 3.0f, 58.0f, 64 },
  decode_non_irange_remote
};
#else
#error "Select a profile: -DREMOTE_PROFILE_IRANGE or -DREMOTE_PROFILE_NON_IRANGE"
#endif
