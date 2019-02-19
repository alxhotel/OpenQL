/**
 * @file   crossbar_qubit_line_resource.h
 * @date   02/2019
 * @author Alejandro Morais
 * @brief  Qubit Line resource
 */

#ifndef QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
#define QL_CROSSBAR_QUBIT_LINE_RESOURCE_H

#include <map>
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
    voltage = 0,
    signal = 1
} line_mode_t;

/**
 * Qubit line resource type
 */
class crossbar_qubit_line_resource_t : public resource_t
{
public:
    crossbar_state_t * crossbar_state;
    
    std::map<size_t, line_mode_t> line_mode;
    std::map<size_t, size_t> line_busy;
    std::map<size_t, double> line_value;
    
    crossbar_qubit_line_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_t * crossbar_state_local) : resource_t("qubit_lines", dir)
    {
        count = (crossbar_state->board_state.size() * 2) - 1;
        for (size_t i = 0; i < count; i++)
        {
            line_mode[i] = line_mode_t.voltage;
            line_busy[i] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
            line_value[i] = 0;
        }
    }
    
    crossbar_qubit_resource_t* clone() const & { return new crossbar_qubit_resource_t(*this);}
    crossbar_qubit_resource_t* clone() && { return new crossbar_qubit_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // Get params
        std::pair<int, int> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            int right_line = 0;
            int left_line = 0;
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_left") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (operation_name.compare("shuttle_down") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            /*for (int i = 0;)
            {
                if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;                
                }
            }*/
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                int left_line = 0;
                int right_line = 0;
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    right_line = pos_a.second - pos_a.first;
                    left_line = right_line - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    left_line = pos_a.second - pos_a.first;
                    right_line = left_line + 1;
                }
                
                if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;                
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<int, int> pos_b = crossbar_state->positions[ins->operands[1]];
            
            int left_line = std::min(pos_a.second - pos_a.first, pos_b.second - pos_b.first);
            int right_line = left_line + 1;
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
            {
                DOUT("    " << name << " resource busy ...");
                return false;                
            }
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            
            // ------------------------------
            // 1. Qubit lines for first phase
            // ------------------------------
            int left_line;
            int right_line;
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_left_down") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
            {
                DOUT("    " << name << " resource busy ...");
                return false;                
            }
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            int left_line;
            int right_line;
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_right_up") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
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
        std::pair<int, int> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            int right_line = 0;
            int left_line = 0;
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_left") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (operation_name.compare("shuttle_down") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            line_busy[left_line] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
            line_busy[right_line] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                int left_line = 0;
                int right_line = 0;
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    right_line = pos_a.second - pos_a.first;
                    left_line = right_line - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    left_line = pos_a.second - pos_a.first;
                    right_line = left_line + 1;
                }
                
                if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;                
                }
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<int, int> pos_b = crossbar_state->positions[ins->operands[1]];
            
            int left_line = std::min(pos_a.second - pos_a.first, pos_b.second - pos_b.first);
            int right_line = left_line + 1;
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
            {
                DOUT("    " << name << " resource busy ...");
                return false;                
            }
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            
            // ------------------------------
            // 1. Qubit lines for first phase
            // ------------------------------
            int left_line;
            int right_line;
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_left_down") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
            {
                DOUT("    " << name << " resource busy ...");
                return false;                
            }
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            int left_line;
            int right_line;
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_right_up") == 0)
            {
                right_line = pos_a.second - pos_a.first;
                left_line = right_line - 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                left_line = pos_a.second - pos_a.first;
                right_line = left_line + 1;
            }
            
            if (!check_lines(direction, op_start_cycle, operation_duration, left_line, right_line))
            {
                DOUT("    " << name << " resource busy ...");
                return false;                
            }
        }
    }
    
private:
    bool check_lines(scheduling_direction_t direction, int op_start_cycle,
            int operation_duration, int line_index)
    {
        if (direction == forward_scheduling)
        {
            if (op_start_cycle < line_busy[line_index] || op_start_cycle < line_busy[line_index])
            {
                return false;
            }
        }
        else
        {
            if (op_start_cycle + operation_duration < line_busy[line_index])
            {
                return false;
            }
        } 
    }
    
    void reserve_line(scheduling_direction_t direction, int op_start_cycle,
            int operation_duration, int line_index)
    {
        line_busy[line_index] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
    }
};

}
}
}

#endif // QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
