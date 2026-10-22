#pragma once

#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace reconclave {
enum class UiJob { Wifi, Evidence, Capture, Nmap, Screenshot, Count };
enum class UiJobState { Idle, Running, Cancelling, Succeeded, Failed, Cancelled };
struct UiJobRecord {
  UiJobState state = UiJobState::Idle;
  std::string detail = "Not started";
  std::chrono::steady_clock::time_point started{}, finished{};
  bool active() const { return state == UiJobState::Running || state == UiJobState::Cancelling; }
  long elapsedSeconds() const {
    if (state == UiJobState::Idle) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(
        (active() ? std::chrono::steady_clock::now() : finished) - started).count();
  }
};

// UI-thread-owned lifecycle records. Workers report through their existing
// atomic completion channels; this tracker never invokes hardware or LVGL.
class UiJobs {
 public:
  bool begin(UiJob job, std::string detail) {
    auto& record = records_[static_cast<unsigned>(job)];
    if (record.active()) return false;
    record = {UiJobState::Running, std::move(detail), std::chrono::steady_clock::now(), {}};
    return true;
  }
  void cancelling(UiJob job) {
    auto& record = records_[static_cast<unsigned>(job)];
    if (record.active()) record.state = UiJobState::Cancelling;
  }
  void progress(UiJob job, std::string detail) {
    auto& record = records_[static_cast<unsigned>(job)];
    if (record.active()) record.detail = std::move(detail);
  }
  void finish(UiJob job, bool ok, std::string detail, bool cancelled = false) {
    auto& record = records_[static_cast<unsigned>(job)];
    if (!record.active()) return;
    record.state = cancelled ? UiJobState::Cancelled : ok ? UiJobState::Succeeded : UiJobState::Failed;
    record.detail = std::move(detail);
    record.finished = std::chrono::steady_clock::now();
  }
  const UiJobRecord& get(UiJob job) const { return records_[static_cast<unsigned>(job)]; }
  unsigned activeCount() const {
    unsigned count = 0;
    for (const auto& record : records_) if (record.active()) ++count;
    return count;
  }
 private:
  std::array<UiJobRecord, static_cast<unsigned>(UiJob::Count)> records_{};
};
inline const char* uiJobStateName(UiJobState state) {
  switch (state) {
    case UiJobState::Idle: return "Idle";
    case UiJobState::Running: return "Running";
    case UiJobState::Cancelling: return "Stopping";
    case UiJobState::Succeeded: return "Completed";
    case UiJobState::Failed: return "Failed";
    case UiJobState::Cancelled: return "Stopped";
  }
  return "Unknown";
}
}  // namespace reconclave
