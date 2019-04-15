/**
 * @file   crossbar_resource.h
 * @date   03/2019
 * @author Alejandro Morais
 * @brief  Crossbar resource
 */

#ifndef CROSSBAR_RESOURCE_H
#define CROSSBAR_RESOURCE_H

#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>

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
    std::map<size_t, crossbar_state_t*> crossbar_states;
    
    crossbar_resource_t(std::string name, scheduling_direction_t dir)
        : resource_t(name, dir)
    {
    }
    
    crossbar_resource_t(std::string name, scheduling_direction_t dir,
        std::map<size_t, crossbar_state_t*> & crossbar_states_local)
        : resource_t(name, dir)
    {
        crossbar_states = crossbar_states_local;
        m = get_first_crossbar_state()->get_y_size();
        n = get_first_crossbar_state()->get_x_size();
    }
    
    //crossbar_resource_t* clone() const & { return new crossbar_resource_t(*this);}
    //crossbar_resource_t* clone() && { return new crossbar_resource_t(std::move(*this)); }
    
    crossbar_state_t* get_first_crossbar_state()
    {
        return crossbar_states[(unsigned int) 0];
    }
    
    crossbar_state_t* get_last_crossbar_state(size_t curr_cycle)
    {
        std::map<size_t, crossbar_state_t*>::iterator it;
        for (int i = curr_cycle; i >= 0; i--)
        {
            it = crossbar_states.find((size_t) i);
            if (it != crossbar_states.end())
            {
                return it->second;
            }
        }
        
        return NULL;
    }
    
};

}
}
}

#endif // CROSSBAR_RESOURCE_H
