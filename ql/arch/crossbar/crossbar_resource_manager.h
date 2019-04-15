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
#include <ql/arch/crossbar/resources/interval_tree.h>
#include <ql/arch/crossbar/resources/crossbar_qubit_resource.h>
#include <ql/arch/crossbar/resources/crossbar_site_resource.h>
#include <ql/arch/crossbar/resources/crossbar_barrier_resource.h>
#include <ql/arch/crossbar/resources/crossbar_qubit_line_resource.h>
#include <ql/arch/crossbar/resources/crossbar_wave_resource.h>

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
    std::map<size_t, crossbar_state_t*> crossbar_states;
    
    crossbar_resource_manager_t() : resource_manager_t() {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform)
        : resource_manager_t(platform, forward_scheduling) {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform, ql::scheduling_direction_t dir)
        : resource_manager_t(platform, dir)
    {
        crossbar_state_t* initial_crossbar_state;
        // Initialize the board state
        if (platform.topology.count("x_size") > 0
            && platform.topology.count("y_size") > 0)
        {
            initial_crossbar_state = new crossbar_state_t(
                platform.topology["y_size"],
                platform.topology["x_size"]
            );
        }
        else
        {
            COUT("Error: Grid topology for the crossbar was not defined");
            throw ql::exception("[x] Error: Grid topology for the crossbar was not defined!", false);
        }
        
        // Initialize the configuration
        if (platform.topology.count("init_configuration") > 0)
        {
            for (json::const_iterator it = platform.topology["init_configuration"].begin();
                it != platform.topology["init_configuration"].end(); ++it)
            {
                int key = std::stoi(it.key());
                std::string type = it.value()["type"];
                std::vector<int> value = it.value()["position"];
                initial_crossbar_state->add_qubit(value[0], value[1], key, (type.compare("ancilla") == 0));
            }
        }
        else
        {
            COUT("Error: Qubit init placement for the crossbar were not defined");
            throw ql::exception("[x] Error: Qubit init placement for the crossbar were not defined!", false);
        }
        
        // Add initial placement to the current state
        crossbar_states[(unsigned int) 0] = initial_crossbar_state;
        
        DOUT("New crossbar_resource_manager_t for direction " << dir << " with num of resources: " << platform.resources.size() );
        for (json::const_iterator it = platform.resources.begin(); it != platform.resources.end(); ++it)
        {
            std::string key = it.key();

            if (key == "qubits")
            {
                resource_t * qubit_resource = new crossbar_qubit_resource_t(platform, dir, crossbar_states);
                resource_ptrs.push_back(qubit_resource);
            }
            else if (key == "barriers")
            {
                resource_t * barrier_resource = new crossbar_barrier_resource_t(platform, dir, crossbar_states);
                resource_ptrs.push_back(barrier_resource);
            }
            else if (key == "qubit_lines")
            {
                resource_t * qubit_line_resource = new crossbar_qubit_line_resource_t(platform, dir, crossbar_states);
                resource_ptrs.push_back(qubit_line_resource);
            }
            else if (key == "wave")
            {
                resource_t * wave_resource = new crossbar_wave_resource_t(platform, dir, crossbar_states);
                resource_ptrs.push_back(wave_resource);
            }
            else if (key == "sites")
            {
                resource_t * site_resource = new crossbar_site_resource_t(platform, dir, crossbar_states);
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
            size_t qubit_index = ins->operands[0];
            
            // Get the last crossbar state
            crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle + operation_duration);
            crossbar_state_t* crossbar_state = last_crossbar_state->clone();
            
            if (operation_name.compare("shuttle_up") == 0)
            {
                crossbar_state->shuttle_up(qubit_index);
            }
            else if (operation_name.compare("shuttle_down") == 0)
            {
                crossbar_state->shuttle_down(qubit_index);
            }
            else if (operation_name.compare("shuttle_left") == 0)
            {
                crossbar_state->shuttle_left(qubit_index);
            }
            else if (operation_name.compare("shuttle_right") == 0)
            {
                crossbar_state->shuttle_right(qubit_index);
            }
            
            // Insert into the interval tree
            crossbar_states[op_start_cycle + operation_duration] = crossbar_state;
        }
    }
    
    crossbar_state_t* get_last_crossbar_state(size_t cycle)
    {
        std::map<size_t, crossbar_state_t*>::iterator it;
        for (int i = cycle; i >= 0; i--)
        {
            it = crossbar_states.find((size_t) i);
            if (it != crossbar_states.end())
            {
                return it->second;
            }
        }
    }
    
    bool has_dead_lock(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // TODO
        return false;
    }
    
    void solve_dead_lock(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // TODO
        while (this->has_dead_lock(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration))
        {
            // Try to solve the deadlock
            break;
        }
    }
    
    ~crossbar_resource_manager_t()
    {
    }
};

}
}
}

#endif // QL_CROSSBAR_RESOURCE_MANAGER_H
