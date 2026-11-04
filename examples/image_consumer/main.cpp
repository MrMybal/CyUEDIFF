#include <cyuediff/hdr.hpp>
#include <iostream>
int main() {
    // Host-decoded samples, independent of Unreal, Git, CyUI and GPU resources.
    cyuediff::ImageRgba8 before{2, 2, std::vector<std::uint8_t>(16, 0)}, after = before;
    after.pixels[7] = 255; // pixel (1,0), alpha channel
    auto ldr = cyuediff::compare_images(before, after);
    cyuediff::ImageRgba32f hdr_before{2, 2, std::vector<float>(16, 1)}, hdr_after = hdr_before;
    hdr_after.pixels[8] += .5f; // pixel (0,1), red channel in source float space
    auto hdr = cyuediff::compare_hdr_images(hdr_before, hdr_after);
    std::cout << cyuediff::Json{{"ldr", ldr.to_json()}, {"hdr", hdr.to_json()}}.dump(2) << '\n';
}
