#pragma once
#include <array>
#include <brtoy/brtoy.h>
#include <brtoy/vec.h>
#include <memory>
#include <optional>
#include <string_view>

namespace brtoy {

struct Handle {
    static Handle fromIndex(size_t index);

    bool valid() const;
    operator bool() const;
    size_t index() const;

    size_t m_value = 0;
};

using Window = u64;

struct WindowState {
    OsHandle native_handle;
    bool is_closing;
    V2u dim;
};

struct Input {
    float mouse_dx;
    float mouse_dy;
    bool lmb_is_down;
    bool rmb_is_down;
    std::array<bool, 256> key_is_down;
};

class Platform {
  public:
    struct Impl;
    using ImplPtr = std::unique_ptr<Impl>;

    Platform() = default;
    Platform(Platform &&) = default;

    virtual ~Platform();

    static std::optional<Platform> init();
    static void errorMessage(const char *msg);

    OsHandle appInstanceHandle() const;
    bool tick(Input &out_input);
    void requestQuit();

    Window createWindow(const char *name);
    void setWindowTitle(Window window, std::string_view title);
    WindowState windowState(Window window) const;

    u64 getTimestampTicksPerSecond();
    u64 getTimestamp();

  private:
    ImplPtr m_impl;
};

} // namespace brtoy
