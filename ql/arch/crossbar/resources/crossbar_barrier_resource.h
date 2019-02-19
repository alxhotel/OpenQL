/**
 * @file   crossbar_barrier_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Barrier resource
 */

#ifndef QL_CROSSBAR_BARRIER_RESOURCE_H
#define QL_CROSSBAR_BARRIER_RESOURCE_H

#include <vector>
#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>
#include <ql/arch/crossbar/crossbar_resource_manager.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

typedef enum {
    lowered = 0,
    raised = 1,
    neutral = 2
} barrier_state_t;

/**
 * Horizontal & Vertical barriers resource type
 */
class crossbar_barrier_resource_t : public resource_t
{
public:
    crossbar_state_t * crossbar_state;
    
    // Vertical barrier
    std::vector<size_t> vertical_barrier_busy;

    // Horizontal barrier
    std::vector<size_t> horizontal_barrier_busy;
    
    crossbar_barrier_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_t * crossbar_state_local)
    : resource_t("barrier", dir), crossbar_state(crossbar_state_local)
    {
        count = (crossbar_state->board_state.size() - 1);
        vertical_barrier_busy.reserve(count);
        horizontal_barrier_busy.reserve(count);
        
        for (size_t i = 0; i < count; i++)
        {
            vertical_barrier_busy[i] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
            horizontal_barrier_busy[i] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
        }
    }

    crossbar_barrier_resource_t* clone() const & { return new crossbar_barrier_resource_t(*this);}
    crossbar_barrier_resource_t* clone() && { return new crossbar_barrier_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (direction == backward_scheduling) EOUT("Backwards scheduling not implemented");
        
        int n = crossbar_state->board_state.size();
        std::pair<int, int> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {                
                // Barrier at the left
                if (pos_a.second - 1 >= 0 && op_start_cycle < vertical_barrier_busy[pos_a.second - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the right
                if (pos_a.second < n - 1 && op_start_cycle < vertical_barrier_busy[pos_a.second])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    middle_barrier = pos_a.first;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    middle_barrier = pos_a.first - 1;
                }
                
                // Barrier between qubit and destination
                if (op_start_cycle < horizontal_barrier_busy[middle_barrier])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the top
                if (middle_barrier + 1 < n - 1 && op_start_cycle < horizontal_barrier_busy[middle_barrier + 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the bottom
                if (middle_barrier - 1 >= 0 && op_start_cycle < horizontal_barrier_busy[middle_barrier - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {                
                // Barrier at the top
                if (pos_a.first < n - 1 && op_start_cycle < horizontal_barrier_busy[pos_a.first])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the bottom
                if (pos_a.first - 1 >= 0 && op_start_cycle < horizontal_barrier_busy[pos_a.first - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                if (op_start_cycle < vertical_barrier_busy[middle_barrier])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the left
                if (middle_barrier - 1 >= 0 && op_start_cycle < vertical_barrier_busy[middle_barrier - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the right
                if (middle_barrier + 1 < n - 1 && op_start_cycle < vertical_barrier_busy[middle_barrier + 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                // Barrier at the top
                if (pos_a.first < n - 1 && op_start_cycle < horizontal_barrier_busy[pos_a.first])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the bottom
                if (pos_a.first - 1 >= 0 && op_start_cycle < horizontal_barrier_busy[pos_a.first - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                if (op_start_cycle < vertical_barrier_busy[middle_barrier])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the left
                if (middle_barrier - 1 >= 0 && op_start_cycle < vertical_barrier_busy[middle_barrier - 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Barrier at the right
                if (middle_barrier + 1 < n - 1 && op_start_cycle < vertical_barrier_busy[middle_barrier + 1])
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else
            {
                // Single gate
                for (size_t i = 0; i < count; i++)
                {
                    if (op_start_cycle < vertical_barrier_busy[i])
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                    if (op_start_cycle < horizontal_barrier_busy[i])
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // SQSWAP
            std::pair<int, int> pos_b = crossbar_state->positions[ins->operands[1]];
            int column = pos_a.second;
            int middle_barrier = std::min(pos_a.first, pos_b.first);
            
            // Barrier between qubits
            if (op_start_cycle < horizontal_barrier_busy[middle_barrier])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Barrier at the left
            if (column - 1 >= 0 && op_start_cycle < vertical_barrier_busy[column - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Barrier at the right
            if (column < n - 1 && op_start_cycle < vertical_barrier_busy[column])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Barrier at the top
            if (middle_barrier + 1 < n - 1 && op_start_cycle < horizontal_barrier_busy[middle_barrier + 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Barrier at the bottom
            if (middle_barrier - 1 >= 0 && op_start_cycle < horizontal_barrier_busy[middle_barrier - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        else if (instruction_type.compare("measurement_gate") == 0)
        {
            // Measurement
            
            // --------------------------------
            // 1. Barriers for the first phase
            // --------------------------------
            
            // Barrier at the top
            if (pos_a.first < n - 1 && op_start_cycle < horizontal_barrier_busy[pos_a.first])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the bottom
            if (pos_a.first - 1 >= 0 && op_start_cycle < horizontal_barrier_busy[pos_a.first - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            int middle_barrier = 0;
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                middle_barrier = pos_a.second - 1;
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.second;
            }
            
            // Barrier between data and ancilla
            if (op_start_cycle < vertical_barrier_busy[middle_barrier])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the left
            if (middle_barrier - 1 >= 0 && op_start_cycle < vertical_barrier_busy[middle_barrier - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the right
            if (middle_barrier + 1 < n - 1 && op_start_cycle < vertical_barrier_busy[middle_barrier + 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // ---------------------------------
            // 2. Barriers for the second phase
            // ---------------------------------
            
            // Barrier at the left
            if (pos_a.second - 1 >= 0 && op_start_cycle < vertical_barrier_busy[pos_a.second - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the right
            if (pos_a.second < n - 1 && op_start_cycle < vertical_barrier_busy[pos_a.second])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                middle_barrier = pos_a.first;
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.first - 1;
            }
            
            // Barrier between sites
            if (op_start_cycle < horizontal_barrier_busy[middle_barrier])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the top
            if (middle_barrier + 1 >= 0 && op_start_cycle < horizontal_barrier_busy[middle_barrier + 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }

            // Barrier at the bottom
            if (middle_barrier - 1 < n - 1 && op_start_cycle < horizontal_barrier_busy[middle_barrier - 1])
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        
        DOUT("    " << name << " resource available ...");
        return true;
    }

    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (direction == backward_scheduling) EOUT("Backwards scheduling not implemented");

        int n = crossbar_state->board_state.size();
        std::pair<int, int> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                // Barrier at the left
                if (pos_a.second - 1 >= 0)
                {
                    vertical_barrier_busy[pos_a.second - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the right
                if (pos_a.second < n - 1)
                {
                    vertical_barrier_busy[pos_a.second] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    middle_barrier = pos_a.first;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    middle_barrier = pos_a.first - 1;
                }
                
                // Barrier between qubit and destination
                horizontal_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                
                // Barrier at the top
                if (middle_barrier + 1 < n - 1)
                {
                    horizontal_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the bottom
                if (middle_barrier - 1 >= 0)
                {
                    horizontal_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {                
                // Barrier at the top
                if (pos_a.first < n - 1)
                {
                    horizontal_barrier_busy[pos_a.first] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the bottom
                if (pos_a.first - 1 >= 0)
                {
                    horizontal_barrier_busy[pos_a.first - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                vertical_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                
                // Barrier at the left
                if (middle_barrier - 1 >= 0)
                {
                    vertical_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the right
                if (middle_barrier + 1 < n - 1)
                {
                    vertical_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                // Barrier at the top
                if (pos_a.first < n - 1)
                {
                    horizontal_barrier_busy[pos_a.first] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the bottom
                if (pos_a.first - 1 >= 0)
                {
                    horizontal_barrier_busy[pos_a.first - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                int middle_barrier = 0;
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                vertical_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                
                // Barrier at the left
                if (middle_barrier - 1 >= 0)
                {
                    vertical_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
                
                // Barrier at the right
                if (middle_barrier + 1 < n - 1)
                {
                    vertical_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
            }
            else
            {
                // Single gate
                for (size_t i = 0; i < count; i++)
                {
                    vertical_barrier_busy[i] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                    horizontal_barrier_busy[i] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // SQSWAP
            std::pair<int, int> pos_b = crossbar_state->positions[ins->operands[1]];
            int column = pos_a.second;
            int middle_barrier = std::min(pos_a.first, pos_b.first);

            // Barrier between qubits
            horizontal_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            
            // Barriers at the left
            if (column > 0)
            {
                horizontal_barrier_busy[column - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            // Barrier at the right
            if (column < n - 1)
            {
                horizontal_barrier_busy[column] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            // Barriers at the top
            if (middle_barrier < n - 2)
            {
                vertical_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            // Barrier at the bottom
            if (middle_barrier > 0)
            {
                vertical_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
        }
        else if (instruction_type.compare("measurement_gate") == 0)
        {
            // Measurement
            
            // --------------------------------
            // 1. Barriers for the first phase
            // --------------------------------
            
            // Barrier at the top
            if (pos_a.first < n - 1)
            {
                horizontal_barrier_busy[pos_a.first] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }

            // Barrier at the bottom
            if (pos_a.first - 1 >= 0)
            {
                horizontal_barrier_busy[pos_a.first - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            int middle_barrier = 0;
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                middle_barrier = pos_a.second - 1;
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.second;
            }
            
            // Barrier between data and ancilla
            vertical_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            
            // Barrier at the left
            if (middle_barrier - 1 >= 0)
            {
                vertical_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }

            // Barrier at the right
            if (middle_barrier + 1 < n - 1)
            {
                vertical_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            // --------------------------------
            // 2. Barriers for the second phase
            // --------------------------------
            
            // Barrier at the left
            if (pos_a.second - 1 >= 0)
            {
                vertical_barrier_busy[pos_a.second - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }

            // Barrier at the right
            if (pos_a.second < n - 1)
            {
                vertical_barrier_busy[pos_a.second] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
            
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                middle_barrier = pos_a.first;
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.first - 1;
            }
            
            // Barrier between data and ancilla
            horizontal_barrier_busy[middle_barrier] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            
            // Barrier at the top
            if (middle_barrier + 1 >= 0)
            {
                horizontal_barrier_busy[middle_barrier + 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }

            // Barrier at the bottom
            if (middle_barrier - 1 < n - 1)
            {
                horizontal_barrier_busy[middle_barrier - 1] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            }
        }
    }
    ~crossbar_barrier_resource_t() {}
};

}
}
}

#endif // QL_CROSSBAR_BARRIER_RESOURCE_H
