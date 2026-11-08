#include <cyuediff/animation_compression.hpp>
#include <fstream>
#include <iostream>
using namespace cyuediff;
int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            std::cerr << "Usage: per_track_animation_consumer INPUT.json BUFFER.bin OUTPUT.json\n";
            return 2;
        }
        if (fs::file_size(fs::u8path(argv[1])) > 16ull * 1024 * 1024)
            throw std::runtime_error("Metadata exceeds example read limit");
        Json input;
        {
            std::ifstream f(fs::u8path(argv[1]));
            if (!f)
                throw std::runtime_error("Cannot open metadata");
            f >> input;
        }
        PerTrackAnimationSource s;
        s.frame_count = input.at("frames").get<std::uint32_t>();
        s.track_to_skeleton = input.at("trackToSkeleton").get<std::vector<std::int32_t>>();
        s.track_offset_count = input.at("trackOffsetCount").get<std::uint32_t>();
        s.scale_offset_count = input.at("scaleOffsetCount").get<std::uint32_t>();
        s.scale_strip_size = input.at("scaleStripSize").get<std::uint32_t>();
        s.key_byte_count = input.at("keyBytes").get<std::uint32_t>();
        const auto path = fs::u8path(argv[2]);
        const auto size = fs::file_size(path);
        if (size > 256ull * 1024 * 1024)
            throw std::runtime_error("Buffer exceeds example read limit");
        s.buffer.resize(static_cast<std::size_t>(size));
        {
            std::ifstream f(path, std::ios::binary);
            if (!f || !f.read(reinterpret_cast<char *>(s.buffer.data()), static_cast<std::streamsize>(size)))
                throw std::runtime_error("Cannot read buffer");
        }
        PerTrackAnimationOptions o;
        o.profile = PerTrackAnimationProfile::unreal_5_3_windows;
        const auto decoded = decode_per_track_animation(s, o);
        Json output = {{"animation", decoded.to_json(input.value("includeKeys", false))},
                       {"samples", Json::array()}};
        const auto frames = input.at("sampleFrames").get<std::vector<double>>();
        if (frames.size() > 100000 || std::uint64_t(frames.size()) * s.track_to_skeleton.size() > 200000)
            throw std::runtime_error("Samples exceed example output limit");
        const auto interpolation = input.value("step", false) ? CompressedAnimationInterpolation::step
                                                              : CompressedAnimationInterpolation::linear;
        for (double frame : frames) {
            Json sample = {{"frame", frame}, {"tracks", Json::array()}};
            for (const auto &pose : decoded.sample(frame, interpolation))
                sample["tracks"].push_back(pose.to_json());
            output["samples"].push_back(std::move(sample));
        }
        const auto destination = fs::u8path(argv[3]);
        if (fs::exists(destination) &&
            (fs::equivalent(destination, fs::u8path(argv[1])) || fs::equivalent(destination, path)))
            throw std::runtime_error("Output cannot replace metadata or buffer input");
        std::ofstream f(destination, std::ios::binary);
        if (!f || !(f << output.dump(2) << '\n'))
            throw std::runtime_error("Cannot write output");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
