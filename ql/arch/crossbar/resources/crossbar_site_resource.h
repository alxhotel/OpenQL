/**
 * @file   crossbar_site_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Site resource
 */

#ifndef CROSSBAR_SITE_RESOURCE_H
#define CROSSBAR_SITE_RESOURCE_H

#include <map>
#include <vector>
#include <ql/arch/crossbar/crossbar_resource.h>
#include <ql/arch/crossbar/crossbar_state_map.h>

#include "crossbar_wave_resource.h"

namespace ql
{
namespace arch
{
namespace crossbar
{

/**
 * Site resource type
 */
class crossbar_site_resource_t : public crossbar_resource_t
{
public:
    std::map<size_t, std::map<size_t, Intervals::IntervalTree<size_t, size_t>>> site_state;
    
    crossbar_site_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_map_t* crossbar_state_map_local)
        : crossbar_resource_t("sites", dir, crossbar_state_map_local)
    {
        count = (m * n);
    }

    crossbar_site_resource_t* clone() const & { return new crossbar_site_resource_t(*this);}
    crossbar_site_resource_t* clone() && { return new crossbar_site_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        if (available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, false))
        {
            
            crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        last_crossbar_state->print();
            
            DOUT("    " << name << " resource available ...");
            return true;
        }
        
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        last_crossbar_state->print();
        
        return false;
    }

    void reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        available_or_reserve(op_start_cycle, ins, operation_name,
            operation_type, instruction_type, operation_duration, true);
    }

private:
    bool check_site(size_t op_start_cycle, size_t operation_duration,
            std::pair<size_t, size_t> site, size_t expected_count)
    {
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        size_t count = last_crossbar_state->get_count_by_position(site.first, site.second);
        
        std::cout << "Check s[" << std::to_string(site.first) << ", " << std::to_string(site.second) << "]"
                << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration
                << " (expected " << std::to_string(expected_count) << " got " << std::to_string(count) << ")"
                << std::endl << std::flush;
        
        if (count != expected_count)
        {   
            return false;
        }
        
        const auto &intervals = site_state[site.first][site.second].findOverlappingIntervals(
            {op_start_cycle, op_start_cycle + operation_duration},
            false
        );
        
        // NOTE: Does not matter the direction of the scheduling
        for (const auto &interval : intervals)
        {
            if (interval.value >= 1)
            {
                return false;
            }
        }
        
        return true;
    }
    
    void reserve_site(size_t op_start_cycle, size_t operation_duration, std::pair<size_t, size_t> site)
    {
        std::cout << "Reserve s[" << std::to_string(site.first) << ", " << std::to_string(site.second) << "]"
                << " from " << op_start_cycle << " to " << op_start_cycle + operation_duration
                << std::endl << std::flush;
                
        site_state[site.first][site.second].insert({op_start_cycle, op_start_cycle + operation_duration, 1});
    }
    
    bool available_or_reserve(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration,
        bool reserve)
    {
        // Get params
        crossbar_state_t* last_crossbar_state = get_last_crossbar_state(op_start_cycle);
        std::pair<size_t, size_t> pos_a = last_crossbar_state->get_pos_by_site(ins->operands[0]);
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
            
            if (operation_name.compare("shuttle_up") == 0)
            {
                destination_site = std::make_pair(pos_a.first + 1, pos_a.second);
            }
            else if (operation_name.compare("shuttle_down") == 0)
            {
                destination_site = std::make_pair(pos_a.first - 1, pos_a.second);
            }
            else if (operation_name.compare("shuttle_left") == 0)
            {
                destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
            }
            else if (operation_name.compare("shuttle_right") == 0)
            {
                destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
            }
            
            if (direction == ql::forward_scheduling)
            {
                // Origin site
                if (!check_site(op_start_cycle, operation_duration, origin_site, 1))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }

                // Destination site
                if (!check_site(op_start_cycle, operation_duration, destination_site, 0))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else
            {
                // Origin site
                if (!check_site(op_start_cycle, operation_duration, origin_site, 0))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }

                // Destination site
                if (!check_site(op_start_cycle, operation_duration, destination_site, 1))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            
            if (reserve)
            {
                // Origin site
                reserve_site(op_start_cycle, operation_duration, origin_site);

                // Destination site
                reserve_site(op_start_cycle, operation_duration, destination_site);
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // One qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
                
            // Z gate by shuttling
            if (operation_name.rfind("_shuttle") != std::string::npos)
            {
                if (operation_name.rfind("_shuttle_left") != std::string::npos)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (operation_name.rfind("_shuttle_right") != std::string::npos)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
            }
            else
            {
                if (operation_name.rfind("_left") != std::string::npos)
                {
                    // Left
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (operation_name.rfind("_right") != std::string::npos)
                {
                    // Right
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
                else
                {
                    // Global single qubit gate
                    if (pos_a.second > 0 && last_crossbar_state->get_count_by_position(pos_a.first, pos_a.second - 1) == 0)
                    {
                        // Left site is empty
                        destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                    }
                    else if (pos_a.second < n - 1 && last_crossbar_state->get_count_by_position(pos_a.first, pos_a.second + 1) == 0)
                    {
                        // Right site is empty
                        destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                    }
                    else
                    {
                        // Both sites are empty
                        EOUT("THIS SHOULD NEVER HAPPEN: can not schedule a one-qubit gate because adjacent sites are not empty");
                        return false;
                    }
                }
            }
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site, 1))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Destination site
            if (!check_site(op_start_cycle, operation_duration, destination_site, 0))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (reserve)
            {
                // Origin site
                reserve_site(
                    op_start_cycle,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE * 2 + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2,
                    origin_site);

                // Destination site
                reserve_site(
                    op_start_cycle,
                    crossbar_wave_resource_t::SHUTTLE_DURATION_CYCLE * 2 + crossbar_wave_resource_t::WAVE_DURATION_CYCLES * 2,
                    destination_site);
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = last_crossbar_state->get_pos_by_site(ins->operands[1]);
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site, 1))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Destination site
            if (!check_site(op_start_cycle, operation_duration, destination_site, 1))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (reserve)
            {
                // Origin site
                reserve_site(op_start_cycle, operation_duration, origin_site);

                // Destination site
                reserve_site(op_start_cycle, operation_duration, destination_site);
            }
        }
        else if (instruction_type.compare("measurement_gate") == 0)
        {
            // Measurement
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> ancilla_site = pos_a;
            std::pair<size_t, size_t> empty_site = pos_a;
            
            // Ancilla site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second - 1);
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second + 1);
            }
            
            // Empty site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                empty_site = std::make_pair(pos_a.first + 1, pos_a.second);
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                empty_site = std::make_pair(pos_a.first - 1, pos_a.second);
            }
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site, 1))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Ancilla site
            if (!check_site(op_start_cycle, operation_duration, ancilla_site, 1))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Vertical empty site
            if (!check_site(op_start_cycle, operation_duration, empty_site, 0))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            if (reserve)
            {
                // Origin site
                reserve_site(op_start_cycle, operation_duration, origin_site);

                // Ancilla site
                reserve_site(op_start_cycle, operation_duration, ancilla_site);

                // Empty site
                reserve_site(op_start_cycle, operation_duration, empty_site);
            }
        }
        
        return true;
    }
};

}
}
}

#endif // CROSSBAR_SITE_RESOURCE_H
