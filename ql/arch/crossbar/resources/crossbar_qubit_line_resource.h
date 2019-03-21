/**
 * @file   crossbar_qubit_line_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Qubit Line resource
 */

#ifndef QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
#define QL_CROSSBAR_QUBIT_LINE_RESOURCE_H

#include <map>
#include <ql/arch/crossbar/crossbar_resource.h>
#include <ql/arch/crossbar/crossbar_state.h>

#include "crossbar_wave_resource.h"

namespace ql
{
namespace arch
{
namespace crossbar
{

typedef enum {
    voltage = 0,
    signal = 1
} line_mode_t;

typedef enum {
    equal = 0,
    less = 1
} cond_t;

class ql_condition
{
public:
    size_t pos_a_i;
    size_t pos_a_j;
    size_t pos_b_i;
    size_t pos_b_j;
    line_mode_t line_mode;
    cond_t less_or_equal;
    
    ql_condition(size_t pos_a_i, size_t pos_a_j, size_t pos_b_i, size_t pos_b_j,
        line_mode_t line_mode, cond_t less_or_equal)
    {
        this->pos_a_i = pos_a_i;
        this->pos_a_j = pos_a_j;
        this->pos_b_i = pos_b_i;
        this->pos_b_j = pos_b_j;
        this->line_mode = line_mode;
        this->less_or_equal = less_or_equal;
    }
    
    int get_ql_a()
    {
        return pos_a_j - pos_a_i;
    }
    
    int get_ql_b()
    {
        return pos_b_j - pos_b_i;
    }
    
    bool has_conflict(ql_condition* other_condition)
    {
        // They share at least an operand
        if (this->get_ql_a() == other_condition->get_ql_a()
            || this->get_ql_a() == other_condition->get_ql_b()
            || this->get_ql_b() == other_condition->get_ql_a()
            || this->get_ql_b() == other_condition->get_ql_b())
        {
            if (this->line_mode != other_condition->line_mode)
            {
                return true;
            }
            
            if (this->line_mode == line_mode_t::voltage)
            {
                if (this->less_or_equal == other_condition->less_or_equal)
                {
                    // Same condition
                    
                    if (this->less_or_equal == cond_t::less)
                    {
                        if (this->get_ql_a() == other_condition->get_ql_b()
                            && this->get_ql_b() == other_condition->get_ql_a())
                        {
                            return true;
                        }
                    }
                    else if (this->less_or_equal == cond_t::equal)
                    {
                        // They are compatible
                    }
                }
                else
                {
                    // Different condition
                    
                    if ((this->get_ql_a() == other_condition->get_ql_a()
                        && this->get_ql_b() == other_condition->get_ql_b())
                        ||
                        (this->get_ql_a() == other_condition->get_ql_b()
                        && this->get_ql_b() == other_condition->get_ql_a()))
                    {
                        return true;
                    }
                }
            }
            else if (this->line_mode == line_mode_t::signal)
            {
                if (this->get_ql_a() == other_condition->get_ql_b()
                    || this->get_ql_b() == other_condition->get_ql_a())
                {
                    return true;
                }
            }
        }
        
        return false;
    }
};

class ql_info
{
public:
    crossbar_state_t* crossbar_state;
    std::string operation_name;
    std::vector<size_t> operands;
    std::vector<ql_condition*> conditions;
    
    ql_info(crossbar_state_t* crossbar_state, std::string operation_name, std::vector<size_t> operands)
    {
        this->crossbar_state = crossbar_state;
        this->operation_name = operation_name;
        this->operands = operands;
    }
    
    bool has_conflict(ql_info* other_ql_info)
    {
        for (ql_condition* my_condition : this->conditions)
        {
            for (ql_condition* other_condition : other_ql_info->conditions)
            {
                if (my_condition->has_conflict(other_condition))
                {
                    // Get sites of other_condition
                    std::vector<size_t> condition_sites;
                    condition_sites.push_back(crossbar_state->get_site_by_pos(
                        other_condition->pos_a_i, other_condition->pos_a_j
                    ));
                    condition_sites.push_back(crossbar_state->get_site_by_pos(
                        other_condition->pos_b_i, other_condition->pos_b_j
                    ));
                    
                    bool equal = true;
                    for (size_t site : this->operands)
                    {
                        if (std::find(condition_sites.begin(), condition_sites.end(), site) == condition_sites.end())
                        {
                            equal = false;
                            break;
                        }
                    }
                    
                    if (equal)
                    {
                        // No conflict
                        // Because this instruction is the owner of the sites
                        continue;
                    }
                    else
                    {
                        return true;
                    }
                    
                    return true;
                }
            }
        }
        
        return false;
    }
};

/**
 * Qubit line resource type
 */
class crossbar_qubit_line_resource_t : public crossbar_resource_t
{
public:
    // 0 = RF mode, > 0 = voltage
    Intervals::IntervalTree<size_t, ql_info*> qubit_line;
    
