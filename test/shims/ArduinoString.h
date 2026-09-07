// Arduino-compatible String shim for native tests
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>

class String {
 public:
  String() = default;
  String(const char *s)         : s_(s ? s : "") {}
  String(const char *s, size_t n): s_(s ? std::string(s, n) : "") {}
  String(const String &o)       : s_(o.s_) {}
  String(const std::string &o)  : s_(o) {}
  String(int v)                 { s_ = std::to_string(v); }
  String(unsigned v)             { s_ = std::to_string(v); }
  String(long v)                { s_ = std::to_string(v); }
  String(unsigned long v)       { s_ = std::to_string(v); }
  String(float v, int decimals = 2) {
    char b[40]; snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b;
  }
  String(double v, int decimals = 2) {
    char b[40]; snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b;
  }

  String &operator=(const String &o) { s_ = o.s_; return *this; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o)   { if (o) s_ += o; return *this; }
  String &operator+=(char c)         { s_ += c; return *this; }
  String operator+(const String &o) const { return String(s_ + o.s_); }
  String operator+(const char *o) const   { return String(s_ + (o ? o : "")); }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator<(const String &o) const  { return s_ <  o.s_; }

  const char *c_str() const { return s_.c_str(); }
  char       *c_str()       { return (char *)s_.c_str(); }
  size_t      length() const { return s_.size(); }
  bool        isEmpty() const { return s_.empty(); }
  void        reserve(size_t n) { s_.reserve(n); }

  int  indexOf(char c) const {
    auto p = s_.find(c);
    return p == std::string::npos ? -1 : (int)p;
  }
  int  indexOf(char c, size_t from) const {
    auto p = s_.find(c, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  int  indexOf(const char *needle) const {
    auto p = s_.find(needle ? needle : "");
    return p == std::string::npos ? -1 : (int)p;
  }
  bool startsWith(const char *prefix) const {
    if (!prefix) return false;
    auto n = strlen(prefix);
    return s_.size() >= n && s_.compare(0, n, prefix) == 0;
  }
  bool endsWith(const char *suffix) const {
    if (!suffix) return false;
    auto n = strlen(suffix);
    return s_.size() >= n && s_.compare(s_.size() - n, n, suffix) == 0;
  }
  String substring(size_t begin) const { return String(s_.substr(begin)); }
  String substring(size_t begin, size_t end) const {
    return String(s_.substr(begin, end - begin));
  }
  void  trim() {
    auto a = s_.find_first_not_of(" \t\r\n");
    auto b = s_.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) { s_.clear(); return; }
    s_ = s_.substr(a, b - a + 1);
  }
  void  replace(const String &from, const String &to) {
    if (from.s_.empty()) return;
    size_t p = 0;
    while ((p = s_.find(from.s_, p)) != std::string::npos) {
      s_.replace(p, from.s_.size(), to.s_);
      p += to.s_.size();
    }
  }
  void  replace(const char *from, const char *to) {
    if (!from) return;
    size_t p = 0;
    while ((p = s_.find(from, p)) != std::string::npos) {
      s_.replace(p, strlen(from), to ? to : "");
      p += strlen(to ? to : "");
    }
  }
  float toFloat() const { return s_.empty() ? 0.0f : (float)atof(s_.c_str()); }
  long  toInt() const   { return s_.empty() ? 0    : atol(s_.c_str()); }
  int   charAt(size_t i) const { return (int)(unsigned char)s_[i]; }
  void  remove(size_t i, size_t n) { s_.erase(i, n); }
  char  charAtUnsafe(size_t i) const { return s_[i]; }
  String &operator=(const char *s) { s_ = s ? s : ""; return *this; }
  String &operator=(const std::string &s) { s_ = s; return *this; }
  size_t printTo(char *buf, size_t bufsize) const {
    size_t n = std::min(s_.size(), bufsize - 1);
    memcpy(buf, s_.c_str(), n);
    buf[n] = '\0';
    return n;
  }
  const std::string &std_str() const { return s_; }

 private:
  std::string s_;
};

// Stream IO shims used by Serial.printf etc.
inline size_t printTo(const String &s, char *buf, size_t bufsize) {
  return s.printTo(buf, bufsize);
}
