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

#if defined(linux) || defined(__linux) || defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "backward.hpp"
#include "cxxutil/edk_attribute.h"
#include "cxxutil/log.h"
#include "cxxutil/noncopy.h"
#include "cxxutil/rwlock.h"
#include "cxxutil/threadsafe_queue.h"

#define EnvToString(envname, dflt)   \
  (!getenv(envname) ? (dflt) : getenv(envname))

#define EnvToInt(envname, dflt)  \
  (!getenv(envname) ? (dflt) : strtol(getenv(envname), NULL, 10))

#define EnvToBool(envname, dflt)   \
  (!getenv(envname) ? (dflt) : memchr("tTyY1\0", getenv(envname)[0], 6) != NULL)

static bool g_init_logging = false;
static bool g_log_to_stderr = true;
static bool g_log_to_file = false;
static uint32_t g_flush_interval = 30;

static bool g_handle_signals = EnvToBool("EDK_HANDLE_SIGNALS", false);
static std::string g_log_filter = EnvToString("EDK_LOG_FILTER", "");
// A simple helper class that registers for you the most common signals
// and other callbacks to segfault, hardware exception, un-handled exception etc.
static std::unique_ptr<backward::SignalHandling> sh{g_handle_signals ? (new backward::SignalHandling) : nullptr};

static inline bool IsInitLogging() {
  return g_init_logging;
}

// Based on: https://github.com/google/glog/blob/master/src/utilities.cc
static pid_t GetTID() {
  // On Linux we try to use gettid().
#if defined(linux) || defined(__linux) || defined(__linux__)
#ifndef __NR_gettid
#if !defined __i386__
#error "Must define __NR_gettid for non-x86 platforms"
#else
#define __NR_gettid 224
#endif
#endif
  static bool lacks_gettid = false;
  if (!lacks_gettid) {
    pid_t tid = syscall(__NR_gettid);
    if (tid != -1) {
      return tid;
    }
    // Technically, this variable has to be volatile, but there is a small
    // performance penalty in accessing volatile variables and there should
    // not be any serious adverse effect if a thread does not immediately see
    // the value change to "true".
    lacks_gettid = true;
  }
#endif  // OS_LINUX

  // If gettid() could not be used, we use one of the following.
#if defined(linux) || defined(__linux) || defined(__linux__)
  return getpid();  // Linux:  getpid returns thread ID when gettid is absent
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  return GetCurrentThreadId();
#else
  // If none of the techniques above worked, we use pthread_self().
  return (pid_t)(uintptr_t)pthread_self();
#endif
}

/**
 * @brief Remove all spaces in the string
 */
static std::string StringTrim(const std::string& str) {
  std::string::size_type index = 0;
  std::string result = str;

  while ((index = result.find(' ', index)) != std::string::npos) {
    result.erase(index, 1);
  }
  return result;
}

static double GetTimeStamp() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  double now = (static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000) * 0.000001;
  return now;
}

// cycle clock is retuning microseconds since the epoch.
static size_t CycleClock_Now() {
  return static_cast<size_t>(GetTimeStamp() * 1000000);
}

static const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  if (!base)
    base = strrchr(filepath, '\\');
#endif
  return base ? ( base + 1) : filepath;
}

