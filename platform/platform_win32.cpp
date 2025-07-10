#include <Windows.h>
#include <array>
#include <brtoy/platform.h>
#include <format>
#include <string>
#include <unordered_map>

namespace brtoy {

static std::wstring widen(const char *str, size_t size = 0) {
    if (size == 0)
        size = std::strlen(str);
    std::wstring result;
    int result_size = MultiByteToWideChar(CP_UTF8, 0, str, size, nullptr, 0);
    if (result_size > 0) {
        result.resize(result_size);
        MultiByteToWideChar(CP_UTF8, 0, str, size, &result[0], result_size);
    }
    return result;
}

struct NativeWindowState {
    WindowState m_state;
};

struct Platform::Impl {
    Impl();

    static constexpr wchar_t WindowClassName[] = L"brtoy_window_class";

    HINSTANCE m_instance;
    ATOM m_wnd_class;
    std::unordered_map<Window, NativeWindowState> m_windows;
    u64 m_ticks_per_second;
    Input m_cur_input = {};

    bool m_alive = true;
};

LRESULT windowProc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param);

Platform::Impl::Impl() : m_instance(GetModuleHandleW(NULL)) {
    LARGE_INTEGER qpf_result;
    QueryPerformanceFrequency(&qpf_result);
    m_ticks_per_second = qpf_result.QuadPart;

    WNDCLASSEXW wnd_class_desc = {
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = windowProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = m_instance,
        .hIcon = NULL,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .hbrBackground = NULL,
        .lpszMenuName = NULL,
        .lpszClassName = WindowClassName,
        .hIconSm = NULL,
    };
    m_wnd_class = RegisterClassExW(&wnd_class_desc);
}

Platform::~Platform() = default;

std::optional<Platform> Platform::init() {
    auto impl = std::make_unique<Platform::Impl>();
    if (impl && impl->m_wnd_class) {
        Platform platform;
        platform.m_impl = std::move(impl);
        return platform;
    } else {
        return {};
    }
}

void Platform::errorMessage(const char *msg) {
    MessageBoxW(NULL, widen(msg).c_str(), L"brtoy", MB_OK | MB_ICONEXCLAMATION);
}

OsHandle Platform::appInstanceHandle() const { return (OsHandle)m_impl->m_instance; }

LRESULT windowProc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;
    bool handled = true;

    NativeWindowState *window_state = nullptr;
    Window window = (Window)hwnd;
    Platform::Impl *platform = nullptr;
    if (hwnd) {
        platform = (Platform::Impl *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (platform) {
            window_state = &platform->m_windows[window];
        }
    }

    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN: {
        if (msg == WM_LBUTTONDOWN)
            platform->m_cur_input.lmb_is_down = true;
        if (msg == WM_RBUTTONDOWN)
            platform->m_cur_input.rmb_is_down = true;
        if (GetCapture() != hwnd)
            SetCapture(hwnd);
    } break;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        if (msg == WM_LBUTTONUP)
            platform->m_cur_input.lmb_is_down = false;
        if (msg == WM_RBUTTONUP)
            platform->m_cur_input.rmb_is_down = false;
        if (!platform->m_cur_input.lmb_is_down && !platform->m_cur_input.rmb_is_down)
            ReleaseCapture();

    } break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (w_param <= 0xff) {
            UINT key_code = w_param;
            if (key_code == VK_SHIFT) {
                UINT scan_code = (0xff0000 & (UINT)l_param) >> 16;
                key_code = MapVirtualKey(scan_code, MAPVK_VSC_TO_VK_EX);
            }
            bool went_down = (l_param & (1 << 31)) == 0;
            platform->m_cur_input.key_is_down[key_code] = went_down;
        }
        if (w_param > 0xff || msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            handled = false;
        }
    } break;
    case WM_CAPTURECHANGED: {
        platform->m_cur_input.lmb_is_down = false;
        platform->m_cur_input.rmb_is_down = false;
    } break;
    case WM_CLOSE:
        if (window_state)
            window_state->m_state.is_closing = true;
        break;
    case WM_SIZE:
        if (window_state)
            window_state->m_state.dim = {LOWORD(l_param), HIWORD(l_param)};
        break;
    default:
        handled = false;
    }

    if (!handled)
        result = DefWindowProcW(hwnd, msg, w_param, l_param);
    return result;
}

