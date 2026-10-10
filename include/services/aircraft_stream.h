#pragma once

#include <cstddef>

namespace services::adsb {

/**
 * One byte of push-back over any reader with `int read()`.
 *
 * The feed's `ac` array is parsed one aircraft at a time (see below), which
 * means peeking at the byte between elements and handing a '{' back so the
 * JSON parser sees a whole object. read()/readBytes() match the shape
 * ArduinoJson expects of a custom input.
 */
template <typename Inner>
class PushbackReader {
 public:
  explicit PushbackReader(Inner& inner) : inner_(&inner) {}

  int read() {
    if (has_held_) {
      has_held_ = false;
      return held_;
    }
    return inner_->read();
  }

  size_t readBytes(char* out, size_t length) {
    size_t n = 0;
    while (n < length) {
      const int c = read();
      if (c < 0) {
        break;
      }
      out[n++] = static_cast<char>(c);
    }
    return n;
  }

  void unread(int c) {
    held_ = c;
    has_held_ = true;
  }

 private:
  Inner* inner_;
  int held_ = -1;
  bool has_held_ = false;
};

inline bool isJsonSpace(int c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

template <typename Reader>
int readSkippingSpace(Reader& in) {
  int c = in.read();
  while (isJsonSpace(c)) {
    c = in.read();
  }
  return c;
}

/**
 * Consume input up to and including the '[' that opens the `"ac"` array.
 *
 * Parsing the array element by element keeps RAM flat no matter how many
 * aircraft come back -- a 200 km query over a busy region is several hundred
 * objects, far too many to hold as one JSON document on this chip.
 *
 * False when the stream ends first (an error body has no such array).
 */
template <typename Reader>
bool seekAircraftArray(Reader& in) {
  static const char kKey[] = "\"ac\"";
  size_t matched = 0;
  for (;;) {
    const int c = in.read();
    if (c < 0) {
      return false;
    }
    if (c == kKey[matched]) {
      if (++matched == 4) {
        break;
      }
    } else {
      matched = (c == '"') ? 1 : 0;
    }
  }
  if (readSkippingSpace(in) != ':') {
    return false;
  }
  return readSkippingSpace(in) == '[';
}

enum class ArrayStep {
  kElement,    // an object follows; its '{' has been pushed back
  kEnd,        // the closing ']' was consumed
  kMalformed,  // stream ended or held something unexpected
};

/** Step over separators to the next array element. */
template <typename Inner>
ArrayStep nextArrayElement(PushbackReader<Inner>& in) {
  int c = readSkippingSpace(in);
  if (c == ',') {
    c = readSkippingSpace(in);
  }
  if (c == ']') {
    return ArrayStep::kEnd;
  }
  if (c == '{') {
    in.unread(c);
    return ArrayStep::kElement;
  }
  return ArrayStep::kMalformed;
}

}  // namespace services::adsb
