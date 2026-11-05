#include "../apps/desktop/platform_win32.hpp"
#include <iostream>
using namespace cyuediff;
using namespace cyuediff::desktop;
int main() {
    int checks = 0;
    auto check = [&](bool good, const char *message) {
        ++checks;
        if (!good)
            throw std::runtime_error(message);
    };
    try {
        NativeWindow window(true, WindowPlacement{900000, -900000, 2400, 1600, 192, true});
        check(window.placement_restored && !IsWindowVisible(window.handle), "Restoration remains hidden");
        auto p = window.placement();
        check(p.has_value() && p->maximized && p->x != 900000 && p->y != -900000,
              "Stored intent and fitted bounds");
        RECT r{};
        check(GetWindowRect(window.handle, &r) && r.left == p->x && r.top == p->y &&
                  r.right - r.left == p->width && r.bottom - r.top == p->height,
              "Reported placement matches native rectangle");
        check(WindowPlacement::from_json(p->to_json()).to_json() == p->to_json(),
              "Native placement satisfies file contract");
        window.reset_placement();
        p = window.placement();
        check(p && !p->maximized && !IsWindowVisible(window.handle),
              "Reset clears maximized intent and remains hidden");
        check(SetWindowPos(window.handle, nullptr, p->x + 10, p->y + 10, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE),
              "Move hidden window");
        auto moved = window.placement();
        check(moved && moved->x == p->x + 10 && moved->y == p->y + 10 && moved->width == p->width &&
                  moved->height == p->height,
              "Normal rectangle is remembered on native movement");
        SendMessageW(window.handle, WM_CLOSE, 0, 0);
        check(IsWindow(window.handle) && window.quit && !window.pump(),
              "Close retains HWND for final persistence");
        check(window.placement()->to_json() == moved->to_json() && !IsWindowVisible(window.handle),
              "Clean-close placement remains readable without displaying a window");
        std::cout << checks << " native window lifecycle checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
