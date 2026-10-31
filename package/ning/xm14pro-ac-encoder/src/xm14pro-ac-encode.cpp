// SPDX-License-Identifier: LGPL-2.1-or-later

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "IRac.h"
#include "IRsend.h"
#include "IRutils.h"

static bool parse_long(const char *text, long minimum, long maximum, long *out) {
  if (!text || !*text) return false;
  char *end = nullptr;
  errno = 0;
  long value = std::strtol(text, &end, 10);
  if (errno || !end || *end || value < minimum || value > maximum) return false;
  *out = value;
  return true;
}

static bool parse_bool(const char *text, bool *out) {
  if (!std::strcmp(text, "1") || !strcasecmp(text, "on") ||
      !strcasecmp(text, "true")) {
    *out = true;
    return true;
  }
  if (!std::strcmp(text, "0") || !strcasecmp(text, "off") ||
      !strcasecmp(text, "false")) {
    *out = false;
    return true;
  }
  return false;
}

static bool parse_temperature(const char *text, float *out) {
  if (!text || !*text) return false;
  char *end = nullptr;
  errno = 0;
  float value = std::strtof(text, &end);
  if (errno || !end || *end || !std::isfinite(value) || value < 10 || value > 35)
    return false;
  *out = value;
  return true;
}

static void usage(const char *program) {
  std::fprintf(stderr,
      "usage: %s PROTOCOL MODEL POWER MODE TEMP FAN SWING_V SWING_H\n"
      "  MODE: auto|cool|heat|dry|fan\n"
      "  FAN: auto|min|low|medium|high|max\n"
      "  SWING_V: off|auto|highest|high|middle|low|lowest\n"
      "  SWING_H: off|auto|leftmax|left|middle|right|rightmax|wide\n",
      program);
}

int main(int argc, char **argv) {
  if (argc != 9) {
    usage(argv[0]);
    return 2;
  }

  const decode_type_t protocol = strToDecodeType(argv[1]);
  if (protocol == decode_type_t::UNKNOWN || !IRac::isProtocolSupported(protocol)) {
    std::fprintf(stderr, "unsupported air-conditioner protocol: %s\n", argv[1]);
    return 2;
  }

  long model_value = -1;
  bool power = false;
  float degrees = 0;
  if (!parse_long(argv[2], -1, SHRT_MAX, &model_value) ||
      !parse_bool(argv[3], &power) || !parse_temperature(argv[5], &degrees)) {
    std::fprintf(stderr, "invalid model, power, or temperature\n");
    return 2;
  }

  const stdAc::opmode_t mode = IRac::strToOpmode(argv[4], stdAc::opmode_t::kOff);
  const stdAc::fanspeed_t fan =
      IRac::strToFanspeed(argv[6], static_cast<stdAc::fanspeed_t>(-1));
  const stdAc::swingv_t swing_v =
      IRac::strToSwingV(argv[7], static_cast<stdAc::swingv_t>(-2));
  const stdAc::swingh_t swing_h =
      IRac::strToSwingH(argv[8], static_cast<stdAc::swingh_t>(-2));
  if (mode == stdAc::opmode_t::kOff ||
      fan == static_cast<stdAc::fanspeed_t>(-1) ||
      swing_v == static_cast<stdAc::swingv_t>(-2) ||
      swing_h == static_cast<stdAc::swingh_t>(-2)) {
    std::fprintf(stderr, "invalid mode, fan, or swing setting\n");
    return 2;
  }

  timingList.clear();
  timingFrequency = 0;
  IRac encoder(kGpioUnused);
  if (!encoder.sendAc(protocol, static_cast<int16_t>(model_value), power, mode,
                      degrees, true, fan, swing_v, swing_h,
                      false, false, false, true, false, false, true)) {
    std::fprintf(stderr, "protocol rejected requested state\n");
    return 3;
  }
  if (timingFrequency < 30000 || timingFrequency > 60000 || timingList.empty()) {
    std::fprintf(stderr, "encoder produced no usable carrier or timing data\n");
    return 3;
  }

  std::printf("%u\n", timingFrequency);
  for (size_t index = 0; index < timingList.size(); ++index) {
    if (timingList[index] <= 0) {
      std::fprintf(stderr, "encoder produced an invalid timing value\n");
      return 3;
    }
    if (index) std::putchar(',');
    std::printf("%d", timingList[index]);
  }
  std::putchar('\n');
  return 0;
}
