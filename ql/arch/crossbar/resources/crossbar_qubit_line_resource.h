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
#include <ql/arch/crossbar/crossbar_state_map.h>

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
    
    bool owns(ql_condition* other_condition)
    {   
        for (const auto& cond : this->conditions)
        {
            if (cond->less_or_equal == other_condition->less_or_equal
                && cond->line_mode == other_condition->line_mode)
            {
                std::vector<size_t> condition_sites;
                condition_sites.push_back(crossbar_state->get_site_by_pos(
                    other_condition->pos_a_i, other_condition->pos_a_j
                ));
                condition_sites.push_back(crossbar_state->get_site_by_pos(
                    other_condition->pos_b_i, other_condition->pos_b_j
                ));
                
                bool same_operands = true;
                for (size_t site : this->operands)
                {
                    if (std::find(condition_sites.begin(), condition_sites.end(), site) == condition_sites.end())
                    {
                        same_operands = false;
                    }
                }
                
                if (same_operands)
                {
                    return true;
                }
            }
        }
        
        return false;
    }
    
    bool has_conflict(ql_info* other_ql_info)
    {
        for (auto my_cond_it = this->conditions.begin(); my_cond_it != this->conditions.end(); )
        {
            ql_condition* my_condition = *my_cond_it;
            bool ereased_my_cond_it = false;
            
            for (auto other_cond_it = other_ql_info->conditions.begin();
                    other_cond_it != other_ql_info->conditions.end(); )
            {
                ql_condition* other_condition = *other_cond_it;
                bool ereased_other_cond_it = false;
                
                if (my_condition->has_conflict(other_condition))
                {
                    // Get my sites
                    std::vector<size_t> my_condition_sites;
                    my_condition_sites.push_back(crossbar_state->get_site_by_pos(
                        my_condition->pos_a_i, my_condition->pos_a_j
                    ));
                    my_condition_sites.push_back(crossbar_state->get_site_by_pos(
                        my_condition->pos_b_i, my_condition->pos_b_j
                    ));
                    
                    // Get conflicting sites
                    std::vector<size_t> other_condition_sites;
                    other_condition_sites.push_back(crossbar_state->get_site_by_pos(
                        other_condition->pos_a_i, other_condition->pos_a_j
                    ));
                    other_condition_sites.push_back(crossbar_state->get_site_by_pos(
                        other_condition->pos_b_i, other_condition->pos_b_j
                    ));
                    
                    // Check if sites are the same
                    bool sites_are_equal = true;
                    for (size_t site : my_condition_sites)
                    {
                        if (std::find(other_condition_sites.begin(), other_condition_sites.end(), site) == other_condition_sites.end())
                        {
                            sites_are_equal = false;
                            break;
                        }
                    }
                    
                    if (!sites_are_equal)
                    {
                        // There can not be an "agreement" between different sites
                        return true;
                    }
                    
                    bool this_is_owner = true;
                    
                    // Check if I am the owner
                    for (size_t site : this->operands)
                    {
                        if (std::find(other_condition_sites.begin(), other_condition_sites.end(), site) == other_condition_sites.end())
                        {
                            this_is_owner = false;
                            break;
                        }
                    }
                    
                    // Check if "other" is the owner
                    bool other_is_owner = true;
                    for (size_t site : other_ql_info->operands)
                    {
                        if (std::find(other_condition_sites.begin(), other_condition_sites.end(), site) == other_condition_sites.end())
                        {
                            other_is_owner = false;
                            break;
                        }
                    }
                    
                    // Only one of the conditions can be an owner
                    if (this_is_owner != other_is_owner)
                    {
                        // No conflict
                        // Because for this condition someone is the owner of the conflicting sites
                        // So the owner has preference over the other one
                        
                        // Remove unnecessary conflicting condition
                        if (this_is_owner)
                        {
                            other_cond_it = other_ql_info->conditions.erase(other_cond_it);
                            ereased_other_cond_it = true;
                        }
                        else
                        {
                            my_cond_it = this->conditions.erase(my_cond_it);
                            ereased_my_cond_it = true;
                            break;
                        }
                    }
                    else
                    {
                        return true;
                    }
                }
                
                if (!ereased_other_cond_it)
                {
                    other_cond_it++;
                }
            }
            
            if (!ereased_my_cond_it)
            {
                my_cond_it++;
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
        ql::scheduling_direction_t dir, crossbar_state_map_t* crossbar_state_map_local)
        : crossbar_resource_t("qubit_lines", dir, crossbar_state_map_local)
    {
        count = (n * 2) - 1;
    }
    
    crossbar_qubit_line_resource_t* clone() const & { return new crossbar_qubit_line_resource_t(*this);}
    crossbar_qubit_line_resource_t* clone() && { return new crossbar_qubit_line_resource_t(std::move(*this)); }
    
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
    bool check_line(
            std::string operation_name, std::vector<size_t> operands,
            size_t op_start_cycle, size_t operation_duration,
            size_t pos_a_i, size_t pos_a_j, size_t pos_b_i, size_t pos_b_j,
            line_mode_t line_mode, cond_t less_or_equal = cond_t::less)
    {
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        
        ql_info* my_ql_info = new ql_info(last_crossbar_state, operation_name, operands);

        std::vector<ql_condition*> cond_to_check;

        // Add prior checks
        for (size_t k = 0; k < n; k++)
        {
            if (pos_a_i == pos_b_i)
            {
                if (k == pos_a_i) continue;

                // Check QL for isolated qubits in same row/column
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
                    // Check for adjacent qubits (cz)
                    DOUT(std::string("Horizontally adjacent qubits at ")
                        + std::to_string(k) + std::string(", ") + std::to_string(pos_a_j)
                    );
                    
                    ql_condition* my_condition = new ql_condition(
                        k, pos_a_j, k, pos_b_j, line_mode_t::voltage, cond_t::equal
                    );
                    my_ql_info->conditions.push_back(my_condition);

                    cond_to_check.push_back(my_condition);
                }
            }
            else
            {
                if (k == pos_a_j) continue;

                // Check QL for isolated qubits in same row/column
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
                    // Check for adjacent qubits (sqswap)
                    DOUT(std::string("Vertically adjacent qubits at ")
                        + std::to_string(pos_a_i) + std::string(", ") + std::to_string(k)
                    );
                    
                    ql_condition* my_condition = new ql_condition(
                        pos_a_i, k, pos_b_i, k, line_mode_t::voltage, cond_t::equal
                    );
                    my_ql_info->conditions.push_back(my_condition);

                    cond_to_check.push_back(my_condition);
                }
            }
        }

        // Add my current QL
        ql_condition* my_condition = new ql_condition(
            pos_a_i, pos_a_j, pos_b_i, pos_b_j, line_mode, less_or_equal
        );
        my_ql_info->conditions.push_back(my_condition);

        const auto &intervals = qubit_line.findOverlappingIntervals(
            {op_start_cycle, op_start_cycle + operation_duration},
            false
        );

        // Check conditions        
        for (const auto &interval : intervals)
        {
            ql_info* other_ql_info = interval.value;

            if (my_ql_info->has_conflict(other_ql_info))
            {
                std::cout << "Conflict with " << other_ql_info->operation_name << std::endl << std::flush;
                
                return false;
            }
            else
            {
                std::cout << "No conflict with " << other_ql_info->operation_name << std::endl << std::flush;
            }
        }

        // EDGE CASE FOR SQSWAP / CZ
        // Found interval with same condition
        for (auto it = cond_to_check.begin(); it != cond_to_check.end();)
        {
            ql_condition* cond = *it;

            bool erased = false;
            for (const auto &interval : intervals)
            {
                ql_info* other_ql_info = interval.value;

                if (other_ql_info->owns(cond))
                {
                    it = cond_to_check.erase(it);
                    erased = true;
                    break;
                }
            }

            if (!erased)
            {
                it++;
            }
        }

        if (cond_to_check.size() > 0)
        {
            return false;
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
                    // Check for adjacent qubits (CZ)
                    
                    // No need to reserve this
                    // It should already be reserved by another operation
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
                    // Check for adjacent qubits (sqswap)
                    
                    // No need to reserve this
                    // It should already be reserved by another operation
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
                size_t origin_i = pos_a.first;
                size_t destination_i = 0;
                
                if (operation_name.compare("shuttle_up") == 0)
                {
                    destination_i = pos_a.first + 1;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    destination_i = pos_a.first - 1;
                }
                
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    origin_i, pos_a.second, destination_i, pos_a.second,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle, operation_duration,
                        origin_i, pos_a.second, destination_i, pos_a.second,
                        line_mode_t::voltage, cond_t::less);
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t origin_j = pos_a.second;
                size_t destination_j = 0;
                
                if (operation_name.compare("shuttle_left") == 0)
                {
                    destination_j = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    destination_j = pos_a.second + 1;
                }
                
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration,
                    pos_a.first, origin_j, pos_a.first, destination_j,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle, operation_duration,
                        pos_a.first, origin_j, pos_a.first, destination_j,
                        line_mode_t::voltage, cond_t::less);
                }
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
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
                
                // RESERVE
                if (reserve)
                {
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
            }
            else
            {
                if (operation_name.rfind("_left") != std::string::npos)
                {
                    // Left
                    new_pos_a_j = pos_a.second - 1;
                }
                else if (operation_name.rfind("_right") != std::string::npos)
                {
                    // Right
                    new_pos_a_j = pos_a.second + 1;
                }
                else
                {
                    // Qubit lines used to make the auxiliary shuttle between waves
                    if (pos_a.second > 0 && last_crossbar_state->get_count_by_position(pos_a.first, pos_a.second - 1) == 0)
                    {
                        new_pos_a_j = pos_a.second - 1;
                    }
                    else if (pos_a.second < n - 1 && last_crossbar_state->get_count_by_position(pos_a.first, pos_a.second + 1) == 0)
                    {
                        new_pos_a_j = pos_a.second + 1;
                    }
                    else
                    {
                        // Both sites are empty
                        EOUT("THIS SHOULD NEVER HAPPEN: can not schedule a one-qubit gate because adjacent sites are not empty");
                        return false;
                    }
                }
                
                // First shuttle
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // Second shuttle
                if (!check_line(
                    operation_name, ins->operands,
                    op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                    pos_a.first, new_pos_a_j, pos_a.first, pos_a.second,
                    line_mode_t::voltage, cond_t::less))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                // RESERVE
                if (reserve)
                {
                    // First shuttle
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                        line_mode_t::voltage, cond_t::less);
                    
                    // Second shuttle
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2 + crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE,
                        pos_a.first, new_pos_a_j, pos_a.first, pos_a.second,
                        line_mode_t::voltage, cond_t::less);
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> pos_b = last_crossbar_state->get_pos_by_site(ins->operands[1]);
            
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
                
                // RESERVE
                if (reserve)
                {
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle, operation_duration,
                        pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                        line_mode_t::voltage, cond_t::equal);
                }
            }
            else if (operation_name.compare("cz") == 0)
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
                
                // RESERVE
                if (reserve)
                {
                    reserve_line(
                        operation_name, ins->operands,
                        op_start_cycle, operation_duration,
                        pos_a.first, pos_a.second, pos_b.first, pos_b.second,
                        line_mode_t::voltage, cond_t::equal);
                }
            }
        }
        else if (instruction_type.compare("measurement_gate") == 0)
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
                op_start_cycle, operation_duration / 2,
                pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                line_mode_t::voltage, cond_t::less))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // RESERVE
            if (reserve)
            {
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle, operation_duration / 2,
                    pos_a.first, pos_a.second, pos_a.first, new_pos_a_j,
                    line_mode_t::voltage, cond_t::less);
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
                op_start_cycle + operation_duration / 2, operation_duration / 2,
                pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                line_mode_t::signal))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // RESERVE
            if (reserve)
            {
                reserve_line(
                    operation_name, ins->operands,
                    op_start_cycle + operation_duration / 2, operation_duration / 2,
                    pos_a.first, pos_a.second, new_pos_a_i, pos_a.second,
                    line_mode_t::signal);
            }
        }
        
        return true;
    }
};

}
}
}

#endif // QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
