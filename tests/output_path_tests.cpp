#include "../apps/desktop/path_guard.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using namespace cyuediff;
using namespace cyuediff::desktop;
int main() {
    auto root =
        fs::temp_directory_path() /
        ("cyuediff-path-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto missing = root / "Asset.uexp";
        if (resolved_output_path(missing) != fs::canonical(root) / "Asset.uexp")
            throw std::runtime_error("Missing companion ancestor");
        if (resolved_output_path(root / "nested" / "new" / "report.json") !=
            fs::canonical(root) / "nested" / "new" / "report.json")
            throw std::runtime_error("Missing nested output ancestor");
        {
            std::ofstream out(root / "Asset.uasset");
            out << "fixture";
        }
        if (resolved_output_path(root / "Asset.uasset") != fs::canonical(root / "Asset.uasset"))
            throw std::runtime_error("Existing input canonical identity");
#ifdef _WIN32
        const DWORD size = GetShortPathNameW(root.c_str(), nullptr, 0);
        if (size) {
            std::wstring short_name(size, L'\0');
            const auto written = GetShortPathNameW(root.c_str(), short_name.data(), size);
            if (written && written < size) {
                short_name.resize(written);
                if (resolved_output_path(fs::path(short_name) / "Asset.uexp") !=
                    fs::canonical(root) / "Asset.uexp")
                    throw std::runtime_error("Short parent absent companion");
            }
        }
#endif
        fs::remove_all(root);
        std::cout << "Desktop absent companion and ancestor path checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        fs::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