bool Platform::tick(Input &input) {
    for (auto it = m_impl->m_windows.begin(); it != m_impl->m_windows.end();) {
        if (!m_impl->m_alive || it->second.m_state.is_closing) {
            DestroyWindow((HWND)it->second.m_state.native_handle);
            it = m_impl->m_windows.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_impl->m_alive) {
        PostQuitMessage(0);
    }

    m_impl->m_cur_input.mouse_dx = 0;
    m_impl->m_cur_input.mouse_dy = 0;

    // input
    {
        UINT cb_size = 0;
        UINT getrib_result = GetRawInputBuffer(NULL, &cb_size, sizeof(RAWINPUTHEADER));
        BRTOY_ASSERT(getrib_result == 0);
        if (cb_size) {
            constexpr size_t ri_count_max = 16;
            UINT rib_capacity = ri_count_max * cb_size;
            std::vector<std::byte> rib_bytes(rib_capacity);
            RAWINPUT *rib = (RAWINPUT *)rib_bytes.data();
            UINT rib_size = rib_capacity;
            UINT ri_count = GetRawInputBuffer(rib, &rib_size, sizeof(RAWINPUTHEADER));
            if (ri_count == UINT(-1)) {
                auto msg = std::format("Unable to get raw input buffer (err: {})", GetLastError());
                Platform::errorMessage(msg.c_str());
                PostQuitMessage(1);
            } else if (ri_count > 0) {
                RAWINPUT *ri = rib;
                for (UINT i = 0; i < ri_count; ++i) {
                    RID_DEVICE_INFO ri_device = {};
                    ri_device.cbSize = sizeof(RID_DEVICE_INFO);
                    UINT ri_info_size = sizeof(ri_device);
                    UINT res = GetRawInputDeviceInfoW(ri->header.hDevice, RIDI_DEVICEINFO,
                                                      &ri_device, &ri_info_size);
                    BRTOY_ASSERT(res == sizeof(RID_DEVICE_INFO));
                    if (ri_device.dwType == RIM_TYPEMOUSE) {
                        DWORD sample_rate = ri_device.mouse.dwSampleRate;
                        float sample_rate_recip = 1.0f / sample_rate;
                        RAWMOUSE &rm = ri->data.mouse;
                        if (rm.usFlags & MOUSE_MOVE_ABSOLUTE) {
                            // TODO: Handle absolute mouse move (i.e. remote
                            // desktop, weird device?)
                        } else if (!(rm.lLastX == 0 && rm.lLastY == 0)) {
                            if (sample_rate != 0) {
                                m_impl->m_cur_input.mouse_dx = (float)rm.lLastX * sample_rate_recip;
                                m_impl->m_cur_input.mouse_dy = (float)rm.lLastY * sample_rate_recip;
                            } else {
                                m_impl->m_cur_input.mouse_dx = rm.lLastX;
                                m_impl->m_cur_input.mouse_dy = rm.lLastY;
                            }
                        } else {
                        }
                    }
                }
            }
        }
    }

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    input = m_impl->m_cur_input;
    return m_impl->m_alive;
}

void Platform::requestQuit() { m_impl->m_alive = false; }

Window Platform::createWindow(const char *name) {
    DWORD style_ex = WS_EX_OVERLAPPEDWINDOW;
    DWORD style = WS_OVERLAPPEDWINDOW;
    HWND hwnd = CreateWindowExW(style_ex, Impl::WindowClassName, widen(name).c_str(), style,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL,
                                NULL, GetModuleHandleW(NULL), NULL);

    Window window = 0;
    if (hwnd != NULL) {
        window = (Window)hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)m_impl.get());
        m_impl->m_windows[window] = {.m_state = {.native_handle = (u64)hwnd}};

        std::array rids = std::to_array<RAWINPUTDEVICE>({
            {
                .usUsagePage = 0x1,
                .usUsage = 0x2,
                .dwFlags = RIDEV_INPUTSINK,
                .hwndTarget = hwnd,
            },
            {
                .usUsagePage = 0x1,
                .usUsage = 0x6,
                .dwFlags = RIDEV_INPUTSINK,
                .hwndTarget = hwnd,
            },
        });
        BOOL rid_register_result =
            RegisterRawInputDevices(rids.data(), rids.size(), sizeof(RAWINPUTDEVICE));
        ShowWindow(hwnd, SW_SHOW);
    }
    return window;
}

void Platform::setWindowTitle(Window window, std::string_view title) {
    std::wstring title_wide = widen(title.data(), title.length());
    SetWindowTextW((HWND)window, title_wide.c_str());
}

WindowState Platform::windowState(Window window) const {
    WindowState state{};
    if (window) {
        state = m_impl->m_windows[window].m_state;
    }
    return state;
}

u64 Platform::getTimestampTicksPerSecond() { return m_impl->m_ticks_per_second; }

u64 Platform::getTimestamp() {
    LARGE_INTEGER qpf_result;
    QueryPerformanceCounter(&qpf_result);
    return qpf_result.QuadPart;
}

} // namespace brtoy
