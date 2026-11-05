#pragma once
#include "rich_curve_fixture.hpp"
namespace cyuediff::test {
inline Fixture transform_curve_file_fixture(int change = 0, int version = 1013) {
    return fixture(
        version, 1, false, 0, {"CurveData",         "AnimationCurveData", "TransformCurves",
                               "TransformCurve",    "FloatCurves",        "FloatCurve",
                               "VectorCurve",       "TranslationCurve",   "RotationCurve",
                               "ScaleCurve",        "CurveName",          "LegacyName",
                               "NameProperty",      "Root~/control",      "Hand",
                               "RichCurve",         "RichCurveKey",       "Keys",
                               "CurveTypeFlags",    "DefaultValue",       "FloatProperty",
                               "PreInfinityExtrap", "PostInfinityExtrap", "ByteProperty"},
        [=](Bytes &s, const NameWriter &name) {
            auto tag = [&](Bytes &out, const char *field, const char *kind, const Bytes &payload,
                           const char *structure = nullptr, bool array = false, int index = 0) {
                name(out, field);
                name(out, kind);
                if (version >= 1012) {
                    out.i(structure ? 1 : 0);
                    if (structure) {
                        if (array) {
                            name(out, "StructProperty");
                            out.i(1);
                        }
                        name(out, structure);
                        out.i(0);
                    }
                }
                out.i(static_cast<int>(payload.data.size()));
                if (version >= 1012) {
                    out.put<std::uint8_t>(index ? 1 : 0);
                    if (index)
                        out.i(index);
                } else {
                    out.i(index);
                    if (array)
                        name(out, "StructProperty");
                    else if (structure) {
                        name(out, structure);
                        out.zeros(16);
                    } else if (std::string(kind) == "ByteProperty")
                        name(out, "None");
                    out.put<std::uint8_t>(0);
                    if (version >= 1011)
                        out.put<std::uint8_t>(0);
                }
                out.append(payload);
            };
            auto array_start = [&](Bytes &out, const char *field, const char *structure, int count) {
                out.i(count);
                if (version < 1012) {
                    Bytes empty;
                    tag(out, field, "StructProperty", empty, structure);
                }
            };
            Bytes data, groups;
            array_start(groups, "TransformCurves", "TransformCurve", change == 6 ? 0 : 2);
            if (change != 6)
                for (int position = 0; position < 2; ++position) {
                    const int group = change == 2 ? 1 - position : position;
                    Bytes n;
                    name(n, group == 0 || change == 3 ? "Root~/control" : "Hand");
                    tag(groups, change == 8 && group == 0 ? "LegacyName" : "CurveName", "NameProperty", n);
                    Bytes flags;
                    flags.i(4);
                    tag(groups, "CurveTypeFlags", "IntProperty", flags);
                    int part_index = 0;
                    for (const char *part : {"TranslationCurve", "RotationCurve", "ScaleCurve"}) {
                        const int component = part_index++;
                        if (change == 4 && group == 0 && component == 2)
                            continue;
                        Bytes vector;
                        for (int axis = 0; axis < 3; ++axis) {
                            if (change == 5 && group == 0 && component == 1 && axis == 1)
                                continue;
                            Bytes curve, keys;
                            const int count = change >= 11 && change <= 13 ? 0 : 2;
                            array_start(keys, "Keys", "RichCurveKey", count);
                            for (int key = 0; key < count; ++key) {
                                const bool target = group == 0 && component == 0 && axis == 0;
                                const float base = component == 2 ? 1.f : float(axis);
                                auto k = rich_curve_key(
                                    float(key * 2),
                                    base + key * 2 + (change == 1 && target && key == 1 ? 2.f : 0.f),
                                    (change == 9 || change == 10 || change == 14) && target && key == 0 ? 2
                                                                                                        : 0);
                                if (target && key == 0 && (change == 9 || change == 14)) {
                                    k.data[2] = 2;
                                    const float weight = change == 14 ? -1.f : 1.f;
                                    std::memcpy(k.data.data() + 23, &weight, 4);
                                }
                                if (change == 10 && target && key == 0)
                                    k.data[2] = 255;
                                keys.append(k);
                            }
                            tag(curve, "Keys", "ArrayProperty", keys, "RichCurveKey", true);
                            if (change == 11 || change == 13) {
                                Bytes def;
                                def.put<float>(change == 11 ? 3.f : std::numeric_limits<float>::max());
                                tag(curve, "DefaultValue", "FloatProperty", def);
                            }
                            if (change == 15)
                                for (const char *field : {"PreInfinityExtrap", "PostInfinityExtrap"}) {
                                    Bytes mode;
                                    mode.put<std::uint8_t>(4);
                                    tag(curve, field, "ByteProperty", mode);
                                }
                            name(curve, "None");
                            tag(vector, "FloatCurves", "StructProperty", curve, "RichCurve", false, axis);
                        }
                        name(vector, "None");
                        tag(groups, part, "StructProperty", vector,
                            change == 16 && component == 2 && group == 0 ? "RichCurve" : "VectorCurve");
                    }
                    name(groups, "None");
                }
            if (change != 7)
                tag(data, "TransformCurves", "ArrayProperty", groups, "TransformCurve", true);
            Bytes floats;
            array_start(floats, "FloatCurves", "FloatCurve", 0);
            tag(data, "FloatCurves", "ArrayProperty", floats, "FloatCurve", true);
            name(data, "None");
            tag(s, "CurveData", "StructProperty", data, "AnimationCurveData");
        },
        -1, -1, -1, -1, -1, -1, -1, "AnimDataModel", "/Script/Engine");
}
} // namespace cyuediff::test
