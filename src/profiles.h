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

enum FeatureBits : uint32_t {
  FEAT_SOUND = 1u << 0,
  FEAT_FLAME = 1u << 1,
  FEAT_LEFT  = 1u << 2,
  FEAT_RIGHT = 1u << 3,
  FEAT_PLUS  = 1u << 4,
  FEAT_MINUS = 1u << 5,
};

struct RemoteControlProfile {
  const char* name;
  RadioConfig radio;
  uint32_t features;
};

// Pick our active profile at compile time
#if defined(REMOTE_PROFILE_IRANGE)
static const RemoteControlProfile  ACTIVE_PROFILE = {
  "iRange Remote (FSK)",
  { CC1101::MOD_2FSK, 433.913f, 20.0f, 58.0f, 64 },
  FEAT_SOUND | FEAT_FLAME | FEAT_LEFT | FEAT_RIGHT | FEAT_PLUS | FEAT_MINUS
};
#elif defined(REMOTE_PROFILE_NON_IRANGE)
static const RemoteControlProfile ACTIVE_PROFILE = {
  "Non iRange Remote (ASK/OOK)",
  { CC1101::MOD_ASK_OOK, 433.92f, 3.0f, 58.0f, 64 },
  FEAT_FLAME | FEAT_LEFT | FEAT_RIGHT | FEAT_PLUS | FEAT_MINUS
};
#else
#error "Select a profile: -DREMOTE_PROFILE_IRANGE or -DREMOTE_PROFILE_NON_IRANGE"
#endif
