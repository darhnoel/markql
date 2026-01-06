#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XSQL_PLUGIN_API_VERSION 1

typedef bool (*XsqlPluginCommandFn)(const char* line,
                                    void* user_data,
                                    char* out_error,
                                    size_t out_error_size);

typedef bool (*XsqlTokenizerFn)(const char* text,
                                void* user_data,
                                char* out_tokens,
                                size_t out_tokens_size,
                                char* out_error,
                                size_t out_error_size);

typedef struct XsqlPluginHost {
  uint32_t api_version;
  void* host_context;
  bool (*register_command)(void* host_context,
                           const char* name,
                           const char* help,
                           XsqlPluginCommandFn fn,
                           void* user_data,
                           char* out_error,
                           size_t out_error_size);
  bool (*register_tokenizer)(void* host_context,
                             const char* lang,
                             XsqlTokenizerFn fn,
                             void* user_data,
                             char* out_error,
                             size_t out_error_size);
  void (*print)(void* host_context, const char* message, bool is_error);
} XsqlPluginHost;

typedef bool (*XsqlRegisterPluginFn)(const XsqlPluginHost* host,
                                     char* out_error,
                                     size_t out_error_size);

#ifdef __cplusplus
}  // extern "C"
#endif
