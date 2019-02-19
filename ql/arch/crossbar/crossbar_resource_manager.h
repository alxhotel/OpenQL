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
#include <ql/arch/crossbar/resources/crossbar_qubit_resource.h>
#include <ql/arch/crossbar/resources/crossbar_barrier_resource.h>

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
    crossbar_state_t* crossbar_state;
    
    crossbar_resource_manager_t() : resource_manager_t() {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform) : resource_manager_t(platform, forward_scheduling) {}
    
    crossbar_resource_manager_t(const ql::quantum_platform & platform, ql::scheduling_direction_t dir) : resource_manager_t(platform, dir)
    {
        crossbar_state = new crossbar_state_t();
        // Initialize the positions
        if (platform.topology.count("positions") > 0)
        {
            if (platform.topology["positions"].size() == platform.qubit_number)
            {
                for (json::const_iterator it = platform.topology["positions"].begin(); it != platform.topology["positions"].end(); ++it)
                {
                    int key = std::stoi(it.key());
                    std::vector<int> value = it.value();
                    crossbar_state->positions[key] = std::make_pair(value[0], value[1]);
                }
            }
            else
            {
                COUT("Error: The number of positions defined is not equal to the amount of qubits");
                throw ql::exception("[x] Error: The number of positions defined is not equal to the amount of qubits!", false);
            }
        }
        else
        {
            COUT("Error: Qubit positions for the crossbar were not defined");
            throw ql::exception("[x] Error: Qubit positions for the crossbar were not defined!", false);
        }
        
        // Initialize the board state
        if (platform.topology.count("grid_size_x") > 0
            && platform.topology.count("grid_size_y") > 0)
        {
            for (int i = 0; i < platform.topology["grid_size_y"]; i++)
            {
                crossbar_state->board_state[i] = {};
                for (int j = 0; j < platform.topology["grid_size_x"]; j++)
                {
                    crossbar_state->board_state[i][j] = -1;
                }
            }
        }
        else
        {
            COUT("Error: Grid topology for the crossbar was not defined");
            throw ql::exception("[x] Error: Grid topology for the crossbar was not defined!", false);
        }
        
        DOUT("New crossbar_resource_manager_t for direction " << dir << " with num of resources: " << platform.resources.size() );
        for (json::const_iterator it = platform.resources.begin(); it != platform.resources.end(); ++it)
        {
            std::string key = it.key();

            if (key == "qubits")
            {
                resource_t * qubit_resource = new crossbar_qubit_resource_t(platform, dir);
                resource_ptrs.push_back(qubit_resource);
            }
            else if (key == "barriers")
            {
                resource_t * barrier_resource = new crossbar_barrier_resource_t(platform, dir, crossbar_state);
                resource_ptrs.push_back(barrier_resource);
            }
            else
            {
                COUT("Error : Un-modelled resource: '" << key << "'");
                throw ql::exception("[x] Error : Un-modelled resource: " + key + " !", false);
            }
        }
    }
    
    ~crossbar_resource_manager_t()
    {
        free(crossbar_state);
    }
};

}
}
}

#endif // QL_CROSSBAR_RESOURCE_MANAGER_H