    crossbar_qubit_line_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, std::map<size_t, crossbar_state_t*> crossbar_states_local)
        : crossbar_resource_t("qubit_lines", dir, crossbar_states_local)
    {
        count = (n * 2) - 1;
    }
    
    crossbar_qubit_line_resource_t* clone() const & { return new crossbar_qubit_line_resource_t(*this);}
    crossbar_qubit_line_resource_t* clone() && { return new crossbar_qubit_line_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // Get params
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        std::pair<size_t, size_t> pos_a = last_crossbar_state->get_position_by_site(ins->operands[0]);
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                size_t new_pos_a_i = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    new_pos_a_i = pos_a.first + 1;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    new_pos_a_i = pos_a.first - 1;
                }
                
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t new_pos_a_j = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            size_t new_pos_a_j = 0;
                
            // Z, S & T gate by shuttling
            if (operation_name.rfind("_shuttle") != std::string::npos)
            {
                if (operation_name.rfind("_shuttle_left") != std::string::npos)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (operation_name.rfind("_shuttle_right") != std::string::npos)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                
                // Shuttle to the next column
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration / 2,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Shuttle back
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle + operation_duration / 2, operation_duration / 2,
                    pos_a.first, new_pos_a_j, pos_a.first, pos_a.second,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else
            {
                // Qubit lines used to make the auxiliary shuttle between waves
                if (pos_a.second - 1 >= 0 && last_crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (pos_a.second + 1 <= n - 2
                    && last_crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                else
                {
                    // Both sites are empty
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> pos_b = last_crossbar_state->get_position_by_site(ins->operands[1]);
            
            if (operation_name.compare("sqswap") == 0)
            {
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                    line_mode_t::voltage, cond_t::equal))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else if (operation_name.compare("cphase") == 0)
            {
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                    line_mode_t::voltage, cond_t::equal))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            
            // ------------------------------
            // 1. Qubit lines for first phase
            // ------------------------------
            size_t new_pos_a_j = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_left_down") == 0)
            {
                new_pos_a_j = pos_a.second - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                new_pos_a_j = pos_a.second + 1;
            }
            
            if (!check_line(
                operation_name, ins->operands,
                op_start_cycle, operation_duration,
                pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                line_mode_t::voltage, cond_t::less))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            size_t new_pos_a_i = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0
                || instruction_type.compare("measurement_right_up") == 0)
            {
                new_pos_a_i = pos_a.first + 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                new_pos_a_i = pos_a.first - 1;
            }
            
            if (!check_line(
                operation_name, ins->operands,
                op_start_cycle, operation_duration,
                pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                line_mode_t::signal))
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
        // Get params
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        std::pair<size_t, size_t> pos_a = last_crossbar_state->get_position_by_site(ins->operands[0]);
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                size_t new_pos_a_i = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    new_pos_a_i = pos_a.first + 1;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    new_pos_a_i = pos_a.first - 1;
                }
                
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                    line_mode_t::voltage, cond_t::less);
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t new_pos_a_j = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less);
            }
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            size_t new_pos_a_j = 0;
            