namespace edk {
namespace log {

const int g_min_log_level = EnvToInt("EDK_LOG_LEVEL", 2);
const bool g_enable_category_filter = getenv("EDK_LOG_FILTER");
using CategoryFilterMaps = std::unordered_map<std::string, LogSeverity>;
static std::unique_ptr<CategoryFilterMaps> CreateFilterMaps();
static std::unique_ptr<CategoryFilterMaps> g_filter_maps = CreateFilterMaps();

namespace detail {
bool CategoryActivated(const char* category, LogSeverity severity) noexcept {
  if (EDK_UNLIKELY(g_enable_category_filter && g_filter_maps)) {
    auto it = g_filter_maps->find(std::string(category));
    return it != g_filter_maps->end() && it->second >= severity;
  }
  return g_min_log_level >= severity;
}
}  // namespace detail

constexpr int NUM_SEVERITIES = 7;
const char* const LogSeverityNames[NUM_SEVERITIES] = {
  "FATAL", "ERROR", "WARNING", "INFO", "DEBUG", "TRACE", "ALL"
};

enum LogColor {
  COLOR_DEFAULT,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_YELLOW
};

static LogColor SeverityToColor(LogSeverity severity) {
  assert(severity >= 0 && severity < NUM_SEVERITIES);
  LogColor color = COLOR_DEFAULT;
  switch (severity) {
  case LOG_INFO:
  case LOG_DEBUG:
  case LOG_TRACE:
  case LOG_ALL:
    color = COLOR_DEFAULT;
    break;
  case LOG_WARNING:
    color = COLOR_YELLOW;
    break;
  case LOG_ERROR:
  case LOG_FATAL:
    color = COLOR_RED;
    break;
  default:
    // should never get here.
    assert(false);
  }
  return color;
}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
// Returns the character attribute for the given color.
static WORD GetColorAttribute(LogColor color) {
  switch (color) {
    case COLOR_RED:    return FOREGROUND_RED;
    case COLOR_GREEN:  return FOREGROUND_GREEN;
    case COLOR_YELLOW: return FOREGROUND_RED | FOREGROUND_GREEN;
    default:           return 0;
  }
}
#else
// Returns the ANSI color code for the given color.
static const char* GetAnsiColorCode(LogColor color) {
  switch (color) {
    case COLOR_RED:     return "1";
    case COLOR_GREEN:   return "2";
    case COLOR_YELLOW:  return "3";
    case COLOR_DEFAULT:  return "";
  }
  return NULL;  // stop warning about return type.
}
#endif  // OS_WINDOWS

static void ColoredWriteToStderr(LogSeverity severity,
                                 const char* message, size_t len) {
  const LogColor color = SeverityToColor(severity);

  // Avoid using cerr from this module since we may get called during
  // exit code, and cerr may be partially or fully destroyed by then.
  if (EDK_LIKELY(COLOR_DEFAULT == color)) {
    fwrite(message, len, 1, stderr);
    return;
  }
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  const HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);

  // Gets the current text color.
  CONSOLE_SCREEN_BUFFER_INFO buffer_info;
  GetConsoleScreenBufferInfo(stderr_handle, &buffer_info);
  const WORD old_color_attrs = buffer_info.wAttributes;

  // We need to flush the stream buffers into the console before each
  // SetConsoleTextAttribute call lest it affect the text that is already
  // printed but has not yet reached the console.
  fflush(stderr);
  SetConsoleTextAttribute(stderr_handle,
                          GetColorAttribute(color) | FOREGROUND_INTENSITY);
  fwrite(message, len, 1, stderr);
  fflush(stderr);
  // Restores the text color.
  SetConsoleTextAttribute(stderr_handle, old_color_attrs);
#else
  fprintf(stderr, "\033[0;3%sm", GetAnsiColorCode(color));
  fwrite(message, len, 1, stderr);
  fprintf(stderr, "\033[m");  // Resets the terminal to default.
#endif  // OS_WINDOWS
}

static void WriteToStderr(const char* message, size_t len) {
  // Avoid using cerr from this module since we may get called during
  // exit code, and cerr may be partially or fully destroyed by then.
  fwrite(message, len, 1, stderr);
}

static std::unique_ptr<CategoryFilterMaps> CreateFilterMaps() {
  std::string filter_str = StringTrim(g_log_filter);
  if (filter_str.empty()) {
    return nullptr;
  }

  std::unique_ptr<CategoryFilterMaps> maps(new CategoryFilterMaps);

  const char* category = filter_str.c_str();
  const char* sep;
  while ((sep = strchr(category, ':')) != NULL) {
    std::string pattern(category, sep - category);
    std::transform(pattern.begin(), pattern.end(), pattern.begin(),
        [](unsigned char c) { return std::toupper(c); });
    int category_level = g_min_log_level;
    if (sscanf(sep, ":%d", &category_level) != 1) {
      fprintf(stderr, "Parse %s log level failed, will set to %d\n", pattern.c_str(), category_level);
    }
    maps->insert(std::make_pair(pattern, LogSeverity(category_level)));
    // Skip past this entry
    category = strchr(sep, ',');
    if (category == nullptr) break;
    category++;  // Skip past ","
  }
  return maps;
}

std::string LogSink::ToString(LogSeverity severity, const char* category,
                              const char* filename, int line,
                              const struct ::tm* tm_time, int32_t usecs,
                              const char* message, size_t message_len) {
  std::ostringstream stream;
  stream.fill('0');
  stream << "EasyDK " << category
         << ' '
         << LogSeverityNames[severity][0]
         << std::setw(4) << 1900 + tm_time->tm_year
         << std::setw(2) << 1 + tm_time->tm_mon
         << std::setw(2) << tm_time->tm_mday
         << ' '
         << std::setw(2) << tm_time->tm_hour << ':'
         << std::setw(2) << tm_time->tm_min << ':'
         << std::setw(2) << tm_time->tm_sec << '.'
         << std::setw(6) << usecs
         << ' '
         << std::setfill(' ') << std::setw(5)
         << static_cast<unsigned int>(GetTID())
         << ' '
         << filename << ':' << line
         << "] ";
  stream << std::string(message, message_len);
  return stream.str();
}

const size_t LogMessage::MaxLogMsgLen = 1024;

struct LogMessage::LogMessageData {
  LogMessageData();
  // Buffer space; contains complete message text.
  char message_buf_[LogMessage::MaxLogMsgLen + 1];
  LogStream stream_;
  LogSeverity severity_;      // What level is this LogMessage logged at?
  int line_;                 // line number where logging call is.
  time_t timestamp_;            // Time of creation of LogMessage
  struct ::tm tm_time_;         // Time of creation of LogMessage
  int32_t usecs_;                   // Time of creation of LogMessage - microseconds part
  size_t num_prefix_chars_;     // # of chars of prefix in this message
  size_t num_chars_to_log_;     // # of chars of msg to send to log
  const char* filename_;        // basename of file that called LOG
  const char* category_;        // Which category call is.
  bool has_been_flushed_;       // false => data has not been flushed

