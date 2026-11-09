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
  bool lockKnown;
  add(s, "locked_s", "%u", (unsigned)m.secondsLocked(nowMs, lockKnown));
  add(s, "lock_losses", "%u", (unsigned)n.lockLosses());
  add(s, "gpsdo_restarts", "%u", (unsigned)m.gpsdoRestarts());
  add(s, "gps_restarts", "%u", (unsigned)m.gpsRestarts());
  if (m.gpsdoRestarts() || m.gpsRestarts())
    add(s, "last_restart_age_s", "%u", (unsigned)(ageMs(nowMs, m.lastRestartMs()) / 1000));
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
    if (m.warmUpRemainingS()) add(s, "warmup_remaining_s", "%u", (unsigned)m.warmUpRemainingS());
  }
  if (m.msSinceNanoLine(nowMs) != UINT32_MAX) add(s, "gpsdo_age_s", "%u", (unsigned)(m.msSinceNanoLine(nowMs) / 1000));
  if (!n.revision().empty()) {
    s += ",\"firmware\":";
    s += quoted(n.revision());
  }
  if (m.haveStatus(nowMs)) {
    const StatusLine& st = m.status();
    add(s, "lock_counter", "%u", (unsigned)st.lockCount);
    add(s, "lock_target", "%u", (unsigned)st.lockTarget);
    add(s, "lock_test_tic_ns", "%ld", (long)st.ticErrNs);
    add(s, "lock_test_freq_ns", "%ld", (long)st.freqErrNs);
    add(s, "missed_pps_total", "%u", (unsigned)st.missedPps);
    add(s, "since_missed_pps_s", "%u", (unsigned)st.sinceMissedS);
  }
  float ppb;
  if (m.frequencyPpb(1800, ppb)) add(s, "freq_error_ppb_30min", "%.4f", ppb);
  if (m.frequencyPpb(300, ppb)) add(s, "freq_error_ppb_5min", "%.4f", ppb);
  add(s, "missed_pps_since_display", "%u", (unsigned)m.missedPpsSinceStart());
  add(s, "damaged_lines", "%u", (unsigned)m.damagedLines());
  add(s, "gps_position_learned", "%s", tf(m.watch().positionLearned()));
  add(s, "gps_seen", "%s", tf(m.gpsEverSeen()));
  if (!m.gps().antennaStatus().empty()) {
    s += ",\"antenna\":";
    s += quoted(m.gps().antennaStatus());
  }
  if (m.gpsEverSeen()) {
    const NmeaParser& g = m.gps();
    const Fix& f = g.fix();
    bool noFix = m.alarms() & (kAlarmGpsNoFix | kAlarmGpsSilent);
    add(s, "fix", "\"%s\"", noFix ? "none" : f.mode == 3 ? "3D" : f.mode == 2 ? "2D" : "fix");
    add(s, "sats_used", "%u", f.haveGga ? (unsigned)f.satsUsed : (unsigned)g.satsUsed(nowMs));
    add(s, "sats_in_view", "%u", (unsigned)g.satsInView());
    add(s, "hdop", "%.2f", f.hdop);
    add(s, "gps_age_s", "%u", (unsigned)(ageMs(nowMs, g.lastGoodMs()) / 1000));
  }
  static const struct { uint16_t bit; const char* name; } kNames[] = {
      {kAlarmNanoSilent, "gpsdo_silent"}, {kAlarmNoPps, "no_pps"},         {kAlarmMissing10MHz, "no_10mhz"},
      {kAlarmUnlocked, "unlocked"},       {kAlarmDacLimit, "dac_near_limit"}, {kAlarmGpsSilent, "gps_silent"},
      {kAlarmGpsNoFix, "gps_no_fix"},     {kAlarmGpsFewSats, "gps_few_sats"}, {kAlarmRefSuspect, "reference_suspect"},
      {kAlarmGpsSpoof, "gps_spoofing"},   {kAlarmGpsSignalLoss, "gps_signal_loss"},
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
    {"binary_sensor", "reference_ok", "Reference OK", "{{ 'ON' if value_json.reference_ok else 'OFF' }}",
     ",\"icon\":\"mdi:check-decagram\""},
    {"binary_sensor", "locked", "Locked", "{{ 'ON' if value_json.locked else 'OFF' }}", ",\"icon\":\"mdi:lock\""},
    {"binary_sensor", "gps_interference", "GPS interference",
     "{{ 'ON' if ('gps_spoofing' in value_json.alarms or 'gps_signal_loss' in value_json.alarms) else 'OFF' }}",
     ",\"device_class\":\"problem\""},
    {"sensor", "state", "State", "{{ value_json.state }}", ",\"icon\":\"mdi:information-outline\""},
    {"sensor", "tic_ns", "Phase vs GPS pulse", "{{ value_json.tic_ns }}",
     ",\"unit_of_measurement\":\"ns\",\"state_class\":\"measurement\",\"icon\":\"mdi:sine-wave\""},
    {"sensor", "tic_error_ns", "Phase error (lock test)",
     "{{ value_json.lock_test_tic_ns if value_json.lock_test_tic_ns is defined else value_json.tic_error_ns }}",
     ",\"unit_of_measurement\":\"ns\",\"state_class\":\"measurement\",\"suggested_display_precision\":0,\"icon\":\"mdi:target\""},
    {"sensor", "dac", "Oscillator tuning (DAC)", "{{ value_json.dac }}",
     ",\"state_class\":\"measurement\",\"icon\":\"mdi:tune-vertical\""},
    {"sensor", "ocxo_c", "Oscillator temperature", "{{ value_json.ocxo_c }}", ",\"device_class\":\"temperature\",\"unit_of_measurement\":\"\\u00b0C\",\"state_class\":\"measurement\""},
    {"sensor", "ambient_c", "Ambient temperature", "{{ value_json.ambient_c }}", ",\"device_class\":\"temperature\",\"unit_of_measurement\":\"\\u00b0C\",\"state_class\":\"measurement\""},
    {"sensor", "locked_s", "Time locked", "{{ value_json.locked_s }}",
     ",\"device_class\":\"duration\",\"unit_of_measurement\":\"s\",\"suggested_display_precision\":0,\"icon\":\"mdi:timer-lock-outline\""},
    {"sensor", "lock_progress", "Lock progress",
     "{{ [100, ((value_json.lock_counter / (value_json.lock_target + 1)) * 100) | round(0)] | min if value_json.lock_target is defined else (100 if value_json.locked else 0) }}",
     ",\"unit_of_measurement\":\"%\",\"icon\":\"mdi:progress-clock\""},
    {"sensor", "lock_losses", "Lock losses", "{{ value_json.lock_losses }}", ",\"state_class\":\"total_increasing\",\"icon\":\"mdi:lock-alert\""},
    {"sensor", "missed_pps", "Missed GPS pulses", "{{ value_json.missed_pps_total | default(0) }}",
     ",\"state_class\":\"total_increasing\",\"icon\":\"mdi:pulse\""},
    {"sensor", "sats_used", "Satellites used", "{{ value_json.sats_used }}", ",\"state_class\":\"measurement\",\"icon\":\"mdi:satellite-variant\""},
    {"sensor", "freq_error_ppb_30min", "Frequency error (30 min)", "{{ value_json.freq_error_ppb_30min }}",
     ",\"unit_of_measurement\":\"ppb\",\"state_class\":\"measurement\",\"suggested_display_precision\":4,\"icon\":\"mdi:sine-wave\""},
    {"sensor", "antenna", "GPS antenna", "{{ value_json.antenna if value_json.antenna is defined else 'not reported yet' }}",
     ",\"icon\":\"mdi:antenna\""},
    {"sensor", "gpsdo_restarts", "GPSDO restarts", "{{ value_json.gpsdo_restarts }}", ",\"state_class\":\"total_increasing\",\"icon\":\"mdi:restart\""},
    {"sensor", "gps_restarts", "GPS restarts", "{{ value_json.gps_restarts }}", ",\"state_class\":\"total_increasing\",\"icon\":\"mdi:restart\""},
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
