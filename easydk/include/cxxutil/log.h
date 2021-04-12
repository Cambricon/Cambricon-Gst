/*************************************************************************
* Copyright (C) 2019 by Cambricon, Inc. All rights reserved
*
* This source code is licensed under the Apache-2.0 license found in the
* LICENSE file in the root directory of this source tree.
*
* A part of this source code is referenced from glog project.
* https://github.com/google/glog/blob/master/src/logging.cc
*
* Copyright (c) 1999, Google Inc.
*
* This source code is licensed under the BSD 3-Clause license found in the
* LICENSE file in the root directory of this source tree.
*
*************************************************************************/

#ifndef EDK_CXXUTIL_LOG_H_
#define EDK_CXXUTIL_LOG_H_

#include <string>
#include <streambuf>
#include <ostream>

/*
 * Log filter.
 *
 * Environment variable: EDK_LOG_FILTER
 * Default: ""
 *
 * Usage:
 *  export EDK_LOG_FILTER=BANG:2,DEVICE:3 ...
 */

/*
 * Min category log level
 *
 * Environment variable: EDK_LOG_LEVEL
 * Default: 2 (LOG_WARNING)
 */

#define STRINGIFY(src) #src

#define _EDK_LOG_IS_ON(cate, sev) edk::log::LogActivated(STRINGIFY(cate), sev)
#define _EDK_LOG_STREAM(cate, sev) edk::log::LogMessage(STRINGIFY(cate), __FILE__, __LINE__, sev).stream()

#define _EDK_LOG(cate, sev) \
  !(_EDK_LOG_IS_ON(cate, sev)) ? (void)0 : edk::log::LogMessageVoidify() & _EDK_LOG_STREAM(cate, sev)
#define _EDK_LOG_IF(cate, sev, condition) \
  !(_EDK_LOG_IS_ON(cate, sev) && (condition)) ? (void)0 : edk::log::LogMessageVoidify() & _EDK_LOG_STREAM(cate, sev)

#define _EDK_LOG_CONCAT(lhs, rhs) lhs##_##rhs
#define _EDK_LOG_COUNTER_NAME(name, line) _EDK_LOG_CONCAT(name, line)
#define _EDK_LOG_COUNTER _EDK_LOG_COUNTER_NAME(_log_counter, __LINE__)

#define LOG_EVERY_N(category, severity, N) \
  static int _EDK_LOG_COUNTER = 0; \
  ++_EDK_LOG_COUNTER > N ? _EDK_LOG_COUNTER -= N : 0; \
  _EDK_LOG_IF(category, edk::log::severity, (_EDK_LOG_COUNTER == 1))

#define LOG_FIRST_N(category, severity, N) \
  static int _EDK_LOG_COUNTER = 0; \
  ++_EDK_LOG_COUNTER > N + 1 ? --_EDK_LOG_COUNTER : 0; \
  _EDK_LOG_IF(category, edk::log::severity, (_EDK_LOG_COUNTER <= N))

// LOGF is on regardless of severity
#define LOGF(category) _EDK_LOG_STREAM(category, edk::log::LOG_FATAL)
#define LOGF_IF(category, condition) \
  !(condition) ? (void)0 : edk::log::LogMessageVoidify() & _EDK_LOG_STREAM(category, edk::log::LOG_FATAL)

#define LOGE(category) _EDK_LOG(category, edk::log::LOG_ERROR)
#define LOGE_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_ERROR, condition)

#define LOGW(category) _EDK_LOG(category, edk::log::LOG_WARNING)
#define LOGW_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_WARNING, condition)

#define LOGI(category) _EDK_LOG(category, edk::log::LOG_INFO)
#define LOGI_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_INFO, condition)

#define LOGD(category) _EDK_LOG(category, edk::log::LOG_DEBUG)
#define LOGD_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_DEBUG, condition)

#define LOGT(category) _EDK_LOG(category, edk::log::LOG_TRACE)
#define LOGT_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_TRACE, condition)

#define LOGA(category) _EDK_LOG(category, edk::log::LOG_ALL)
#define LOGA_IF(category, condition) _EDK_LOG_IF(category, edk::log::LOG_ALL, condition)

