#include <cyuediff/collision.hpp>
#include <cyuediff/collision_wireframe.hpp>
#include <iostream>
int main(int argc, char **argv) {
    try {
        cyuediff::Package before, after;
        if (argc > 1) {
            before = cyuediff::Package::open(cyuediff::fs::u8path(argv[1]));
            after = argc > 2 ? cyuediff::Package::open(cyuediff::fs::u8path(argv[2])) : before;
        } else {
            // A small host-populated decoded model demonstrates the API without engine assets.
            cyuediff::ObjectExport body;
            body.path = "Body";
            body.class_name = "/Script/Engine.BodySetup";
            body.coverage = "properties";
            body.properties = {{"AggGeom",
                                {{"value",
                                  {{"SphereElems",
                                    {{"value", cyuediff::Json::array({{{"Name", {{"value", "Ball"}}},
                                                                       {"Center", {{"value", {0., 0., 0.}}}},
                                                                       {"Radius", {{"value", 1.}}}}})}}}}}}}};
            before.exports.push_back(body);
            after = before;
            after.exports[0].properties["AggGeom"]["value"]["SphereElems"]["value"][0]["Radius"]["value"] =
                2.;
        }
        std::cout << cyuediff::Json{{"snapshot", before.collision_snapshot()},
                                    {"wireframe", cyuediff::collision_wireframe(before).to_json()},
                                    {"comparison", cyuediff::compare_collisions(before, after).to_json()}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
