/**
 * @file   crossbar_barrier_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Barrier resource
 */

#ifndef QL_CROSSBAR_BARRIER_RESOURCE_H
#define QL_CROSSBAR_BARRIER_RESOURCE_H

#include <vector>
#include <ql/arch/crossbar/crossbar_resource.h>
#include <ql/arch/crossbar/crossbar_state_map.h>
#include <ql/arch/crossbar/resources/interval_tree.h>

#include "crossbar_wave_resource.h"

namespace ql
{
namespace arch
{
namespace crossbar
{

typedef enum {
    lowered = 0,
    raised = 1
} barrier_state_t;

/**
 * Horizontal & Vertical barriers resource type
 */
class crossbar_barrier_resource_t : public crossbar_resource_t
{
public:
    std::vector<Intervals::IntervalTree<size_t, barrier_state_t>> vertical_barrier;
    std::vector<Intervals::IntervalTree<size_t, barrier_state_t>> horizontal_barrier;
    
    crossbar_barrier_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_map_t* crossbar_state_map_local)
        : crossbar_resource_t("barrier", dir, crossbar_state_map_local)
    {
        count = (n - 1);
        vertical_barrier.resize(count);
        horizontal_barrier.resize(count);
    }

    crossbar_barrier_resource_t* clone() const & { return new crossbar_barrier_resource_t(*this);}
    crossbar_barrier_resource_t* clone() && { return new crossbar_barrier_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, false))
        {
            DOUT("    " << name << " resource available ...");
            return true;
        }
        
        return false;
    }

    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, true);
    }

