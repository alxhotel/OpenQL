/**
 * @file   crossbar_site_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Site resource
 */

#ifndef CROSSBAR_SITE_RESOURCE_H
#define CROSSBAR_SITE_RESOURCE_H

#include <vector>
#include <ql/resource_manager.h>
#include <ql/arch/crossbar/crossbar_state.h>

namespace ql
{
namespace arch
{
namespace crossbar
{

/**
 * Site resource type
 */
class crossbar_site_resource_t : public resource_t
{
public:
    crossbar_state_t * crossbar_state;
    
    std::map<size_t, std::map<size_t, size_t>> site_state;
    
    crossbar_site_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_t * crossbar_state_local)
        : resource_t("sites", dir), crossbar_state(crossbar_state_local)
    {
        for (size_t i = 0; i < crossbar_state_local->board_state.size(); i++)
        {
            site_state[i] = {};
            for (size_t j = 0; j < crossbar_state_local->board_state[i].size(); j++)
            {
                site_state[i][j] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
            }
        }
    }

    crossbar_site_resource_t* clone() const & { return new crossbar_site_resource_t(*this);}
    crossbar_site_resource_t* clone() && { return new crossbar_site_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // Get params
        size_t n = crossbar_state->board_state.size();
        std::pair<size_t, size_t> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
            
            if (operation_name.compare("shuttle_up") == 0)
            {
                destination_site = std::make_pair(pos_a.first + 1,pos_a.second);
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
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Destination site
            if (!check_site(op_start_cycle, operation_duration, destination_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // One qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
                
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
            }
            else
            {
                // Global single qubit gate
                if (pos_a.second - 1 >= 0 && crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    // Left site is empty
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (pos_a.second + 1 <= n - 2 && crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    // Right site is empty
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
            }
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Destination site
            if (!check_site(op_start_cycle, operation_duration, destination_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = crossbar_state->positions[ins->operands[1]];
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Destination site
            if (!check_site(op_start_cycle, operation_duration, destination_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> ancilla_site = pos_a;
            std::pair<size_t, size_t> vertical_site = pos_a;
            
            // Ancilla site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second - 1);
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second + 1);
            }
            
            // Vertical site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                vertical_site = std::make_pair(pos_a.first + 1, pos_a.second);
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                vertical_site = std::make_pair(pos_a.first - 1, pos_a.second);
            }
            
            // Origin site
            if (!check_site(op_start_cycle, operation_duration, origin_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Ancilla site
            if (!check_site(op_start_cycle, operation_duration, ancilla_site))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // Vertical empty site
            if (!check_site(op_start_cycle, operation_duration, vertical_site))
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
        std::pair<size_t, size_t> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
            
            if (operation_name.compare("shuttle_up") == 0)
            {
                destination_site = std::make_pair(pos_a.first + 1,pos_a.second);
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
            
            // Origin site
            reserve_site(op_start_cycle, operation_duration, origin_site);
            
            // Destination site
            reserve_site(op_start_cycle, operation_duration, destination_site);
        }
        else if (instruction_type.compare("single_qubit_gate") == 0)
        {
            // One qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = pos_a;
                
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
            }
            else
            {
                // Global single qubit gate
                if (pos_a.second - 1 >= 0 && crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    // Left site is empty
                    destination_site = std::make_pair(pos_a.first, pos_a.second - 1);
                }
                else if (pos_a.second + 1 >= 0 && crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    // Right site is empty
                    destination_site = std::make_pair(pos_a.first, pos_a.second + 1);
                }
            }
            
            // Origin site
            reserve_site(op_start_cycle, operation_duration, origin_site);
            
            // Destination site
            reserve_site(op_start_cycle, operation_duration, destination_site);
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> destination_site = crossbar_state->positions[ins->operands[1]];
            
            // Origin site
            reserve_site(op_start_cycle, operation_duration, origin_site);
            
            // Destination site
            reserve_site(op_start_cycle, operation_duration, destination_site);
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            std::pair<size_t, size_t> origin_site = pos_a;
            std::pair<size_t, size_t> ancilla_site = pos_a;
            std::pair<size_t, size_t> vertical_site = pos_a;
            
            // Ancilla site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_left_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second - 1);
            }
            else if (operation_name.compare("measure_right_up") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                ancilla_site = std::make_pair(pos_a.first, pos_a.second + 1);
            }
            
            // Vertical site
            if (operation_name.compare("measure_left_up") == 0 || operation_name.compare("measure_right_up") == 0)
            {
                vertical_site = std::make_pair(pos_a.first + 1, pos_a.second);
            }
            else if (operation_name.compare("measure_left_down") == 0 || operation_name.compare("measure_right_down") == 0)
            {
                vertical_site = std::make_pair(pos_a.first - 1, pos_a.second);
            }
            
            // Origin site
            reserve_site(op_start_cycle, operation_duration, origin_site);
            
            // Ancilla site
            reserve_site(op_start_cycle, operation_duration, ancilla_site);
            
            // Ancilla site
            reserve_site(op_start_cycle, operation_duration, vertical_site);
        }
    }

private:
    bool check_site(size_t op_start_cycle, size_t operation_duration, std::pair<size_t, size_t> site)
    {
        if (direction == forward_scheduling)
        {
            if (op_start_cycle < site_state[site.first][site.second])
            {
                return false;
            }
        }
        else
        {
            if (op_start_cycle + operation_duration > site_state[site.first][site.second])
            {
                return false;
            }
        }
        
        return true;
    }
    
    void reserve_site(size_t op_start_cycle, size_t operation_duration, std::pair<size_t, size_t> site)
    {
        site_state[site.first][site.second] = (direction == forward_scheduling)
                ? op_start_cycle + operation_duration
                : op_start_cycle;
    }
};

}
}
}

#endif // CROSSBAR_SITE_RESOURCE_H
