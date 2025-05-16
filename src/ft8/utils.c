#include "utils.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

bool is_prefixed_callsign(const char *callsign) {
  const char *slash = strchr(callsign, '/');
  if (!slash || slash == callsign) {
    return false;
  }

  size_t prefix_len = slash - callsign;
  if (prefix_len < 1 || prefix_len > 3) {
    return false;
  }

  // Check pattern: 1–2 letters, optionally followed by 1 digit
  if (prefix_len == 1) {
    return isalpha(callsign[0]);
  } else if (prefix_len == 2) {
    return isalpha(callsign[0]) && isalpha(callsign[1]);
  } else if (prefix_len == 3) {
    return isalpha(callsign[0]) && isalpha(callsign[1]) && isdigit(callsign[2]);
  }

  return false;
}

/**
 * Check that text is CQ modifier (3 digits or 1 to 4 letters)
 */
bool is_cq_modifier(const char *text) {
  size_t len = strlen(text);
  char c;

  // Check for 3 digits
  bool correct = true;
  if (len == 3) {
    // Check for 3 digits
    for (size_t i = 0; i < len; i++) {
      if ((text[i] < '0') || (text[i] > '9')) {
        correct = false;
        break;
      }
    }
    if (correct) {
      return true;
    }
  }
  // Check for 1 to 4 letters
  correct = true;
  if ((len >= 1) && (len <= 4)) {
    for (size_t i = 0; i < len; i++) {
      c = toupper(text[i]);
      if ((c < 'A') || (c > 'Z')) {
        correct = false;
        break;
      }
    }
    if (correct) {
      return true;
    }
  }
  return false;
}
