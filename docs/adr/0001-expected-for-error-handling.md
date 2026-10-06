# 0001: `std::expected` for error handling

Status: accepted

## Decision

All fallible operations return `std::expected<T, Paper::Error>` with `Error {ErrorCode code; std::string message;}`. This includes `VulkanContext`, `Swapchain` and `Window`, which currently throw `std::runtime_error` through `unwrap`.

Constructors cannot return `expected`, so these types get private constructors and static factories. They stay non-copyable and non-movable (see `CLAUDE.md`), so factories return `std::expected<std::unique_ptr<T>, Error>`. `unwrap` becomes a helper that translates vk-bootstrap errors into `Error` instead of throwing.

Programmer errors (stale handle, wrong frame state) are `assert`s, not `expected`.

Warnings (missing optional map, fallback to a default texture) go to `spdlog::warn`.

## Consequences

- One error style across the engine; no exceptions crossing the public API.
- Destruction order stays the reverse declaration order in `main`.
- Existing code needs a one-off refactor before the rendering work starts.
