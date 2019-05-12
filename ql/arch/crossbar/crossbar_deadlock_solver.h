/**
 * @file   crossbar_deadlock_solver.h
 * @date   05/2019
 * @author Alejandro Morais
 * @brief  Crossbar deadlock solver
 */

#ifndef CROSSBAR_DEADLOCK_SOLVER_H
#define CROSSBAR_DEADLOCK_SOLVER_H

#include <ql/arch/crossbar/crossbar_state.h>
#include <ql/arch/crossbar/crossbar_state_map.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_deadlock_solver_t
{
public:
    
    crossbar_state_map_t* crossbar_state_map;
    scheduling_direction_t direction;
    
    crossbar_deadlock_solver_t(scheduling_direction_t direction_local,
        crossbar_state_map_t* crossbar_state_map_local)
    {
        this->direction = direction_local;
        this->crossbar_state_map = crossbar_state_map_local;
    }
    
    void solve_deadlock(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // TODO
        
        std::vector<crossbar_state_t*> crossbar_states_cache;
        while (this->has_deadlock(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration))
        {
            // Try to solve the deadlock
            DOUT("Trying to solve deadlock in resource manager");
            
            // Check if we have not passed through this state
            crossbar_state_t* last_crossbar_state = crossbar_state_map->get_last_crossbar_state(op_start_cycle, this->direction);
            for (auto& crossbar_state_local : crossbar_states_cache)
            {
                if (crossbar_state_local->equals(last_crossbar_state))
                {
                    DOUT("Crossbar state already seen");
                    DOUT("TODO: try other strategies");
                    break;
                }
            }
            crossbar_states_cache.push_back(last_crossbar_state);
            
            // Get the conflicting sites
            std::vector<size_t> conflicting_sites = this->get_conflicting_sites(op_start_cycle, ins, operation_name,
                operation_type, instruction_type, operation_duration);
            
            // Shuttle in the same column
            size_t site_a = conflicting_sites[0];
            size_t site_b = conflicting_sites[1];
            std::pair<size_t, size_t> pos_a = last_crossbar_state->get_pos_by_site(site_a);
            std::pair<size_t, size_t> pos_b = last_crossbar_state->get_pos_by_site(site_b);
            
            // Search for preferences            
            if (pos_a.first == pos_b.first)
            {
                // Horizontally adjacent
                
                if (site_a == ins->operands[0])
                {
                    // Shuttle second operand away
                    
                }
                else if (site_b == ins->operands[0])
                {
                    // Shuttle first operand away
                    
                }
                else
                {
                    
                }
            }
            else if (pos_a.second == pos_b.second)
            {
                DOUT("Solving vertically adjacent");
                
                // Vertically adjacent
                if (site_a == ins->operands[0])
                {
                    // Shuttle second operand away
                    
                    DOUT("Site A");
                    
                }
                else if (site_b == ins->operands[0])
                {
                    DOUT("Site B");
                    
                    // Shuttle first operand away
                    if (pos_a.first + 1 < last_crossbar_state->get_y_size()
                        && last_crossbar_state->get_count_by_position(pos_a.first + 1, pos_a.second) == 0)
                    {
                        DOUT("Shuttling UP");
                        // SHUTTLE B UP
                        size_t qubit_index = last_crossbar_state->get_qubit_by_site(site_a);
                        last_crossbar_state->shuttle_up(qubit_index);
                        
                        last_crossbar_state->print();
                    }
                    else if (pos_a.first - 1 >= 0
                        && last_crossbar_state->get_count_by_position(pos_a.first - 1, pos_a.second) == 0)
                    {
                        DOUT("Shuttling DOWN");
                        // SHUTTLE B DOWN
                        last_crossbar_state->shuttle_down(site_a);
                    }
                    else
                    {
                        DOUT("ELSE");
                    }
                }
                else
                {
                    
                }
            }
            
            break;
        }
    }

private:
    
    bool has_deadlock(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        std::vector<size_t> conflicting_sites = this->get_conflicting_sites(op_start_cycle, ins, operation_name,
                operation_type, instruction_type, operation_duration);
        
        return conflicting_sites.size() > 0;
    }
    
    std::vector<size_t> get_conflicting_sites(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        std::vector<size_t> conflicting_sites;
        
        // Check if two qubits are adjacent AND current instruction influence them
        // This also checks if we are shuttling to a non-empty site
        crossbar_state_t* last_crossbar_state = crossbar_state_map->get_last_crossbar_state(op_start_cycle, direction);
        std::pair<size_t, size_t> pos_a = last_crossbar_state->get_pos_by_site(ins->operands[0]);
        
        if (operation_name.find("_up") == std::string::npos
            || operation_name.find("_down") == std::string::npos)
        {
            int top = 0;
            int bottom = 0;
            if (direction == ql::forward_scheduling)
            {
                if (operation_name.find("_up") != std::string::npos)
                {
                    top = pos_a.first + 1;
                    bottom = pos_a.first;
                }
                else
                {   
                    top = pos_a.first;
                    bottom = pos_a.first - 1;
                }
            }
            else
            {
                if (operation_name.find("_up") != std::string::npos)
                {
                    top = pos_a.first;
                    bottom = pos_a.first - 1;
                }
                else
                {
                    top = pos_a.first + 1;
                    bottom = pos_a.first;
                }
            }
            
            for (size_t j = 0; j < last_crossbar_state->get_x_size(); j++)
            {
                if (last_crossbar_state->get_count_by_position(top, j) > 0
                    && last_crossbar_state->get_count_by_position(bottom, j) > 0)
                {
                    conflicting_sites.push_back(last_crossbar_state->get_site_by_pos(top, j));
                    conflicting_sites.push_back(last_crossbar_state->get_site_by_pos(bottom, j));
                }
            }
        }
        else if (operation_name.find("_left") == std::string::npos
            || operation_name.find("_right") == std::string::npos)
        {
            int left = 0;
            int right = 0;
            if (direction == ql::forward_scheduling)
            {
                if (operation_name.find("_left") != std::string::npos)
                {
                    left = pos_a.second - 1;
                    right = pos_a.second;
                }
                else
                {
                    left = pos_a.second;
                    right = pos_a.second + 1;
                }
            }
            else
            {
                if (operation_name.find("_left") != std::string::npos)
                {
                    left = pos_a.second;
                    right = pos_a.second + 1;
                }
                else
                {
                    left = pos_a.second - 1;
                    right = pos_a.second;
                }
            }
            
            for (size_t i = 0; i < last_crossbar_state->get_y_size(); i++)
            {
                if (last_crossbar_state->get_count_by_position(i, left) > 0
                    && last_crossbar_state->get_count_by_position(i, right) > 0)
                {
                    conflicting_sites.push_back(last_crossbar_state->get_site_by_pos(i, left));
                    conflicting_sites.push_back(last_crossbar_state->get_site_by_pos(i, right));
                }
            }
        }
        
        return conflicting_sites;
    }
    
};

}
}
}

#endif // CROSSBAR_DEADLOCK_SOLVER_H
