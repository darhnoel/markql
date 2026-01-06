#include "xsql/plugin_api.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "khmer_segmenter.h"

namespace {

struct KhmerPluginState {
  KhmerSegmenter* segmenter = nullptr;
  std::string dict_path;
  std::string freq_path;
};

const char* env_or_default(const char* env_name, const char* fallback) {
  if (const char* value = std::getenv(env_name)) {
    if (*value) {
      return value;
    }
  }
  return fallback;
}

const char* default_dict_path() {
#if defined(XSQL_KHMER_PLUGIN_SOURCE)
  return XSQL_KHMER_PLUGIN_SOURCE "/port/common/khmer_dictionary_words.txt";
#else
  return "plugins/khmer_segmenter/port/common/khmer_dictionary_words.txt";
#endif
}

const char* default_freq_path() {
#if defined(XSQL_KHMER_PLUGIN_SOURCE)
  return XSQL_KHMER_PLUGIN_SOURCE "/port/common/khmer_word_frequencies.bin";
#else
  return "plugins/khmer_segmenter/port/common/khmer_word_frequencies.bin";
#endif
}

bool ensure_segmenter(KhmerPluginState& state, std::string& error) {
  if (state.segmenter) {
    return true;
  }
  const char* dict_path = env_or_default("XSQL_KHMER_DICT", default_dict_path());
  const char* freq_path = env_or_default("XSQL_KHMER_FREQ", default_freq_path());
  state.segmenter = khmer_segmenter_init(dict_path, freq_path);
  if (!state.segmenter) {
    error = "Failed to initialize khmer_segmenter (check dictionary paths).";
    return false;
  }
  state.dict_path = dict_path;
  state.freq_path = freq_path ? freq_path : "";
  return true;
}

bool tokenize_khmer(const char* text,
                    void* user_data,
                    char* out_tokens,
                    size_t out_tokens_size,
                    char* out_error,
                    size_t out_error_size) {
  if (!text || !out_tokens || out_tokens_size == 0) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Invalid tokenizer buffer.");
    }
    return false;
  }
  auto* state = static_cast<KhmerPluginState*>(user_data);
  if (!state) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Tokenizer state missing.");
    }
    return false;
  }
  std::string error;
  if (!ensure_segmenter(*state, error)) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "%s", error.c_str());
    }
    return false;
  }
  char* segmented = khmer_segmenter_segment(state->segmenter, text, "\n");
  if (!segmented) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Segmentation failed.");
    }
    return false;
  }
  size_t len = std::strlen(segmented);
  if (len + 1 > out_tokens_size) {
    std::free(segmented);
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Tokenizer output buffer too small.");
    }
    return false;
  }
  std::memcpy(out_tokens, segmented, len + 1);
  std::free(segmented);
  return true;
}

}  // namespace

extern "C" bool xsql_register_plugin(const XsqlPluginHost* host,
                                      char* out_error,
                                      size_t out_error_size) {
  if (!host || host->api_version != XSQL_PLUGIN_API_VERSION) {
    if (out_error && out_error_size > 0) {
      std::snprintf(out_error, out_error_size, "Unsupported plugin API version.");
    }
    return false;
  }
  static KhmerPluginState state;
  if (!host->register_tokenizer(host->host_context,
                                "khmer",
                                &tokenize_khmer,
                                &state,
                                out_error,
                                out_error_size)) {
    return false;
  }
  return true;
}
