#include "base/cvar.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <fstream>
#include <string>

namespace Paper {
namespace {
std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
    s.remove_suffix(1);
  return s;
}

Error invalid(std::string message) { return {ErrorCode::InvalidArgument, std::move(message)}; }

template <typename T> std::expected<T, Error> parse(std::string_view text);

template <> std::expected<bool, Error> parse<bool>(std::string_view text) {
  if (text == "1" || text == "true")
    return true;
  if (text == "0" || text == "false")
    return false;
  return std::unexpected(invalid("expected 0, 1, true or false, got '" + std::string(text) + "'"));
}

template <> std::expected<int, Error> parse<int>(std::string_view text) {
  int value = 0;
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size())
    return std::unexpected(invalid("expected an integer, got '" + std::string(text) + "'"));
  return value;
}

template <> std::expected<float, Error> parse<float>(std::string_view text) {
  float value = 0;
  auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc{} || end != text.data() + text.size() || !std::isfinite(value))
    return std::unexpected(invalid("expected a number, got '" + std::string(text) + "'"));
  return value;
}

template <typename T> std::string format(T value) {
  if constexpr (std::is_same_v<T, bool>)
    return value ? "1" : "0";
  else
    return std::to_string(value);
}
} // namespace

template <typename T> std::string Cvar<T>::toString() const { return format(m_value); }
template <typename T> std::string Cvar<T>::defaultString() const { return format(m_default); }

template <typename T>
std::expected<void, Error> Cvar<T>::setFromString(std::string_view text) {
  auto parsed = parse<T>(trim(text));
  if (!parsed)
    return std::unexpected(Error{parsed.error().code, name() + ": " + parsed.error().message});
  set(*parsed);
  return {};
}

template class Cvar<bool>;
template class Cvar<int>;
template class Cvar<float>;

void CvarRegistry::insert(std::unique_ptr<CvarBase> cvar) {
  assert(!find(cvar->name()) && "cvar registered twice");
  CvarBase* raw = cvar.get();
  m_storage.push_back(std::move(cvar));
  auto pos = std::lower_bound(m_sorted.begin(), m_sorted.end(), raw,
                              [](const CvarBase* a, const CvarBase* b) { return a->name() < b->name(); });
  m_sorted.insert(pos, raw);
}

CvarBase* CvarRegistry::find(std::string_view name) const {
  auto pos = std::lower_bound(m_sorted.begin(), m_sorted.end(), name,
                              [](const CvarBase* a, std::string_view n) { return a->name() < n; });
  return pos != m_sorted.end() && (*pos)->name() == name ? *pos : nullptr;
}

std::expected<void, Error> CvarRegistry::set(std::string_view name, std::string_view value) {
  CvarBase* cvar = find(name);
  if (!cvar)
    return std::unexpected(Error{ErrorCode::NotFound, "unknown cvar '" + std::string(name) + "'"});
  return cvar->setFromString(value);
}

std::expected<void, Error> CvarRegistry::execute(std::string_view line) {
  line = trim(line);
  if (line.empty() || line.front() == '#')
    return {};
  const size_t split = line.find_first_of(" \t");
  if (split == std::string_view::npos)
    return std::unexpected(invalid("missing value for '" + std::string(line) + "'"));
  return set(line.substr(0, split), trim(line.substr(split)));
}

std::expected<void, Error> CvarRegistry::loadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file)
    return std::unexpected(Error{ErrorCode::Io, "cannot open config '" + path + "'"});
  std::string line;
  int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    if (auto result = execute(line); !result)
      spdlog::warn("{}:{}: {}", path, lineNumber, result.error().message);
  }
  return {};
}

void CvarRegistry::resetAll() {
  for (CvarBase* cvar : m_sorted)
    cvar->reset();
}

CvarRegistry& cvars() {
  static CvarRegistry registry;
  return registry;
}
} // namespace Paper
