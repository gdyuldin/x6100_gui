#include "morse_decoder.h"

#include <utility>

namespace cw {

MorseDecoder::MorseDecoder(EmitTextFn emit) : emit_(std::move(emit)) {
}

void MorseDecoder::append(char c) {
    if (last_char == ' ' && c == ' ') {
        return;
    }
    last_char = c;
    text_buffer_.push_back(c);
}

void MorseDecoder::append(const std::string_view &s) {
    text_buffer_ += s;
}

void MorseDecoder::flush() {
    if (text_buffer_.length() == 0)
        return;
    if (emit_)
        emit_(text_buffer_.data());
    text_buffer_.clear();
}

bool MorseDecoder::check_and_handle_exceptions()  {
    std::string_view current_input(raw_buffer_.data(), buffer_len_);

    for (const auto& ex : exceptions_) {
        if (current_input == ex.morse_str) {
            append(ex.text_out);
            return true; // Exception processed
        }
    }
    return false; // No exception in vocab
}

void MorseDecoder::handle_token(Token token) {
    switch (token) {
        case CW_DOT:
            if (buffer_len_ < raw_buffer_.size()) raw_buffer_[buffer_len_++] = '.';
            // Descend left. Tree bound guard (max reachable index 126).
            if (tree_index_ < 63) {
                tree_index_ = tree_index_ * 2 + 1;
            }
            break;

        case CW_DASH:
            if (buffer_len_ < raw_buffer_.size()) raw_buffer_[buffer_len_++] = '-';
            // Descend right.
            if (tree_index_ < 63) {
                tree_index_ = tree_index_ * 2 + 2;
            }
            break;

        case CW_LETTER_SPACE:
            if (buffer_len_ > 0) {
                if (!check_and_handle_exceptions()) {
                    if (tree_index_ > 0 && tree_index_ < morse_tree.size()) {
                        char c = morse_tree[tree_index_];
                        if (c == ' ') {
                            append(ERR);
                        } else {
                            append(morse_tree[tree_index_]);
                        }
                    } else {
                        append(ERR);
                    }
                }
            }
            tree_index_ = 0; // Reset to accumulate the next character
            buffer_len_ = 0;
            break;

        case CW_WORD_SPACE:
            // Word pause. First flush a pending letter (if any).
            if (buffer_len_ > 0) {
                if (!check_and_handle_exceptions()) {
                    if (tree_index_ > 0 && tree_index_ < morse_tree.size()) {
                        char c = morse_tree[tree_index_];
                        if (c == ' ') {
                            append(ERR);
                        } else {
                            append(morse_tree[tree_index_]);
                        }
                    }
                }
            }
            append(' '); // Print the inter-word space
            tree_index_ = 0;
            buffer_len_ = 0;
            break;

        case CW_ELEMENT_SPACE:
        case CW_NONE:
        default:
            // Intra-character pauses and empty tokens are ignored by the tree
            break;
    }
    flush();
}

void MorseDecoder::reset() {
    tree_index_ = 0;
    buffer_len_ = 0;
    text_buffer_.clear();
}

} // namespace cw
