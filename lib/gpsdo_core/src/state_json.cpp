#include "state_json.h"

#include <cstdarg>
#include <cstdio>

namespace gpsdo {
namespace {

void add(std::string& s, const char* key, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
void add(std::string& s, const char* key, const char* fmt, ...) {
  char b[96];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  s += s.size() > 1 ? ",\"" : "\"";
  s += key;
  s += "\":";
  s += b;
}

std::string quoted(const std::string& v) {
  std::string o = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}

const char* tf(bool b) { return b ? "true" : "false"; }

}  // namespace

std::string stateJson(const Monitor& m, uint32_t nowMs) {
  std::string s = "{";
  const ReportTracker& n = m.nano();
  const ReportLine& d = n.last();
  add(s, "state", "\"%s\"", headlineText(m.headline()));
  add(s, "reference_ok", "%s", tf(m.referenceOk()));
  add(s, "locked", "%s", tf(n.locked() && n.msSinceData(nowMs) <= Monitor::kSilentMs));
  add(s, "locked_s", "%u", (unsigned)n.secondsLocked());
  add(s, "lock_losses", "%u", (unsigned)n.lockLosses());
  if (n.haveData()) {
    if (!d.missing10MHz) add(s, "tic_ns", "%ld", (long)d.ns);
    add(s, "dac", "%u", (unsigned)d.dac);
    add(s, "dac_near_limit", "%s", tf(ReportTracker::dacWarning(d.dac)));
    add(s, "ocxo_c", "%.1f", d.ocxoC);
    if (d.hasDetail) {
      add(s, "diff_ns", "%ld", (long)d.diffNs);
      add(s, "tic_error_ns", "%.1f", m.ticErrorNs());
      add(s, "tc_s", "%u", (unsigned)d.tc);
      add(s, "ambient_c", "%.1f", d.ambientC);
    }
    uint32_t e = m.earliestLockS();
    if (e) add(s, "earliest_lock_s", "%u", (unsigned)e);
  }
  if (m.msSinceNanoLine(nowMs) != UINT32_MAX) add(s, "gpsdo_age_s", "%u", (unsigned)(m.msSinceNanoLine(nowMs) / 1000));
  if (!n.revision().empty()) {
    s += ",\"firmware\":";
    s += quoted(n.revision());
  }
  add(s, "gps_seen", "%s", tf(m.gpsEverSeen()));
  if (m.gpsEverSeen()) {
    const NmeaParser& g = m.gps();
    const Fix& f = g.fix();
    bool noFix = m.alarms() & (kAlarmGpsNoFix | kAlarmGpsSilent);
    add(s, "fix", "\"%s\"", noFix ? "none" : f.mode == 3 ? "3D" : f.mode == 2 ? "2D" : "fix");
    add(s, "sats_used", "%u", f.haveGga ? (unsigned)f.satsUsed : (unsigned)g.satsUsed(nowMs));
    add(s, "sats_in_view", "%u", (unsigned)g.satsInView());
    add(s, "hdop", "%.2f", f.hdop);
    add(s, "gps_age_s", "%u", (unsigned)((nowMs - g.lastGoodMs()) / 1000));
  }
  static const struct { uint16_t bit; const char* name; } kNames[] = {
      {kAlarmNanoSilent, "gpsdo_silent"}, {kAlarmNoPps, "no_pps"},         {kAlarmMissing10MHz, "no_10mhz"},
      {kAlarmUnlocked, "unlocked"},       {kAlarmDacLimit, "dac_near_limit"}, {kAlarmGpsSilent, "gps_silent"},
      {kAlarmGpsNoFix, "gps_no_fix"},     {kAlarmGpsFewSats, "gps_few_sats"}, {kAlarmRefSuspect, "reference_suspect"},
  };
  s += ",\"alarms\":[";
  bool first = true;
  for (const auto& a : kNames) {
    // Before the GPS tap is wired its silence is expected, so it is not reported as an alarm.
    if (a.bit == kAlarmGpsSilent && !m.gpsEverSeen()) continue;
    if (!(m.alarms() & a.bit)) continue;
    s += first ? "\"" : ",\"";
    s += a.name;
    s += "\"";
    first = false;
  }
  s += "]}";
  return s;
}

namespace {
struct Entity {
  const char* component;
  const char* id;
  const char* name;
  const char* value;  // Jinja template
  const char* extra;  // extra JSON members, already comma-led, or ""
};
const Entity kEntities[] = {
    {"binary_sensor", "reference_ok", "Reference OK", "{{ 'ON' if value_json.reference_ok else 'OFF' }}", ""},
    {"binary_sensor", "locked", "Locked", "{{ 'ON' if value_json.locked else 'OFF' }}", ""},
    {"sensor", "state", "State", "{{ value_json.state }}", ""},
    {"sensor", "tic_ns", "TIC", "{{ value_json.tic_ns }}", ",\"unit_of_measurement\":\"ns\",\"state_class\":\"measurement\""},
    {"sensor", "tic_error_ns", "TIC error", "{{ value_json.tic_error_ns }}", ",\"unit_of_measurement\":\"ns\",\"state_class\":\"measurement\""},
    {"sensor", "dac", "DAC", "{{ value_json.dac }}", ",\"state_class\":\"measurement\""},
    {"sensor", "ocxo_c", "OCXO temperature", "{{ value_json.ocxo_c }}", ",\"device_class\":\"temperature\",\"unit_of_measurement\":\"\\u00b0C\",\"state_class\":\"measurement\""},
    {"sensor", "ambient_c", "Ambient temperature", "{{ value_json.ambient_c }}", ",\"device_class\":\"temperature\",\"unit_of_measurement\":\"\\u00b0C\",\"state_class\":\"measurement\""},
    {"sensor", "locked_s", "Time locked", "{{ value_json.locked_s }}", ",\"device_class\":\"duration\",\"unit_of_measurement\":\"s\""},
    {"sensor", "lock_losses", "Lock losses", "{{ value_json.lock_losses }}", ",\"state_class\":\"total_increasing\""},
    {"sensor", "sats_used", "Satellites used", "{{ value_json.sats_used }}", ",\"state_class\":\"measurement\""},
};
}  // namespace

int Discovery::count() { return (int)(sizeof kEntities / sizeof kEntities[0]); }

bool Discovery::entity(int i, const std::string& base, const std::string& deviceId, std::string& topic,
                       std::string& payload) {
  if (i < 0 || i >= count()) return false;
  const Entity& e = kEntities[i];
  topic = std::string("homeassistant/") + e.component + "/" + deviceId + "/" + e.id + "/config";
  payload = std::string("{\"name\":\"") + e.name + "\",\"unique_id\":\"" + deviceId + "_" + e.id +
            "\",\"state_topic\":\"" + base + "/state\",\"value_template\":\"" + e.value +
            "\",\"availability_topic\":\"" + base + "/status\"" + e.extra +
            ",\"device\":{\"identifiers\":[\"" + deviceId +
            "\"],\"name\":\"GPSDO\",\"model\":\"Lars GPSDO with CYD monitor\",\"manufacturer\":\"M0LTE\"}}";
  return true;
}

}  // namespace gpsdo