#define CHECK(category, condition) \
  LOGF_IF((category), !(condition)) << "Check condition (" << STRINGIFY(condition) ") failed"

namespace edk {
namespace log {

/**
 * @brief log severity
 * 0, FATAL
 * 1, LOG_ERROR
 * 2, WARNING
 * 3, INFO
 * 4, DEBUG
 * 5, TRACE
 * 6, ALL
 */
enum LogSeverity {
  LOG_FATAL = 0,
  LOG_ERROR,
  LOG_WARNING,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE,
  LOG_ALL
};

class LogSink {
 public:
  virtual ~LogSink() { }
  virtual void Send(LogSeverity severity, const char* category,
                    const char* filename, int line,
                    const struct ::tm* tm_time, int32_t usecs,
                    const char* message, size_t message_len) = 0;

  virtual void WaitTillSent() { }  // noop default
  static std::string ToString(LogSeverity severity, const char* category,
                              const char* filename, int line,
                              const struct ::tm* tm_time, int32_t usecs,
                              const char* message, size_t message_len);
};  // class LogSink

class LogMessageVoidify {
 public:
  LogMessageVoidify() { }
  // This has to be an operator with a precedence lower than << but
  // higher than ?:
  void operator&(std::ostream&) { }
};

class LogMessage {
 public:
  class LogStreamBuf : public std::streambuf {
   public:
    // REQUIREMENTS: "len" must be >= 2 to account for the '\n' and '\0'.
    LogStreamBuf(char *buf, int len) {
      setp(buf, buf + len - 2);
    }

    // This effectively ignores overflow.
    virtual int_type overflow(int_type ch) {
      return ch;
    }

    // Legacy public ostrstream method.
    size_t pcount() const { return pptr() - pbase(); }
    char* pbase() const { return std::streambuf::pbase(); }
  };  // class LogStreamBuf

  class LogStream : public std::ostream {
   public:
    LogStream(char *buf, int len)
        : std::ostream(NULL),
          streambuf_(buf, len) {
      rdbuf(&streambuf_);
    }

    // Legacy std::streambuf methods.
    size_t pcount() const { return streambuf_.pcount(); }
    char* pbase() const { return streambuf_.pbase(); }
    char* str() const { return pbase(); }

   private:
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStreamBuf streambuf_;
  };  // class LogStream

  LogMessage(const char* category, const char* file, int line, LogSeverity severity);
  ~LogMessage();
  void Init(const char* category, const char* file, int line, LogSeverity severity);
  std::ostream& stream();
  struct LogMessageData;

 private:
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
  void Flush();
  void SendToLog();
  LogMessageData* data_;
  LogMessageData* allocated_;
  static const size_t MaxLogMsgLen;
};  // class LogMessage

namespace detail {
bool CategoryActivated(const char* category, LogSeverity severity) noexcept;
}  // namespace detail

extern const int g_min_log_level;
extern const bool g_enable_category_filter;
inline bool LogActivated(const char* category, LogSeverity severity) noexcept {
  if (g_enable_category_filter) {
    return detail::CategoryActivated(category, severity);
  }
  return g_min_log_level >= severity;
}

/**
 * @brief Init log system
 *
 * @note Only log to stderr as default if not init
 *
 * @param log_to_stderr Log messages go to stderr
 * @param log_to_file Log messages go to log file
 * @param log_dir Directory to store log file
 */
void InitLogging(bool log_to_stderr, bool log_to_file, const std::string& log_dir = "") noexcept;

/**
 * @brief Shutdown log output
 */
void ShutdownLogging() noexcept;

/**
 * @brief Flush log file interval, in second
 *
 * @note Using 30s as default if not set
 *
 * @param time Flush interval
 */
void SetFileFlushInterval(uint32_t time) noexcept;

/**
 * @brief Add customized log sink to logger
 *
 * @param log_sink Customized log sink
 */
void AddLogSink(LogSink* log_sink) noexcept;

/**
 * @brief Remove customized log sink
 *
 * @param log_sink Customized log sink
 */
void RemoveLogSink(LogSink* log_sink) noexcept;

}  // namespace log
}  // namespace edk

#endif  // EDK_CXXUTIL_LOG_H_
