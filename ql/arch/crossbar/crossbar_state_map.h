/**
 * @file   crossbar_state_map.h
 * @date   05/2019
 * @author Alejandro Morais
 * @brief  Crossbar state mao
 */

#ifndef QL_CROSSBAR_STATE_MAP_H
#define QL_CROSSBAR_STATE_MAP_H

#include <ql/arch/crossbar/crossbar_state.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_state_map_t
{
public:
    size_t max_cycle;
    std::map<size_t, crossbar_state_t*> crossbar_states;
    
    crossbar_state_map_t(size_t max_cycle)
    {
        this->max_cycle = max_cycle;
    }
    
    crossbar_state_t* get(size_t index)
    {
        return crossbar_states[index];
    }
    
    void insert(size_t index, crossbar_state_t* value)
    {
        crossbar_states[index] = value;
    }
    
    crossbar_state_t* get_last_crossbar_state(size_t cycle, scheduling_direction_t direction)
    {
        std::map<size_t, crossbar_state_t*>::iterator it;
        if (direction == ql::forward_scheduling)
        {
            for (size_t i = cycle; i >= 0; i--)
            {
                it = crossbar_states.find((size_t) i);
                if (it != crossbar_states.end())
                {
                    return it->second;
                }
            }
        }
        else
        {
            for (size_t i = cycle; i <= this->max_cycle; i++)
            {
                it = crossbar_states.find((size_t) i);
                if (it != crossbar_states.end())
                {
                    return it->second;
                }
            }
        }
        
        return NULL;
    }
    
};

}
}
}

#endif // CROSSBAR_STATE_MAP_H
