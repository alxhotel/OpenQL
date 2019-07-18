/**
 * @file   crossbar_resource_manager.h
 * @date   01/2019
 * @author Alejandro Morais
 * @brief  Resource management for crossbar platform
 */

#ifndef QL_CROSSBAR_RESOURCE_MANAGER_H
#define QL_CROSSBAR_RESOURCE_MANAGER_H

#include <map>
#include <utility>
#include <vector>
#include <string>
#include <ql/json.h>
#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>
#include <ql/arch/crossbar/crossbar_state_map.h>
#include <ql/arch/crossbar/resources/interval_tree.h>
#include <ql/arch/crossbar/resources/crossbar_site_resource.h>
#include <ql/arch/crossbar/resources/crossbar_barrier_resource.h>
#include <ql/arch/crossbar/resources/crossbar_qubit_line_resource.h>
#include <ql/arch/crossbar/resources/crossbar_wave_resource.h>

#include "crossbar_deadlock_solver.h"

namespace ql
{
namespace arch
{
namespace crossbar
{

using json = nlohmann::json;

/**
 * Crossbar resource manager
 */
class crossbar_resource_manager_t : public resource_manager_t
{
public:
    /**
     * Current state of the position of qubits
     */
    crossbar_state_map_t* crossbar_state_map;
    
    crossbar_resource_manager_t() : resource_manager_t() {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform)
        : resource_manager_t(platform, forward_scheduling) {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform, ql::scheduling_direction_t dir,
        size_t max_cycle, crossbar_state_t* initial_crossbar_state, crossbar_state_t* final_crossbar_state)
        : resource_manager_t(platform, dir)
    {
        // Set attributes
        crossbar_state_map = new crossbar_state_map_t(max_cycle);
        
        // Add initial placement to the current state
        if (direction == ql::forward_scheduling)
        {
            crossbar_state_map->insert((unsigned int) 0, initial_crossbar_state);
        }
        else
        {
            crossbar_state_map->insert(crossbar_state_map->max_cycle, final_crossbar_state);
        }
        
        DOUT("New crossbar_resource_manager_t for direction " << dir << " with num of resources: " << platform.resources.size() );
        for (json::const_iterator it = platform.resources.begin(); it != platform.resources.end(); ++it)
        {
            std::string key = it.key();

            if (key == "barriers")
            {
                resource_t * barrier_resource = new crossbar_barrier_resource_t(platform, dir, crossbar_state_map);
                resource_ptrs.push_back(barrier_resource);
            }
            else if (key == "qubit_lines")
            {
                resource_t * qubit_line_resource = new crossbar_qubit_line_resource_t(platform, dir, crossbar_state_map);
                resource_ptrs.push_back(qubit_line_resource);
            }
            else if (key == "wave")
            {
                resource_t * wave_resource = new crossbar_wave_resource_t(platform, dir, crossbar_state_map);
                resource_ptrs.push_back(wave_resource);
            }
            else if (key == "sites")
            {
                resource_t * site_resource = new crossbar_site_resource_t(platform, dir, crossbar_state_map);
                resource_ptrs.push_back(site_resource);
            }
            else
            {
                COUT("Error : Un-modelled resource: '" << key << "'");
                throw ql::exception("[x] Error : Un-modelled resource: " + key + " !", false);
            }
        }
    }
    
    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // Call parent method
        resource_manager_t::reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration);
        
        // Update the crossbar state
        if (instruction_type.compare("shuttle") == 0)
        {
            std::map<size_t, crossbar_state_t*>& crossbar_states = crossbar_state_map->crossbar_states;
            
            // Get the last crossbar state
            size_t cycle_applied = (direction == ql::forward_scheduling)
                ? op_start_cycle + operation_duration
                : op_start_cycle;
            if (crossbar_states.find(cycle_applied) == crossbar_states.end())
            {
                // Add to last one
                crossbar_state_t* last_crossbar_state = crossbar_state_map->get_last_crossbar_state(cycle_applied, direction);
                crossbar_states[cycle_applied] = last_crossbar_state->clone();
            }
            
            // Apply shuttle to the future states
            for (auto const & entry : crossbar_states) {
                if (direction == ql::forward_scheduling)
                {
                    if (entry.first < cycle_applied)
                    {
                        // Don't apply to previous cycles
                        continue;
                    }
                }
                else
                {
                    if (entry.first > cycle_applied)
                    {
                        // Don't apply to next cycles
                        continue;
                    }
                }
                
                crossbar_state_t* crossbar_state = entry.second;
                size_t qubit_index = (direction == ql::forward_scheduling)
                    ? crossbar_state->get_qubit_by_site(ins->operands[0])
                    : crossbar_state->get_qubit_by_site(ins->operands[1]);

                std::cout << "Moving q[" << std::to_string(qubit_index) << "] from"
                        << " s[" << crossbar_state->get_site_by_qubit(qubit_index) << "]";

                if (operation_name.compare("shuttle_up") == 0)
                {
                    if (direction == ql::forward_scheduling)
                    {
                        crossbar_state->shuttle_up(qubit_index);
                    }
                    else
                    {
                        crossbar_state->shuttle_down(qubit_index);
                    }
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    if (direction == ql::forward_scheduling)
                    {
                        crossbar_state->shuttle_down(qubit_index);
                    }
                    else
                    {
                        crossbar_state->shuttle_up(qubit_index);
                    }
                }
                else if (operation_name.compare("shuttle_left") == 0)
                {
                    if (direction == ql::forward_scheduling)
                    {
                        crossbar_state->shuttle_left(qubit_index);
                    }
                    else
                    {
                        crossbar_state->shuttle_right(qubit_index);
                    }
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    if (direction == ql::forward_scheduling)
                    {
                        crossbar_state->shuttle_right(qubit_index);
                    }
                    else
                    {
                        crossbar_state->shuttle_left(qubit_index);
                    }
                }

                std::cout << " to s[" << crossbar_state->get_site_by_qubit(qubit_index) << "]"
                        << " at " << std::to_string(entry.first)
                        << std::endl << std::flush;

                // Insert into the interval tree
                crossbar_states[entry.first] = crossbar_state;
            }
        }
    }
    
    ~crossbar_resource_manager_t()
    {
    }
    
    void solve_deadlock(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        crossbar_deadlock_solver_t* crossbar_deadlock_solver = new crossbar_deadlock_solver_t(
            this->direction, this->crossbar_state_map
        );
        crossbar_deadlock_solver->solve_deadlock(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration);
    }
    
};

}
}
}

#endif // QL_CROSSBAR_RESOURCE_MANAGER_H
