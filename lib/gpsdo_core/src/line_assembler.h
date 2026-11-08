// Collects bytes from a UART into lines without blocking or allocating per byte.
// Over-long lines are cut and flagged so a stuck or noisy input cannot grow memory.
#pragma once
#include <cstddef>
#include <string>

namespace gpsdo {

class LineAssembler {
public:
  explicit LineAssembler(size_t maxLen = 256) : maxLen_(maxLen) { buf_.reserve(maxLen); }

  // Feed one byte. Returns true when a complete line is ready in line().
  bool push(char c) {
    if (c == '\n') {
      if (!buf_.empty() && buf_.back() == '\r') buf_.pop_back();
      line_.swap(buf_);
      buf_.clear();
      bool wasCut = overflow_;
      overflow_ = false;
      truncated_ = wasCut;
      return true;
    }
    if (buf_.size() >= maxLen_) { overflow_ = true; return false; }
    buf_.push_back(c);
    return false;
  }

  const std::string& line() const { return line_; }
  bool truncated() const { return truncated_; }  // the last line was cut short

private:
  size_t maxLen_;
  std::string buf_, line_;
  bool overflow_ = false, truncated_ = false;
};

}  // namespace gpsdo