            // Z gate by shuttling
            if (operation_name.rfind("_shuttle_left") != std::string::npos)
            {
                if (operation_name.rfind("_shuttle_left") != std::string::npos)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (operation_name.rfind("_shuttle_right") != std::string::npos)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration / 2,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less);
                
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle + operation_duration / 2, operation_duration / 2,
                    pos_a.first, new_pos_a_j, pos_a.first, pos_a.second,
                    line_mode_t::voltage, cond_t::less);
            }
            else
            {
                // Qubit lines used to ma e a auxiliary shuttle between waves
                if (pos_a.second - 1 >= 0 && last_crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (pos_a.second + 1 <= n - 2
                    && last_crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    new_pos_a_j = pos_a.second + 1;
                }
                else
                {
                    // This should never happen
                }
                
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                    operation_duration - (crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2),
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less);
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> pos_b = last_crossbar_state->get_position_by_site(ins->operands[1]);
            
            if (operation_name.compare("sqswap") == 0)
            {
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                    line_mode_t::voltage, cond_t::equal);
            }
            else if (operation_name.compare("cphase") == 0)
            {
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                    line_mode_t::voltage, cond_t::equal);
            }
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            
            // ------------------------------
            // 1. Qubit lines for first phase
            // ------------------------------
            size_t new_pos_a_j = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0
                || instruction_type.compare("measurement_left_down") == 0)
            {
                new_pos_a_j = pos_a.second - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                new_pos_a_j = pos_a.second + 1;
            }
            
            reserve_line(
                operation_name, ins->operands,
                op_start_cycle, operation_duration / 2,
                pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                line_mode_t::voltage, cond_t::less);
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            size_t new_pos_a_i = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0
                || instruction_type.compare("measurement_right_up") == 0)
            {
                new_pos_a_i = pos_a.first + 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                new_pos_a_i = pos_a.first - 1;
            }
            
            reserve_line(
                operation_name, ins->operands,
                op_start_cycle + operation_duration / 2, operation_duration / 2,
                pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                line_mode_t::signal);
        }
    }
    
