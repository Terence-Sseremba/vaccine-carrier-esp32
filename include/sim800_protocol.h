#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum class AtReplyKind {
  None, Ok, Error, CmsError, Cmgs, Prompt, SimReady, SimNotReady,
  RegisteredHome, RegisteredRoaming, NotRegistered, Overflow, Other
};

struct AtReply {
  AtReplyKind kind = AtReplyKind::None;
  int code = -1;
};

// A line is deliberately small: the commands used here have short replies.
// Oversized lines are discarded through the next newline, never truncated into
// a reply that might accidentally be treated as an acknowledgement.
class Sim800Parser {
 public:
  static constexpr size_t CAPACITY = 96;

  void reset() { used_ = 0; overflow_ = false; promptSpace_ = false; }

  AtReply feed(char c) {
    if (c == '\r') return {};
    if (c == '\n') {
      if (overflow_) { reset(); return {AtReplyKind::Overflow, -1}; }
      line_[used_] = '\0';
      AtReply result = classify(line_);
      reset();
      return result;
    }
    if (promptSpace_ && c == ' ') { promptSpace_ = false; return {}; }
    promptSpace_ = false;
    if (used_ == 0 && c == '>') { promptSpace_ = true; return {AtReplyKind::Prompt, -1}; }
    if (overflow_) return {};
    if (used_ >= CAPACITY - 1) { overflow_ = true; return {}; }
    line_[used_++] = c;
    return {};
  }

 private:
  static AtReply classify(const char* line) {
    while (*line == ' ') ++line;
    if (!*line) return {};
    if (strcmp(line, "OK") == 0) return {AtReplyKind::Ok, -1};
    if (strcmp(line, "ERROR") == 0) return {AtReplyKind::Error, -1};
    if (strncmp(line, "+CMS ERROR:", 11) == 0 || strncmp(line, "+CMGS:", 6) == 0) {
      const bool cms = line[3] == 'S';
      const char* number = line + (cms ? 11 : 6);
      char* end = nullptr;
      const long value = strtol(number, &end, 10);
      if (end == number || value < 0 || (*end != '\0' && *end != ' '))
        return {AtReplyKind::Other, -1};
      return {cms ? AtReplyKind::CmsError : AtReplyKind::Cmgs, static_cast<int>(value)};
    }
    if (strncmp(line, "+CPIN:", 6) == 0) {
      const char* value = line + 6;
      while (*value == ' ') ++value;
      return {strcmp(value, "READY") == 0 ? AtReplyKind::SimReady : AtReplyKind::SimNotReady, -1};
    }
    if (strncmp(line, "+CREG:", 6) == 0) {
      const char* comma = strchr(line + 6, ',');
      if (!comma) return {AtReplyKind::Other, -1};
      char* end = nullptr;
      const long state = strtol(comma + 1, &end, 10);
      if (end == comma + 1 || (*end != '\0' && *end != ',' && *end != ' '))
        return {AtReplyKind::Other, -1};
      if (state == 1) return {AtReplyKind::RegisteredHome, 1};
      if (state == 5) return {AtReplyKind::RegisteredRoaming, 5};
      return {AtReplyKind::NotRegistered, static_cast<int>(state)};
    }
    // Echoes, unsolicited indications and unsupported lines cannot advance a command.
    return {AtReplyKind::Other, -1};
  }

  char line_[CAPACITY] = {};
  size_t used_ = 0;
  bool overflow_ = false;
  bool promptSpace_ = false;
};
