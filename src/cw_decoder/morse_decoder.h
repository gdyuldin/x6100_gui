#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string_view>
#include <string>

#include "cw_config.h"

namespace cw {

// Morse binary-tree state machine. Consumes timing Tokens and emits decoded
// text through EmitTextFn. Accumulated text is flushed through a small internal
// buffer, so a future C caller can pass e.g. `void panel_add_text(const char*)`
// as the callback.
class MorseDecoder {
  public:
    using EmitTextFn = std::function<void(const char *)>;

    explicit MorseDecoder(EmitTextFn emit);

    void handle_token(Token token);
    void reset();

  private:
    static constexpr std::array<char, 63> morse_tree = {
        ' ', // 0: tree root (start)

        // Level 1 (1-element codes)
        'E', 'T',

        // Level 2 (2 elements)
        'I', 'A', 'N', 'M',

        // Level 3 (3 elements)
        'S', 'U', 'R', 'W', 'D', 'K', 'G', 'O',

        // Level 4 (4 elements)
        'H', 'V', 'F', ' ', 'L', ' ', 'P', 'J', 'B', 'X', 'C', 'Y', 'Z', 'Q', ' ', ' ',

        // Level 5 (5 elements: digits and basic signs)
        '5', '4', ' ', '3', ' ', ' ', ' ', '2', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '1', '6', '=', '/', ' ', ' ', ' ',
        ' ', ' ', '7', ' ', ' ', ' ', '8', ' ', '9', '0'};

    void append(char c);
    void append(const std::string_view &s);
    void flush();

    bool check_and_handle_exceptions();

    EmitTextFn  emit_;
    size_t      tree_index_ = 0;
    std::string text_buffer_{};

    // Buffer for raw elements
    std::array<char, 12> raw_buffer_{};
    size_t buffer_len_ = 0;

    // Last char to avoid spaces
    char last_char = 'A';

    // Exceptions struct
    struct ExceptionPair {
        std::string_view morse_str; // Morse elements
        std::string_view text_out;
    };

    static constexpr std::string_view ERR = "<ERR>";

    static constexpr std::array<ExceptionPair, 26> exceptions_ = {{
        {"--..--", ","},
        {"..--..", "?"},
        {"-.-.--", "!"},
        {"..--.", "!"},
        {"..--.-", "_"},
        {".-..-.", "\""},
        {".--.-.", "@"},
        {".----.", "'"},
        {".-.-.-", "."},
        {"-....-", "-"},
        {"---...", ":"},
        {"-.-.-.", ";"},
        {"...-..-", "$"},
        {"...-.-", "<SK>"},
        {"...-.", "<SN>"},
        {"-.--.", "<KN>"},
        {"-.-.-", "<CT>"},
        {".-.-.", "<AR>"},
        {".-...", "<AS>"},
        {"-.-.--.-", "<CQ>"},
        {"-...-.-", "<BK>"},
        {"-.-..-..", "<CL>"},
        {"...---...", "<SOS>"},
        {"......", ERR},
        {".......", ERR},
        {"........", ERR}
    }};

};

} // namespace cw
