#include "white/modifiers.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace white {
namespace {
bool finite(Vec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool range(double v,double lo,double hi){return std::isfinite(v)&&v>=lo&&v<=hi;}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);}
double distance(const EllipsoidMask& mask,Vec3 p){p=p-mask.center;p={p.x/mask.radii.x,p.y/mask.radii.y,p.z/mask.radii.z};return (std::sqrt(dot(p,p))-1)*std::min({mask.radii.x,mask.radii.y,mask.radii.z});}
void require(const std::vector<std::string>& errors){if(errors.empty())return;std::string message="Invalid finishing stack:";for(const auto& error:errors)message+="\n- "+error;throw std::invalid_argument(message);}
unsigned target_mask(const FinishModifier& layer,Id object,std::span<const Id> fields){
    if(layer.target_kind==FinishTargetKind::object)return layer.target_id==object?(1u<<fields.size())-1:0;
    unsigned mask=0;for(std::size_t i=0;i<fields.size();++i)if(fields[i]==layer.target_id)mask|=1u<<i;return mask;
}
}
std::vector<std::string> validate_finish_stack(const FinishStack& stack,Id object,std::span<const Id> fields){
    std::vector<std::string> errors;auto check=[&](bool okay,std::string message){if(!okay)errors.push_back(std::move(message));};
    check(stack.contract_version==modifier_stack_contract_version,"Unsupported modifier contract version");
    check(stack.layers.size()<=max_finish_modifiers,"Four-layer finishing budget exceeded");
    check(fields.size()<=2,"Finishing requires at most two evaluated fields");
    std::set<Id> ids;
    for(const auto& layer:stack.layers){
        const auto prefix="Layer "+std::to_string(layer.id)+": ";
        check(layer.id!=0&&ids.insert(layer.id).second,prefix+"stable ID must be nonzero and unique");
        check(layer.kind==FinishModifierKind::cut||layer.kind==FinishModifierKind::density||layer.kind==FinishModifierKind::protect_detail,prefix+"unknown operation");
        check(layer.target_kind==FinishTargetKind::object||layer.target_kind==FinishTargetKind::field,prefix+"unknown target type");
        check(layer.target_id!=0&&target_mask(layer,object,fields)!=0,prefix+"missing object/field reference "+std::to_string(layer.target_id)+"; existing cloud retained");
        check(range(layer.strength,0,1)&&range(layer.density_multiplier,0,4),prefix+"strength/multiplier outside supported range");
        check(finite(layer.mask.center)&&std::max({std::abs(layer.mask.center.x),std::abs(layer.mask.center.y),std::abs(layer.mask.center.z)})<=100000,prefix+"mask center outside local coordinate budget");
        check(finite(layer.mask.radii)&&layer.mask.radii.x>=.1&&layer.mask.radii.y>=.1&&layer.mask.radii.z>=.1&&std::max({layer.mask.radii.x,layer.mask.radii.y,layer.mask.radii.z})<=10000,prefix+"mask radii outside [.1,10000] metres");
        check(range(layer.mask.falloff,.01,1000),prefix+"mask feather outside [.01,1000] metres");
        const double coordinate=std::max({1.,std::abs(layer.mask.center.x),std::abs(layer.mask.center.y),std::abs(layer.mask.center.z),layer.mask.radii.x,layer.mask.radii.y,layer.mask.radii.z});
        check(coordinate*std::numeric_limits<float>::epsilon()*8/std::max(.01,layer.mask.falloff)<=.002,prefix+"mask exceeds GPU coordinate precision budget");
    }
    return errors;
}
double ellipsoid_mask_weight(const EllipsoidMask& mask,Vec3 p){const auto d=distance(mask,p);return 1-smooth(d/mask.falloff);}
ModifierFieldScales evaluate_finish_stack(const FinishStack& stack,Vec3 p,Id object,std::span<const Id> fields){
    ModifierFieldScales result;std::array<bool,2> hard{};
    for(const auto& layer:stack.layers){if(!layer.enabled||layer.strength==0)continue;
        const auto target=target_mask(layer,object,fields);const double weight=ellipsoid_mask_weight(layer.mask,p),amount=layer.strength*weight;
        for(std::size_t i=0;i<fields.size();++i)if(target&(1u<<i)){
            if(layer.kind==FinishModifierKind::cut){result.density[i]=std::max(0.,result.density[i]-amount);hard[i]=hard[i]||(layer.hard_cut&&layer.strength==1&&distance(layer.mask,p)<=0);}
            else if(layer.kind==FinishModifierKind::density)result.density[i]*=1+(layer.density_multiplier-1)*amount;
            else result.detail[i]*=1-amount;
        }
    }
    for(std::size_t i=0;i<fields.size();++i)if(hard[i])result.density[i]=0;
    return result;
}
double finish_density_bound(const FinishStack& stack){double bound=1;for(const auto& layer:stack.layers)if(layer.enabled&&layer.kind==FinishModifierKind::density)bound*=std::max(1.,1+(layer.density_multiplier-1)*layer.strength);return bound;}
std::uint64_t finish_stack_hash(const FinishStack& stack){
    std::uint64_t hash=14695981039346656037ull;auto word=[&](std::uint64_t value){for(unsigned i=0;i<8;++i)hash=(hash^static_cast<unsigned char>(value>>(i*8)))*1099511628211ull;};
    auto number=[&](double v){word(std::bit_cast<std::uint64_t>(v==0?0.:v));};auto vector=[&](Vec3 p){number(p.x);number(p.y);number(p.z);};
    word(stack.contract_version);word(stack.layers.size());for(const auto& l:stack.layers){word(l.id);word(unsigned(l.kind));word(l.enabled);number(l.strength);vector(l.mask.center);vector(l.mask.radii);number(l.mask.falloff);word(unsigned(l.target_kind));word(l.target_id);number(l.density_multiplier);word(l.hard_cut);}return hash;
}
GpuFinishStack gpu_finish_stack(const FinishStack& stack,Id object,std::span<const Id> fields){
    require(validate_finish_stack(stack,object,fields));GpuFinishStack out;out.settings={float(stack.layers.size()),float(finish_density_bound(stack)),0,0};
    for(std::size_t i=0;i<stack.layers.size();++i){const auto& l=stack.layers[i];auto& g=out.layers[i];g.center={float(l.mask.center.x),float(l.mask.center.y),float(l.mask.center.z),float(l.mask.falloff)};g.radii={float(l.mask.radii.x),float(l.mask.radii.y),float(l.mask.radii.z),0};g.operation={float(unsigned(l.kind)),l.enabled?1.f:0.f,float(l.strength),float(l.density_multiplier)};g.target={float(target_mask(l,object,fields)),l.hard_cut?1.f:0.f,0,0};}return out;
}
Id next_finish_id(const FinishStack& stack){Id value=0;for(const auto& l:stack.layers)value=std::max(value,l.id);if(value==std::numeric_limits<Id>::max())throw std::invalid_argument("Modifier stable-ID namespace exhausted");return value+1;}
FinishStack finish_stack_command(FinishStack stack,const FinishCommand& command){
    auto found=std::find_if(stack.layers.begin(),stack.layers.end(),[&](const auto& l){return l.id==command.id;});
    if(command.kind==FinishCommandKind::add){auto value=command.value;if(value.id==0)value.id=next_finish_id(stack);stack.layers.push_back(value);}
    else {if(found==stack.layers.end())throw std::invalid_argument("Unknown finishing layer stable ID");
        const auto index=std::size_t(found-stack.layers.begin());
        switch(command.kind){
        case FinishCommandKind::replace:if(command.value.id!=command.id)throw std::invalid_argument("Layer replacement cannot change stable ID");*found=command.value;break;
        case FinishCommandKind::duplicate:{auto value=*found;value.id=next_finish_id(stack);stack.layers.insert(stack.layers.begin()+index+1,value);break;}
        case FinishCommandKind::erase:stack.layers.erase(found);break;
        case FinishCommandKind::move_up:if(index>0)std::swap(stack.layers[index],stack.layers[index-1]);break;
        case FinishCommandKind::move_down:if(index+1<stack.layers.size())std::swap(stack.layers[index],stack.layers[index+1]);break;
        default:throw std::invalid_argument("Unknown finishing stack command");
        }
    }
    if(stack.layers.size()>max_finish_modifiers)throw std::invalid_argument("Four-layer finishing budget exceeded");
    return stack;
}
}
