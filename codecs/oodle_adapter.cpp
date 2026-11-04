// Optional local adapter. The Oodle SDK is supplied by the host; no SDK files are vendored.
#include <oodle2.h>
#include <cstdint>
#include <cstddef>
#include <limits>
extern "C" __declspec(dllexport) std::size_t
cyuediff_decode_block_v1(std::uint32_t method, const std::uint8_t *source, std::size_t source_bytes,
                         std::uint8_t *output, std::size_t output_bytes) {
    if (method != 3 || !source || !output ||
        source_bytes > static_cast<std::size_t>(std::numeric_limits<OO_SINTa>::max()) ||
        output_bytes > static_cast<std::size_t>(std::numeric_limits<OO_SINTa>::max()))
        return 0;
    auto result = OodleLZ_Decompress(source, static_cast<OO_SINTa>(source_bytes), output,
                                     static_cast<OO_SINTa>(output_bytes), OodleLZ_FuzzSafe_Yes,
                                     OodleLZ_CheckCRC_No, OodleLZ_Verbosity_None);
    return result > 0 ? static_cast<std::size_t>(result) : 0;
}
