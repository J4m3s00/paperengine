#pragma once

#include "base/error.h"

#include <algorithm>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Paper {
// A named, typed, runtime-tunable value. Reading is a plain member access, so
// it is safe in hot simulation code. Not thread-safe: set cvars from the
// thread that runs the simulation (or between ticks).
class CvarBase {
public:
  virtual ~CvarBase() = default;
  CvarBase(const CvarBase&) = delete;
  CvarBase& operator=(const CvarBase&) = delete;

  [[nodiscard]] const std::string& name() const { return m_name; }
  [[nodiscard]] const std::string& description() const { return m_description; }
  [[nodiscard]] virtual std::string toString() const = 0;
  [[nodiscard]] virtual std::string defaultString() const = 0;
  // Parses `text` and clamps to the cvar's range. Leaves the value unchanged
  // and returns an Error if `text` is not a valid value.
  [[nodiscard]] virtual std::expected<void, Error> setFromString(std::string_view text) = 0;
  virtual void reset() = 0;

protected:
  CvarBase(std::string name, std::string description)
      : m_name(std::move(name)), m_description(std::move(description)) {}

private:
  std::string m_name;
  std::string m_description;
};

template <typename T> class Cvar final : public CvarBase {
public:
  Cvar(std::string name, T defaultValue, T min, T max, std::string description)
      : CvarBase(std::move(name), std::move(description)), m_value(defaultValue),
        m_default(defaultValue), m_min(min), m_max(max) {}

  [[nodiscard]] T value() const { return m_value; }
  [[nodiscard]] T min() const { return m_min; }
  [[nodiscard]] T max() const { return m_max; }
  operator T() const { return m_value; }

  // Clamps to [min, max].
  void set(T value) { m_value = std::clamp(value, m_min, m_max); }

  [[nodiscard]] std::string toString() const override;
  [[nodiscard]] std::string defaultString() const override;
  [[nodiscard]] std::expected<void, Error> setFromString(std::string_view text) override;
  void reset() override { m_value = m_default; }

private:
  T m_value;
  T m_default;
  T m_min;
  T m_max;
};

// Owns all cvars. Cvar references stay valid for the registry's lifetime.
class CvarRegistry {
public:
  // Registers a new cvar. Registering a name twice is a programmer error.
  template <typename T>
  Cvar<T>& add(std::string name, T defaultValue, T min, T max, std::string description = {}) {
    auto cvar = std::make_unique<Cvar<T>>(std::move(name), defaultValue, min, max,
                                          std::move(description));
    Cvar<T>& ref = *cvar;
    insert(std::move(cvar));
    return ref;
  }
  Cvar<bool>& addBool(std::string name, bool defaultValue, std::string description = {}) {
    return add<bool>(std::move(name), defaultValue, false, true, std::move(description));
  }

  [[nodiscard]] CvarBase* find(std::string_view name) const;

  // Sets one cvar by name. NotFound for unknown names, InvalidArgument for
  // unparsable values.
  [[nodiscard]] std::expected<void, Error> set(std::string_view name, std::string_view value);

  // Executes one config line: "name value". Blank lines and '#' comments are
  // ignored (returns success).
  [[nodiscard]] std::expected<void, Error> execute(std::string_view line);

  // Executes every line of a config file. Bad lines are logged and skipped;
  // only an unreadable file is an error.
  [[nodiscard]] std::expected<void, Error> loadFile(const std::string& path);

  // All cvars sorted by name (for UI and dumps).
  [[nodiscard]] const std::vector<CvarBase*>& all() const { return m_sorted; }

  void resetAll();

private:
  void insert(std::unique_ptr<CvarBase> cvar);

  std::vector<std::unique_ptr<CvarBase>> m_storage;
  std::vector<CvarBase*> m_sorted;
};

// Process-wide registry used by game code (e.g. `sv_airaccelerate`).
CvarRegistry& cvars();
} // namespace Paper
