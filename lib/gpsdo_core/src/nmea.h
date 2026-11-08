// NMEA 0183 parser for the u-blox GPS module's output: GGA, GSA, GSV, RMC, ZDA.
// Handles multiple talkers (GP/GL/GA/GB/BD/GQ/GN), multi-part GSV, NMEA 4.10 system and signal IDs,
// and u-blox legacy extended PRN numbering when a GN GSA has no system ID.
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace gpsdo {

enum class Constellation : uint8_t { Unknown, Gps, Sbas, Glonass, Galileo, BeiDou, Qzss };
const char* constellationPrefix(Constellation c);  // "G", "S", "R", "E", "C", "J", "?"

struct Satellite {
  bool valid = false;
  Constellation sys = Constellation::Unknown;
  uint16_t prn = 0;
  int8_t elevation = -1;   // degrees, -1 when not given
  int16_t azimuth = -1;    // degrees, -1 when not given
  int8_t snr = -1;         // dB-Hz, -1 when not tracked (empty field)
  uint32_t seenMs = 0;     // last GSV mention
  uint32_t usedMs = 0;     // last GSA mention
  bool everUsed = false;
};

struct UtcTime {
  bool valid = false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
  bool dateValid = false;
};

struct Fix {
  uint8_t quality = 0;      // GGA: 0 none, 1 GPS, 2 DGPS, 6 estimated...
  uint8_t satsUsed = 0;     // GGA
  float hdop = 0, pdop = 0, vdop = 0;
  bool haveAltitude = false;
  float altitudeM = 0;
  bool haveLatLon = false;  // GGA position; shown on screen only, never published
  double latDeg = 0, lonDeg = 0;
  uint8_t mode = 1;         // GSA: 1 none, 2 2D, 3 3D
  uint32_t ggaMs = 0, gsaMs = 0;
  bool haveGga = false, haveGsa = false;
};

class NmeaParser {
public:
  static constexpr size_t kMaxSats = 72;
  static constexpr uint32_t kExpireMs = 10000;   // drop satellites not seen for this long
  static constexpr uint32_t kUsedWindowMs = 3000; // used-in-fix if a GSA named it this recently

  // Feed one sentence (starting with '$', with or without "\r\n"). Returns true if it was valid
  // (checksum correct) and understood.
  bool parse(const std::string& sentence, uint32_t nowMs);

  const Fix& fix() const { return fix_; }
  const UtcTime& utc() const { return utc_; }
  uint32_t utcMs() const { return utcMs_; }         // when utc() was last set
  uint32_t utcSetCount() const { return utcSets_; }  // increments each time utc() is set

  // Remove satellites not mentioned in a GSV for kExpireMs.
  void expire(uint32_t nowMs);
  const std::array<Satellite, kMaxSats>& satellites() const { return sats_; }
  bool isUsed(const Satellite& s, uint32_t nowMs) const {
    return s.everUsed && nowMs - s.usedMs <= kUsedWindowMs;
  }
  size_t satsInView() const;
  size_t satsUsed(uint32_t nowMs) const;

  uint32_t goodSentences() const { return good_; }
  uint32_t badChecksums() const { return badChecksum_; }
  uint32_t lastGoodMs() const { return lastGoodMs_; }
  bool sawUbx() const { return sawUbx_; }
  void noteUbx() { sawUbx_ = true; }
  const std::string& lastTxt() const { return lastTxt_; }  // most recent $xxTXT text
  // Antenna supervisor state from "ANTSTATUS=..." notices (OK, SHORT, OPEN, ...); empty if never seen.
  const std::string& antennaStatus() const { return antenna_; }

  static bool checksumOk(const std::string& s);

private:
  Satellite* findOrAdd(Constellation sys, uint16_t prn, uint32_t nowMs);
  void parseGga(const std::string* f, size_t n, uint32_t nowMs);
  void parseGsa(const std::string& talker, const std::string* f, size_t n, uint32_t nowMs);
  void parseGsv(const std::string& talker, const std::string* f, size_t n, uint32_t nowMs);
  void parseRmc(const std::string* f, size_t n, uint32_t nowMs);
  void parseZda(const std::string* f, size_t n, uint32_t nowMs);
  bool parseTime(const std::string& hhmmss);

  Fix fix_;
  UtcTime utc_;
  uint32_t utcMs_ = 0;
  uint32_t utcSets_ = 0;
  std::array<Satellite, kMaxSats> sats_{};
  uint32_t good_ = 0, badChecksum_ = 0, lastGoodMs_ = 0;
  bool sawUbx_ = false;
  std::string lastTxt_, antenna_;
};

// Constellation from a talker ID ("GP", "GL", ...); GN gives Unknown.
Constellation constellationFromTalker(const std::string& talker);
// Constellation from an NMEA 4.10 system ID (1 GPS, 2 GLONASS, 3 Galileo, 4 BeiDou, 5 QZSS).
Constellation constellationFromSystemId(int id);
// u-blox extended numbering (NMEA 4.0 "strict" off): maps a PRN to its constellation and
// in-system satellite number.
Constellation constellationFromUbloxPrn(int prn, uint16_t& svOut);

}  // namespace gpsdo
