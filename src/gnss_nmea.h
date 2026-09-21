#pragma once

#include <string>

namespace reconclave {

struct GnssFix {
  bool valid = false;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_m = 0.0;
  double speed_knots = 0.0;
  int satellites = 0;
  std::string utc;
  std::string last_sentence;
};

// Parses a checksum-valid GGA or RMC sentence. Other valid NMEA messages are
// accepted but do not change the fix.
//
// Deliberately has no dependency on GnssReceiver or libgpiod (unlike
// gnss_receiver.cpp, which needs gpiod to drive the modem's enable line):
// reconclave_k230 (the headless node) and sightline_recon_tests link this
// file alone to reuse the parsing logic without pulling in a GPIO/libgpiod
// dependency neither target has, since neither ever actually talks to the
// modem hardware - only reconclave_k230_ui does.
bool parseNmeaSentence(const std::string& sentence, GnssFix& fix);

}  // namespace reconclave