 private:
  LogMessageData(const LogMessageData&) = delete;
  void operator=(const LogMessageData&) = delete;
};  // struct LogMessageData

class LogFile : public NonCopyable {
 public:
  LogFile(const std::string& file_name, uint64_t max_file_len);
  ~LogFile();
  void Write(const char* msg, int msg_len, bool force_flush);

 private:
  bool CreateLogFile();
  void Flush();
  std::queue<std::string> filepath_queue_;
  std::string file_dir_;
  FILE* file_;
  size_t file_len_;
  size_t max_file_len_;
  void WriteFileLoop();

  ThreadSafeQueue<std::string> msgq_;
  std::thread write_thread_;
  std::atomic<bool> stop_writing_{true};
  std::atomic<bool> force_flush_{false};
  size_t bytes_since_flush_;
  size_t next_flush_time_;

  // for write thread sleep
  size_t sleep_time_ = 30 * 60;  // disk full sleep time in seconds
  std::mutex sleep_mutex_;
  std::condition_variable wake_up_cond_;
  bool thread_exit_ = false;
};  // LogFile

LogFile::LogFile(const std::string& file_dir, size_t max_file_len)
  : file_dir_(file_dir),
  file_(nullptr),
  file_len_(0),
  max_file_len_(max_file_len),
  bytes_since_flush_(0),
  next_flush_time_(0) {
  stop_writing_.store(false);
  force_flush_.store(false);
  write_thread_ = std::thread(&LogFile::WriteFileLoop, this);
}

LogFile::~LogFile() {
  stop_writing_.store(true);
  std::unique_lock<std::mutex> lk(sleep_mutex_);
  thread_exit_ = true;
  lk.unlock();  // ~LogFile() before fwrite disk full, prevent deadlock
  wake_up_cond_.notify_one();
  if (write_thread_.joinable()) {
    write_thread_.join();
  }
}

void LogFile::Write(const char* msg, int msg_len, bool force_flush) {
  if (force_flush) {
    stop_writing_.store(true);
    force_flush_.store(true);
    return;
  }
  if (!stop_writing_.load()) {
    msgq_.Push(std::string(msg, msg_len));
  }
}

bool LogFile::CreateLogFile() {
  std::string file_dir;
  std::string filepath;
  if (!file_dir_.empty()) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    const char& back_char = file_dir_.back();
    file_dir = (back_char == '\\') ? file_dir_ : file_dir_ + "\\";
#else
    const char& back_char = file_dir_.back();
    file_dir = (back_char == '/') ? file_dir_ : file_dir_ + "/";
#endif
  } else {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    file_dir = "C:\\tmp\\";
#else
    file_dir = "/tmp/";
#endif
  }
  double now = GetTimeStamp();
  time_t timestamp = static_cast<time_t>(now);
  struct ::tm tm_time;
  localtime_r(&timestamp, &tm_time);
  int32_t usecs = static_cast<int32_t>((now - timestamp) * 1000000);
  std::ostringstream os;
  os.fill('0');
  os << "edk_"
    << 1900 + tm_time.tm_year
    << std::setw(2) << 1 + tm_time.tm_mon
    << std::setw(2) << tm_time.tm_mday
    << '-'
    << std::setw(2) << tm_time.tm_hour
    << std::setw(2) << tm_time.tm_min
    << std::setw(2) << tm_time.tm_sec << "."
    << std::setw(6) << usecs
    << ".log";
  filepath = file_dir + os.str();

