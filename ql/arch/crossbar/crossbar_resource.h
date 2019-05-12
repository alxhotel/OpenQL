/**
 * @file   crossbar_resource.h
 * @date   03/2019
 * @author Alejandro Morais
 * @brief  Crossbar resource
 */

#ifndef CROSSBAR_RESOURCE_H
#define CROSSBAR_RESOURCE_H

#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state_map.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_resource_t : public resource_t
{
public:
    size_t m;
    size_t n;
    crossbar_state_map_t* crossbar_state_map;
    
    crossbar_resource_t(std::string name, scheduling_direction_t dir)
        : resource_t(name, dir)
    {
    }
    
    crossbar_resource_t(std::string name, scheduling_direction_t dir,
        crossbar_state_map_t* crossbar_state_map_local)
        : resource_t(name, dir)
    {
        crossbar_state_map = crossbar_state_map_local;
        m = get_any_crossbar_state()->get_y_size();
        n = get_any_crossbar_state()->get_x_size();
    }
    
    //crossbar_resource_t* clone() const & { return new crossbar_resource_t(*this);}
    //crossbar_resource_t* clone() && { return new crossbar_resource_t(std::move(*this)); }
    
    crossbar_state_t* get_any_crossbar_state()
    {
        std::map<size_t, crossbar_state_t*>& crossbar_states = crossbar_state_map->crossbar_states;
        return (crossbar_states.begin()->second);
    }
    
    crossbar_state_t* get_last_crossbar_state(size_t curr_cycle)
    {
        return crossbar_state_map->get_last_crossbar_state(curr_cycle, this->direction);
    }
    
};

}
}
}

#endif // CROSSBAR_RESOURCE_H
