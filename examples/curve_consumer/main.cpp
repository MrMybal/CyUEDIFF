#include <cyuediff/curves.hpp>
#include <iostream>
namespace {
cyuediff::Package model(){
 using namespace cyuediff;
 auto key=[](double t,double v){return Json{{"kind","rich-curve-key"},{"time",t},{"value",v},{"interpMode",0},
   {"tangentMode",0},{"tangentWeightMode",0},{"arriveTangent",0.},{"arriveTangentWeight",0.},{"leaveTangent",0.},{"leaveTangentWeight",0.}};};
 Package p;p.complete_input=true;p.summary.ue5_version=1004;
 ObjectExport e;e.name=e.path="Curve";e.class_name="/Script/Engine.CurveFloat";
 e.properties={{"FloatCurve",{{"type",{{"name","StructProperty"},{"parameters",Json::array({{{"name","RichCurve"}}})}}},
   {"value",{{"Keys",{{"type",{{"name","ArrayProperty"},{"parameters",Json::array({{{"name","StructProperty"}}})}}},
     {"value",Json::array({key(0.,0.),key(1.,8.)})}}}}}}}};
 p.exports.push_back(std::move(e));return p;
}
}
int main(int argc,char **argv){
 using namespace cyuediff;
 try{
  if(argc>3)throw Error("arguments","Expected zero, one or two loose assets");
  const auto before=argc>1?Package::open(fs::u8path(argv[1])):model();
  auto after=argc>2?Package::open(fs::u8path(argv[2])):before;
  if(argc==1)after.exports[0].properties["FloatCurve"]["value"]["Keys"]["value"][1]["value"]=10.;
  std::cout<<Json{{"snapshot",curve_snapshot(before)},{"value",evaluate_curve(before,.5)},
    {"comparison",compare_curves(before,after).to_json()}}.dump(2)<<'\n';return 0;
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