  if (file_) {
    fclose(file_);
  }
  file_ = fopen(filepath.c_str(), "w");
  if (!file_) {
    perror("Could not create log file, will not be output to the log file");
    fprintf(stderr, "Could not create log file '%s'!\n", filepath.c_str());
    return false;
  }

  // update flush log time and bytes
  bytes_since_flush_ = 0;
  next_flush_time_ = CycleClock_Now()
    + g_flush_interval * static_cast<size_t>(1000000);  // in usec

  // clean up for old logs
  if (filepath_queue_.size() >= 10) {
    unlink(filepath_queue_.front().c_str());
    filepath_queue_.pop();
  }
  filepath_queue_.push(filepath);

  // create link file
  std::string linkpath = file_dir + "edk.log";
  unlink(linkpath.c_str());  // delete old one if it exists
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
  // Create link file on windows? However, no way has been found yet
#else
  if (symlink(strrchr(filepath.c_str(), '/') + 1, linkpath.c_str()) != 0) {
    perror("Could not create link file");
    fprintf(stderr, "Could not create link file '%s'!\n", linkpath.c_str());
  }
#endif
  return true;
}

void LogFile::WriteFileLoop() {
  if (!CreateLogFile()) {
    stop_writing_.store(true);
    return;
  }
  std::string msg;
  while (!stop_writing_.load()) {
    if (msgq_.WaitAndTryPop(msg, std::chrono::microseconds(200))) {
      if (EDK_UNLIKELY(file_len_ > max_file_len_)) {
        if (!CreateLogFile()) {
          stop_writing_.store(true);
          return;
        }
        file_len_ = 0;
      }
      fwrite(msg.data(), 1, msg.size(), file_);
      if (EDK_UNLIKELY(errno == ENOSPC)) {  // disk full
        perror("Disk is full, log stop output to the log file");
        fprintf(stderr, "Disk is full, log stop output to the log file until %ld seconds!\n", sleep_time_);
        stop_writing_.store(true);  // disk full, stop writing to disk, until wake up
        std::unique_lock<std::mutex> lk(sleep_mutex_);
        wake_up_cond_.wait_for(lk, std::chrono::seconds(sleep_time_), [this]() { return thread_exit_; });
        if (!thread_exit_) {  // wake up by timeout, not notify
          stop_writing_.store(false);
          continue;
        }
      } else {
        file_len_ += msg.size();
        bytes_since_flush_ += msg.size();
      }
    }
    // flush logs at least every 10^6 chars,
    // or every "g_flush_interval" seconds.
    if (bytes_since_flush_ >= 1000000 || CycleClock_Now() >= next_flush_time_) {
      Flush();
    }
  }
  // flush msgq_
  while (!force_flush_.load() && msgq_.WaitAndTryPop(msg, std::chrono::microseconds(100))) {
    if (EDK_UNLIKELY(file_len_ > max_file_len_)) {
      if (!CreateLogFile()) {
        return;
      }
      file_len_ = 0;
    }
    fwrite(msg.data(), 1, msg.size(), file_);
    if (EDK_UNLIKELY(errno == ENOSPC)) {  // disk full
      perror("Disk is full, log stop output to the log file");
      fprintf(stderr, "Disk is full, log stop output to the log file!\n");
      fclose(file_);
      return;
    }
    file_len_ += msg.size();
  }
  fflush(file_);
  fclose(file_);
}

void LogFile::Flush() {
  if (file_ != nullptr) {
    fflush(file_);
    bytes_since_flush_ = 0;
  }
  next_flush_time_ = CycleClock_Now()
    + g_flush_interval * static_cast<size_t>(1000000);  // in usec
}
// end LogFile

