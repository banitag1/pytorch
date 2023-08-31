#pragma once

#include <c10/util/Exception.h>
#include <c10/util/Optional.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else // defined(_WIN32)
#include <Windows.h>
#include <fileapi.h>
#endif // defined(_WIN32)

namespace c10 {
namespace detail {
// Creates the filename pattern passed to and completed by `mkstemp`.
#if !defined(_WIN32)
inline std::string make_filename(std::string_view name_prefix) {
  // The filename argument to `mkstemp` needs "XXXXXX" at the end according to
  // http://pubs.opengroup.org/onlinepubs/009695399/functions/mkstemp.html
  static const std::string kRandomPattern = "XXXXXX";

  // We see if any of these environment variables is set and use their value, or
  // else default the temporary directory to `/tmp`.
  static const char* env_variables[] = {
      "TMPDIR", "TMP", "TEMP", "TEMPDIR"}; // NOLINT

  std::string tmp_directory = "/tmp";
  for (const char* variable : env_variables) {
    if (const char* path = getenv(variable)) {
      tmp_directory = path;
      break;
    }
  }

  std::filesystem::path filename = std::move(tmp_directory);

  filename /= name_prefix;
  filename += kRandomPattern;
  return filename.string();
}
#else
inline std::string make_filename() {
  char name[L_tmpnam_s]{};
  auto res = tmpnam_s(name, L_tmpnam_s);
  if (res != 0) {
    TORCH_WARN("Error generating temporary file");
    return "";
  }
  return name;
}
#endif // !defined(_WIN32)
} // namespace detail

struct TempFile {
  TempFile(const std::filesystem::path& name, int fd = -1)
      : fd(fd), name(name.string()) {}
  TempFile(const TempFile&) = delete;
  TempFile(TempFile&& other) noexcept
      : fd(other.fd), name(std::move(other.name)) {
    other.fd = -1;
  }

  TempFile& operator=(const TempFile&) = delete;
  TempFile& operator=(TempFile&& other) noexcept {
    fd = other.fd;
    other.fd = -1;
    name = std::move(other.name);
    other.name.clear();
    return *this;
  }

  ~TempFile() {
    if (!name.empty()) {
      std::filesystem::remove(name);
#if !defined(_WIN32)
      if (fd >= 0) {
        close(fd);
      }
#endif
    }
  }

  int fd;
  std::string name;
};

struct TempDir {
  TempDir() = default;
  explicit TempDir(std::filesystem::path name) : name(std::move(name)) {}
  TempDir(const TempDir&) = delete;
  TempDir(TempDir&& other) noexcept : name(std::move(other.name)) {
    other.name.clear();
  }

  TempDir& operator=(const TempDir&) = delete;
  TempDir& operator=(TempDir&& other) noexcept {
    name = std::move(other.name);
    other.name.clear();
    return *this;
  }

  ~TempDir() {
    if (!name.empty()) {
      std::filesystem::remove(name);
    }
  }

  std::filesystem::path name;
};

/// Attempts to return a temporary file or returns `nullopt` if an error
/// occurred.
///
/// The file returned follows the pattern
/// `<tmp-dir>/<name-prefix><random-pattern>`, where `<tmp-dir>` is the value of
/// the `"TMPDIR"`, `"TMP"`, `"TEMP"` or
/// `"TEMPDIR"` environment variable if any is set, or otherwise `/tmp`;
/// `<name-prefix>` is the value supplied to this function, and
/// `<random-pattern>` is a random sequence of numbers.
/// On Windows, `name_prefix` is ignored and `tmpnam` is used.
inline c10::optional<TempFile> try_make_tempfile(
    std::string_view name_prefix = "torch-file-") {
#if defined(_WIN32)
  auto filename = detail::make_filename();
#else
  auto filename = detail::make_filename(name_prefix);
#endif
  if (filename.empty()) {
    return c10::nullopt;
  }
#if defined(_WIN32)
  return TempFile(std::move(filename));
#else
  const int fd = mkstemp(filename.data());
  if (fd == -1) {
    return c10::nullopt;
  }
  // Don't make the string from string(filename.begin(), filename.end(), or
  // there will be a trailing '\0' at the end.
  return TempFile(std::move(filename), fd);
#endif // defined(_WIN32)
}

/// Like `try_make_tempfile`, but throws an exception if a temporary file could
/// not be returned.
inline TempFile make_tempfile(std::string_view name_prefix = "torch-file-") {
  if (auto tempfile = try_make_tempfile(name_prefix)) {
    return std::move(*tempfile);
  }
  TORCH_CHECK(false, "Error generating temporary file: ", std::strerror(errno));
}

/// Attempts to return a temporary directory or returns `nullopt` if an error
/// occurred.
///
/// The directory returned follows the pattern
/// `<tmp-dir>/<name-prefix><random-pattern>/`, where `<tmp-dir>` is the value
/// of the `"TMPDIR"`, `"TMP"`, `"TEMP"` or
/// `"TEMPDIR"` environment variable if any is set, or otherwise `/tmp`;
/// `<name-prefix>` is the value supplied to this function, and
/// `<random-pattern>` is a random sequence of numbers.
/// On Windows, `name_prefix` is ignored and `tmpnam` is used.
inline c10::optional<TempDir> try_make_tempdir(
    std::string_view name_prefix = "torch-dir-") {
#if defined(_WIN32)
  for (int i = 0; i < 100; i++) {
    auto dirname = detail::make_filename();
    if (dirname.empty()) {
      return c10::nullopt;
    }
    std::error_code ec{};
    if (std::filesystem::create_directories(dirname, ec)) {
      return TempDir(dirname);
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
      return c10::nullopt;
    }
  }
  return c10::nullopt;
#else
  auto filename = detail::make_filename(name_prefix);
  const char* dirname = mkdtemp(filename.data());
  if (!dirname) {
    return c10::nullopt;
  }
  return TempDir(dirname);
#endif // defined(_WIN32)
}

/// Like `try_make_tempdir`, but throws an exception if a temporary directory
/// could not be returned.
inline TempDir make_tempdir(std::string_view name_prefix = "torch-dir-") {
  if (auto tempdir = try_make_tempdir(name_prefix)) {
    return std::move(*tempdir);
  }
  TORCH_CHECK(
      false, "Error generating temporary directory: ", std::strerror(errno));
}
} // namespace c10