private:    
    bool check_vertical_barrier(size_t op_start_cycle, size_t operation_duration, size_t index, barrier_state_t new_state)
    {
        std::cout << "Check v[" << index << "]"
            << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration << std::endl << std::flush;
        
        if (index >= 0 && index <= n - 2)
        {
            const auto &intervals = vertical_barrier[index].findOverlappingIntervals(
                {op_start_cycle, op_start_cycle + operation_duration},
                false
            );
            
            // NOTE: Does not matter the direction of the scheduling
            for (const auto &interval : intervals)
            {
                if (interval.value != new_state)
                {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    bool check_horizontal_barrier(size_t op_start_cycle, size_t operation_duration, size_t index, barrier_state_t new_state)
    {
        std::cout << "Check h[" << index << "]"
            << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration << std::endl << std::flush;
        
        if (index >= 0 && index <= n - 2)
        {
            const auto &intervals = horizontal_barrier[index].findOverlappingIntervals(
                {op_start_cycle, op_start_cycle + operation_duration},
                false
            );
            
            // NOTE: Does not matter the direction of the scheduling
            for (const auto &interval : intervals)
            {
                if (interval.value != new_state)
                {
                    return false;
                }
            }
        }
        
        return true;
    }
    
    bool check_border_barriers_upwards(size_t op_start_cycle, size_t operation_duration,
        size_t i_index, size_t j_index, barrier_state_t new_state)
    {
        // Top
        if (!check_horizontal_barrier(op_start_cycle, operation_duration, i_index + 1, new_state))
        {
            return false;
        }
        
        // Bottom
        if (!check_horizontal_barrier(op_start_cycle, operation_duration, i_index - 1, new_state))
        {
            return false;
        }
        
        // Left
        if (!check_vertical_barrier(op_start_cycle, operation_duration, j_index - 1, new_state))
        {
            return false;
        }

        // Right
        if (!check_vertical_barrier(op_start_cycle, operation_duration, j_index, new_state))
        {
            return false;
        }
        
        return true;
    }
    
    bool check_border_barriers_rightwards(size_t op_start_cycle, size_t operation_duration,
        size_t i_index, size_t j_index, barrier_state_t new_state)
    {
        // Top
        if (!check_horizontal_barrier(op_start_cycle, operation_duration, i_index, new_state))
        {
            return false;
        }
     
        // Bottom
        if (!check_horizontal_barrier(op_start_cycle, operation_duration, i_index - 1, new_state))
        {
            return false;
        }
        
        // Left
        if (!check_vertical_barrier(op_start_cycle, operation_duration, j_index - 1, new_state))
        {
            return false;
        }

        // Right
        if (!check_vertical_barrier(op_start_cycle, operation_duration, j_index + 1, new_state))
        {
            return false;
        }
        
        return true;
    }
    
    void reserve_vertical_barrier(size_t op_start_cycle, size_t operation_duration, size_t index, barrier_state_t new_state)
    {
        if ((int)index >= 0 && index >= 0 && index <= n - 2)
        {
            std::cout << "Reserve v[" << index << "] from " << op_start_cycle << " to "
                    << op_start_cycle + operation_duration << std::endl << std::flush;
            
            vertical_barrier[index].insert({op_start_cycle, op_start_cycle + operation_duration, new_state});
        }
    }
    
    void reserve_horizontal_barrier(size_t op_start_cycle, size_t operation_duration, size_t index, barrier_state_t new_state)
    {
        if ((int)index >= 0 && index >= 0 && index <= n - 2)
        {
            std::cout << "Reserve h[" << index << "] from " << op_start_cycle << " to "
                    << op_start_cycle + operation_duration << std::endl << std::flush;
            
            horizontal_barrier[index].insert({op_start_cycle, op_start_cycle + operation_duration, new_state});
        }
    }
    
    void reserve_border_barrier_upwards(size_t op_start_cycle, size_t operation_duration,
        size_t i_index, size_t j_index, barrier_state_t new_state)
    {
        // Top
        reserve_horizontal_barrier(op_start_cycle, operation_duration, i_index + 1, new_state);
        
        // Bottom
        reserve_horizontal_barrier(op_start_cycle, operation_duration, i_index - 1, new_state);
        
        // Left
        reserve_vertical_barrier(op_start_cycle, operation_duration, j_index - 1, new_state);
        
        // Right
        reserve_vertical_barrier(op_start_cycle, operation_duration, j_index, new_state);
    }
    
    void reserve_border_barrier_rightwards(size_t op_start_cycle, size_t operation_duration,
        size_t i_index, size_t j_index, barrier_state_t new_state)
    {
        // Top
        reserve_horizontal_barrier(op_start_cycle, operation_duration, i_index, new_state);
        
        // Bottom
        reserve_horizontal_barrier(op_start_cycle, operation_duration, i_index - 1, new_state);
        
        // Left
        reserve_vertical_barrier(op_start_cycle, operation_duration, j_index - 1, new_state);
        
        // Right
        reserve_vertical_barrier(op_start_cycle, operation_duration, j_index + 1, new_state);
    }
    
    bool available_or_reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration,
        bool reserve)
    {
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        std::pair<size_t, size_t> pos_a = last_crossbar_state->get_pos_by_site(ins->operands[0]);
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                size_t middle_barrier = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    middle_barrier = pos_a.first;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    middle_barrier = pos_a.first - 1;
                }
                
                // Barrier between qubit and destination
                if (!check_horizontal_barrier(op_start_cycle, operation_duration,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Border barriers
                if (!check_border_barriers_upwards(op_start_cycle, operation_duration,
                    middle_barrier, pos_a.second, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_horizontal_barrier(op_start_cycle, operation_duration,
                            middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_upwards(op_start_cycle, operation_duration,
                            middle_barrier, pos_a.second, barrier_state_t::raised);
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t middle_barrier = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                if (!check_vertical_barrier(op_start_cycle, operation_duration,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Border barriers
                if (!check_border_barriers_rightwards(op_start_cycle, operation_duration,
                    pos_a.first, middle_barrier, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_vertical_barrier(op_start_cycle, operation_duration,
                            middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_rightwards(op_start_cycle, operation_duration,
                            pos_a.first, middle_barrier, barrier_state_t::raised);
                }
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            if (operation_name.rfind("_shuttle") != std::string::npos)
            {
                int middle_barrier = 0;
                if (operation_name.rfind("_shuttle_left") != std::string::npos)
                {
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.rfind("_shuttle_right") != std::string::npos)
                {
                    middle_barrier = pos_a.second;
                }
                
                // Barrier between qubit and destination
                if (!check_vertical_barrier(op_start_cycle, operation_duration,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Border barriers
                if (!check_border_barriers_rightwards(op_start_cycle, operation_duration,
                    pos_a.first, middle_barrier, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_vertical_barrier(op_start_cycle, operation_duration,
                            middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_rightwards(op_start_cycle, operation_duration,
                            pos_a.first, middle_barrier, barrier_state_t::raised);
                }
            }
            else
            {
                // Single gate
                
                // Wave
                for (size_t i = 0; i < count; i++)
                {
                    // First wave
                    if (!check_vertical_barrier(
                        op_start_cycle,
                        crossbar_wave_resource_t::WAVE_DURATION_CYCLES, i, barrier_state_t::raised))
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                    if (!check_horizontal_barrier(
                        op_start_cycle,
                        crossbar_wave_resource_t::WAVE_DURATION_CYCLES, i, barrier_state_t::raised))
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                    
                    // Second wave
                    if (!check_vertical_barrier(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        crossbar_wave_resource_t::WAVE_DURATION_CYCLES, i, barrier_state_t::raised))
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                    if (!check_horizontal_barrier(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        crossbar_wave_resource_t::WAVE_DURATION_CYCLES, i, barrier_state_t::raised))
                    {
                        DOUT("    " << name << " resource busy ...");
                        return false;
                    }
                    
                    if (reserve)
                    {
                        // First wave
                        reserve_vertical_barrier(
                            op_start_cycle,
                            crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                            i, barrier_state_t::raised);
                        reserve_horizontal_barrier(
                            op_start_cycle,
                            crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                            i, barrier_state_t::raised);

                        // Second wave
                        reserve_vertical_barrier(
                            op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                            crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                            i, barrier_state_t::raised);
                        reserve_horizontal_barrier(
                            op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                            crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                            i, barrier_state_t::raised);
                    }
                }
                
                // First Shuttle
                int middle_barrier = 0;
                if (operation_name.rfind("_left") != std::string::npos)
                {
                    // Left
                    middle_barrier = pos_a.second - 1;
                }
                else if (operation_name.rfind("_right") != std::string::npos)
                {
                    // Right
                    middle_barrier = pos_a.second;
                }
                else
                {
                    // Left then right
                    if (pos_a.second > 0 && last_crossbar_state->get_count_by_site(ins->operands[0] - 1) == 0)
                    {
                        middle_barrier = pos_a.second - 1;
                    }
                    else if (pos_a.second < n - 1 && last_crossbar_state->get_count_by_site(ins->operands[0] + 1) == 0)
                    {
                        middle_barrier = pos_a.second;
                    }
                    else
                    {
                        EOUT("THIS SHOULD NEVER HAPPEN: can not schedule a one-qubit gate because adjacent sites are not empty");
                        return false;
                    }
                }
                
                // First shuttle
                if (!check_vertical_barrier(
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                if (!check_border_barriers_rightwards(
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    pos_a.first, middle_barrier, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Second shuttle
                if (!check_vertical_barrier(
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                if (!check_border_barriers_rightwards(
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    pos_a.first, middle_barrier, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    // First shuttle
                    reserve_vertical_barrier(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_rightwards(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        pos_a.first, middle_barrier, barrier_state_t::raised);
                    
                    // Second shuttle
                    reserve_vertical_barrier(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_rightwards(
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        pos_a.first, middle_barrier, barrier_state_t::raised);
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // SQSWAP
            std::pair<size_t, size_t> pos_b = last_crossbar_state->get_pos_by_site(ins->operands[1]);
            
            if (operation_name.compare("sqswap") == 0)
            {
                int middle_barrier = std::min(pos_a.first, pos_b.first);
                
                // Barrier between qubits
                if (!check_horizontal_barrier(op_start_cycle, operation_duration,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }

                if (!check_border_barriers_upwards(op_start_cycle, operation_duration,
                    middle_barrier, pos_a.second, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_horizontal_barrier(op_start_cycle, operation_duration,
                            middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_upwards(op_start_cycle, operation_duration,
                            middle_barrier, pos_a.second, barrier_state_t::raised);
                }
            }
            else if (operation_name.compare("cz") == 0)
            {
                int middle_barrier = std::min(pos_a.second, pos_b.second);
                
                // Barrier between qubits
                if (!check_vertical_barrier(op_start_cycle, operation_duration,
                    middle_barrier, barrier_state_t::lowered))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }

                if (!check_border_barriers_rightwards(op_start_cycle, operation_duration,
                    pos_a.first, middle_barrier, barrier_state_t::raised))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_vertical_barrier(op_start_cycle, operation_duration,
                            middle_barrier, barrier_state_t::lowered);

                    reserve_border_barrier_rightwards(op_start_cycle, operation_duration,
                            pos_a.first, middle_barrier, barrier_state_t::raised);
                }
            }
        }
        else if (instruction_type.compare("measurement_gate") == 0)
        {
            // Measurement
            
            // --------------------------------
            // 1. Barriers for the first phase
            // --------------------------------
            
            int middle_barrier = 0;
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                middle_barrier = pos_a.second - 1;
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.second;
            }
            
            // Barrier between qubits
            if (!check_vertical_barrier(
                op_start_cycle, operation_duration / 2,
                middle_barrier, barrier_state_t::lowered))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (!check_border_barriers_rightwards(
                op_start_cycle, operation_duration / 2,
                pos_a.first, middle_barrier, barrier_state_t::raised))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // RESERVE
            if (reserve)
            {
                reserve_vertical_barrier(
                        op_start_cycle, operation_duration / 2,
                        middle_barrier, barrier_state_t::lowered);

                reserve_border_barrier_rightwards(
                        op_start_cycle, operation_duration / 2,
                        pos_a.first, middle_barrier, barrier_state_t::raised);
            }
            
            // ---------------------------------
            // 2. Barriers for the second phase
            // ---------------------------------
            
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                middle_barrier = pos_a.first;
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                middle_barrier = pos_a.first - 1;
            }

            // Barrier between qubits
            if (!check_horizontal_barrier(
                op_start_cycle + operation_duration / 2, operation_duration / 2,
                middle_barrier, barrier_state_t::lowered))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (!check_border_barriers_upwards(
                op_start_cycle + operation_duration / 2, operation_duration / 2,
                middle_barrier, pos_a.second, barrier_state_t::raised))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // RESERVE
            if (reserve)
            {
                reserve_horizontal_barrier(
                        op_start_cycle + operation_duration / 2, operation_duration / 2,
                        middle_barrier, barrier_state_t::lowered);

                reserve_border_barrier_upwards(
                        op_start_cycle + operation_duration / 2, operation_duration / 2,
                        middle_barrier, pos_a.second, barrier_state_t::raised);
            }
        }
        
        return true;
    }
};

}
}
}

#endif // QL_CROSSBAR_BARRIER_RESOURCE_H