// LogDestination
class LogDestination : public NonCopyable {
 public:
  static void CreateLogDestination(const std::string& file_name);
  static void DeleteLogDestination();
  static void AddLogSink(LogSink* sink);
  static void RemoveLogSink(LogSink* sink);

  static void LogToStderr(LogSeverity severity, const char* message, size_t message_len);
  static void LogToSinks(LogMessage::LogMessageData* data);
  static void LogToFile(const char* message, size_t message_len, bool force_flush);

 private:
  explicit LogDestination(const std::string& file_name);
  ~LogDestination();
  static LogDestination* GetLogDestination();

  LogFile log_file_;
  static std::vector<LogSink*> sinks_;

  static LogDestination* log_destination_;
  static size_t MAX_FILE_LEN;
  static RwLock sink_lock_;
};  // class LogDestination

std::vector<LogSink*> LogDestination::sinks_;
size_t LogDestination::MAX_FILE_LEN = 1024 * 1024 * 1024;  // FIXME, default log file size 1G.

// static LogDestination pointer, if called CreateLogDestination(), but not called DeleteLogDestination(),
// the LogDestination will not be destroyed, LogFile will not be destroyed too,
// this will cause no fflush log file and part of the log is not written to the file.
// this happens when the process does not end normally, but who cares?
LogDestination* LogDestination::log_destination_ = nullptr;
RwLock LogDestination::sink_lock_;

inline void LogDestination::CreateLogDestination(const std::string& file_name) {
  if (!log_destination_ && g_log_to_file) {
    log_destination_ = new LogDestination(file_name);
  }
}

LogDestination::LogDestination(const std::string& file_name)
  : log_file_(file_name, MAX_FILE_LEN) {
}

LogDestination::~LogDestination() {
}

inline LogDestination* LogDestination::GetLogDestination() {
  return log_destination_;
}

inline void LogDestination::DeleteLogDestination() {
  if (log_destination_) {
    delete log_destination_;
    log_destination_ = nullptr;
  }
}

inline void LogDestination::AddLogSink(LogSink* sink) {
  WriteLockGuard lk(sink_lock_);
  sinks_.push_back(sink);
}

inline void LogDestination::RemoveLogSink(LogSink* sink) {
  WriteLockGuard lk(sink_lock_);
  for (int i = sinks_.size() - 1; i >= 0; i--) {
    if (sinks_[i] == sink) {
      sinks_[i] = sinks_[sinks_.size() - 1];
      sinks_.pop_back();
      break;
    }
  }
}

inline void LogDestination::LogToSinks(LogMessage::LogMessageData* data) {
  ReadLockGuard lk(sink_lock_);
  for (int i = sinks_.size() - 1; i >= 0; i--) {
    sinks_[i]->Send(data->severity_, data->category_,
                    data->filename_, data->line_,
                    &data->tm_time_, data->usecs_,
                    data->message_buf_ + data->num_prefix_chars_,
                    data->num_chars_to_log_ - data->num_prefix_chars_ - 1);
    sinks_[i]->WaitTillSent();
  }
}

inline void LogDestination::LogToStderr(LogSeverity severity, const char* message, size_t message_len) {
  if (g_log_to_stderr) {
    ColoredWriteToStderr(severity, message, message_len);
  }
}

inline void LogDestination::LogToFile(const char* message, size_t message_len, bool force_flush) {
  if (IsInitLogging() && g_log_to_file) {
    GetLogDestination()->log_file_.Write(message, message_len, force_flush);
  }
}
// ------------------------- end LogDestination -----------------------------

static thread_local bool thread_msg_data_available = true;
static thread_local std::aligned_storage<sizeof(LogMessage::LogMessageData),
                         alignof(LogMessage::LogMessageData)>::type thread_msg_data;
static std::mutex before_init_warn_mutex;  // already warned before init mutex

LogMessage::LogMessageData::LogMessageData()
  : stream_(message_buf_, LogMessage::MaxLogMsgLen) {
}

LogMessage::LogMessage(const char* category, const char* file, int line, LogSeverity severity)
  : allocated_(nullptr) {
  Init(category, file, line, severity);
}

LogMessage::~LogMessage() {
  Flush();
  if (EDK_LIKELY(data_ == static_cast<void*>(&thread_msg_data))) {
    data_->~LogMessageData();
    thread_msg_data_available = true;
  } else {
    delete allocated_;
  }
}