private:
    bool check_line(
            std::string operation_name, std::vector<size_t> operands,
            size_t op_start_cycle, size_t operation_duration,
            size_t pos_a_i, size_t pos_a_j, size_t pos_b_i, size_t pos_b_j,
            line_mode_t line_mode, cond_t less_or_equal = cond_t::less)
    {
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        
        if (direction == forward_scheduling)
        {
            ql_info* my_ql_info = new ql_info(last_crossbar_state, operation_name, operands);
   
            // Add prior checks
            for (size_t k = 0; k < n; k++)
            {
                if (pos_a_i == pos_b_i)
                {
                    if (k == pos_a_i) continue;

                    // TODO: Check QL for isolated qubits in same row/column
                    if (last_crossbar_state->get_count_by_position(k, pos_a_j) == 0
                        && last_crossbar_state->get_count_by_position(k, pos_b_j) != 0)
                    {
                        // Qubit in a_j
                        ql_condition* my_condition = new ql_condition(
                            k, pos_a_j, k, pos_b_j, line_mode_t::voltage, cond_t::less
                        );
                        my_ql_info->conditions.push_back(my_condition);
                    }
                    else if (last_crossbar_state->get_count_by_position(k, pos_a_j) != 0
                        && last_crossbar_state->get_count_by_position(k, pos_b_j) == 0)
                    {
                        // Qubit in b_j
                        ql_condition* my_condition = new ql_condition(
                            k, pos_b_j, k, pos_a_j, line_mode_t::voltage, cond_t::less
                        );
                        my_ql_info->conditions.push_back(my_condition);
                    }
                    else if (last_crossbar_state->get_count_by_position(k, pos_a_j) != 0
                        && last_crossbar_state->get_count_by_position(k, pos_b_j) != 0)
                    {
                        // TODO: Check for adjacent qubits (cphase)
                        return false;
                    }
                }
                else
                {
                    if (k == pos_a_j) continue;

                    // TODO: Check QL for isolated qubits in same row/column
                    if (last_crossbar_state->get_count_by_position(pos_a_i, k) == 0
                        && last_crossbar_state->get_count_by_position(pos_b_i, k) != 0)
                    {
                        // Qubit in a_j
                        ql_condition* my_condition = new ql_condition(
                            pos_a_i, k, pos_b_i, k, line_mode_t::voltage, cond_t::less
                        );
                        my_ql_info->conditions.push_back(my_condition);
                    }
                    else if (last_crossbar_state->get_count_by_position(pos_a_i, k) != 0
                        && last_crossbar_state->get_count_by_position(pos_b_i, k) == 0)
                    {
                        // Qubit in b_j
                        ql_condition* my_condition = new ql_condition(
                            pos_b_i, k, pos_a_i, k, line_mode_t::voltage, cond_t::less
                        );
                        my_ql_info->conditions.push_back(my_condition);
                    }
                    else if (last_crossbar_state->get_count_by_position(pos_a_i, k) != 0
                        && last_crossbar_state->get_count_by_position(pos_b_i, k) != 0)
                    {
                        // TODO: Check for adjacent qubits (sqswap)
                        return false;
                    }
                }
            }
            
            // Add my current QL
            ql_condition* my_condition = new ql_condition(
                pos_a_i, pos_a_j, pos_b_i, pos_b_j, line_mode, less_or_equal
            );
            my_ql_info->conditions.push_back(my_condition);
            
            const auto &intervals = qubit_line.findOverlappingIntervals(
                {op_start_cycle, op_start_cycle + operation_duration}
            );
            
            // Check conditions        
            for (const auto &interval : intervals)
            {
                ql_info* other_ql_info = interval.value;
                
                if (my_ql_info->has_conflict(other_ql_info))
                {
                    return false;
                }
            }
        }
        else
        {
            // TODO
        }
        
        return true;
    }
    
    void reserve_line(
        std::string operation_name, std::vector<size_t> operands,
        size_t op_start_cycle, size_t operation_duration,
        size_t pos_a_i, size_t pos_a_j, size_t pos_b_i, size_t pos_b_j,
        line_mode_t line_mode, cond_t less_or_equal = cond_t::less)
    {
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        
        ql_info* my_ql_info = new ql_info(last_crossbar_state, operation_name, operands);
        
        // Add prior checks
        for (size_t k = 0; k < n; k++)
        {
            if (pos_a_i == pos_b_i)
            {
                if (k == pos_a_i) continue;

                // TODO: Check QL for isolated qubits in same row/column
                if (last_crossbar_state->get_count_by_position(k, pos_a_j) == 0
                    && last_crossbar_state->get_count_by_position(k, pos_b_j) != 0)
                {
                    // Qubit in a_j
                    ql_condition* my_condition = new ql_condition(
                        k, pos_a_j, k, pos_b_j, line_mode_t::voltage, cond_t::less
                    );
                    my_ql_info->conditions.push_back(my_condition);
                }
                else if (last_crossbar_state->get_count_by_position(k, pos_a_j) != 0
                    && last_crossbar_state->get_count_by_position(k, pos_b_j) == 0)
                {
                    // Qubit in b_j
                    ql_condition* my_condition = new ql_condition(
                        k, pos_b_j, k, pos_a_j, line_mode_t::voltage, cond_t::less
                    );
                    my_ql_info->conditions.push_back(my_condition);
                }
                else if (last_crossbar_state->get_count_by_position(k, pos_a_j) != 0
                    && last_crossbar_state->get_count_by_position(k, pos_b_j) != 0)
                {
                    // TODO: Check for adjacent qubits (cphase)
                    
                }
            }
            else
            {
                if (k == pos_a_j) continue;

                // TODO: Check QL for isolated qubits in same row/column
                if (last_crossbar_state->get_count_by_position(pos_a_i, k) == 0
                    && last_crossbar_state->get_count_by_position(pos_b_i, k) != 0)
                {
                    // Qubit in a_j
                    ql_condition* my_condition = new ql_condition(
                        pos_a_i, k, pos_b_i, k, line_mode_t::voltage, cond_t::less
                    );
                    my_ql_info->conditions.push_back(my_condition);
                }
                else if (last_crossbar_state->get_count_by_position(pos_a_i, k) != 0
                    && last_crossbar_state->get_count_by_position(pos_b_i, k) == 0)
                {
                    // Qubit in b_j
                    ql_condition* my_condition = new ql_condition(
                        pos_b_i, k, pos_a_i, k, line_mode_t::voltage, cond_t::less
                    );
                    my_ql_info->conditions.push_back(my_condition);
                }
                else if (last_crossbar_state->get_count_by_position(pos_a_i, k) != 0
                    && last_crossbar_state->get_count_by_position(pos_b_i, k) != 0)
                {
                    // TODO: Check for adjacent qubits (sqswap)
                    
                }
            }
        }
        
        // Add my current QL
        ql_condition* my_condition = new ql_condition(
            pos_a_i, pos_a_j, pos_b_i, pos_b_j, line_mode, less_or_equal
        );
        my_ql_info->conditions.push_back(my_condition);
        
        qubit_line.insert({op_start_cycle, op_start_cycle + operation_duration, my_ql_info});
    }
};

}
}
}

#endif // QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