void LogMessage::Init(const char* category, const char* file, int line, LogSeverity severity) {
  if (EDK_LIKELY(thread_msg_data_available)) {
    thread_msg_data_available = false;
    data_ = new (&thread_msg_data) LogMessageData();
  } else {
    allocated_ = new LogMessageData();
    data_ = allocated_;
  }

  stream().fill('0');
  data_->severity_ = severity;
  data_->line_ = line;
  double now = GetTimeStamp();
  data_->timestamp_ = static_cast<time_t>(now);
  localtime_r(&data_->timestamp_, &data_->tm_time_);
  data_->usecs_ = static_cast<int32_t>((now - data_->timestamp_) * 1000000);

  data_->num_chars_to_log_ = 0;
  data_->filename_ = const_basename(file);
  data_->category_ = category;
  data_->has_been_flushed_ = false;

  stream() << "EasyDK " << data_->category_
           << ' '
           << LogSeverityNames[severity][0]
           // << setw(4) << 1900+data_->tm_time_.tm_year
           << std::setw(2) << 1 + data_->tm_time_.tm_mon
           << std::setw(2) << data_->tm_time_.tm_mday
           << ' '
           << std::setw(2) << data_->tm_time_.tm_hour  << ':'
           << std::setw(2) << data_->tm_time_.tm_min   << ':'
           << std::setw(2) << data_->tm_time_.tm_sec   << "."
           << std::setw(6) << data_->usecs_
           << ' '
           << std::setfill(' ') << std::setw(5)
           << static_cast<unsigned int>(GetTID())
#ifdef DEBUG
           << ' '
           << data_->filename_ << ':' << data_->line_
#endif
           << "] ";
  data_->num_prefix_chars_ = data_->stream_.pcount();
}

std::ostream& LogMessage::stream() {
  return data_->stream_;
}

void LogMessage::Flush() {
  if (data_->has_been_flushed_ || !detail::CategoryActivated(data_->category_, data_->severity_)) {
    return;
  }
  data_->num_chars_to_log_ = data_->stream_.pcount();

  bool append_newline = (data_->message_buf_[data_->num_chars_to_log_ - 1] != '\n');
  if (append_newline) {
    data_->message_buf_[data_->num_chars_to_log_++] = '\n';
  }
  SendToLog();
}

void LogMessage::SendToLog() {
  static bool already_warned_before_init = false;
  if (EDK_UNLIKELY(!already_warned_before_init && !IsInitLogging())) {
    std::lock_guard<std::mutex> lk(before_init_warn_mutex);
    if (!already_warned_before_init) {
      const char w[] = "WARNING: Logging before InitLogging() is "
        "written to STDERR\n";
      WriteToStderr(w, strlen(w));
      already_warned_before_init = true;
    }
  }

  LogDestination::LogToStderr(data_->severity_, data_->message_buf_, data_->num_chars_to_log_);
  LogDestination::LogToSinks(data_);
  LogDestination::LogToFile(data_->message_buf_, data_->num_chars_to_log_, false);

  if (EDK_UNLIKELY(data_->severity_ == LOG_FATAL)) {
    if (!g_handle_signals) {
      backward::StackTrace st;
      st.load_here();
      backward::Printer p;
      p.snippet = true;
      p.address = true;
      p.object = true;
      p.print(st);
    }
    LogDestination::LogToFile("", 0, true);  // force flush
    abort();
  }
}

void InitLogging(bool log_to_stderr, bool log_to_file, const std::string& log_dir) noexcept {
  g_init_logging = true;
  g_log_to_stderr = log_to_stderr;
  g_log_to_file = log_to_file;
  if (log_to_file) {
    LogDestination::CreateLogDestination(log_dir);
  }
}

void ShutdownLogging() noexcept {
  LogDestination::DeleteLogDestination();
  g_init_logging = false;
}

void AddLogSink(LogSink* log_sink) noexcept {
  LogDestination::AddLogSink(log_sink);
}

void RemoveLogSink(LogSink* log_sink) noexcept {
  LogDestination::RemoveLogSink(log_sink);
}

void SetFileFlushInterval(uint32_t time) noexcept {
  g_flush_interval = time;
}

}  // namespace log
}  // namespace edk
