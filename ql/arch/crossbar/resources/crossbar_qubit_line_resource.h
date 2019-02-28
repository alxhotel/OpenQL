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
    std::map<int, size_t> line_busy;
    
    crossbar_qubit_line_resource_t(const ql::quantum_platform & platform,
        ql::scheduling_direction_t dir, crossbar_state_t * crossbar_state_local)
        : resource_t("qubit_lines", dir), crossbar_state(crossbar_state_local)
    {
        count = (crossbar_state->board_state.size() * 2) - 1;
        for (int i = -1 * (count - 1 / 2); i <= ((int) (count - 1) / 2); i++)
        {
            //line_mode[i] = line_mode_t.voltage;
            line_busy[i] = (dir == forward_scheduling ? 0 : MAX_CYCLE);
        }
    }
    
    crossbar_qubit_line_resource_t* clone() const & { return new crossbar_qubit_line_resource_t(*this);}
    crossbar_qubit_line_resource_t* clone() && { return new crossbar_qubit_line_resource_t(std::move(*this)); }
    
    bool available(size_t op_start_cycle, ql::gate * ins, std::string & operation_name,
        std::string & operation_type, std::string & instruction_type, size_t operation_duration)
    {
        // Get params
        size_t n = crossbar_state->board_state.size();
        std::pair<size_t, size_t> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                size_t row_a = 0;
                size_t row_b = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    row_a = pos_a.first;
                    row_b = pos_a.first + 1;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    row_a = pos_a.first;
                    row_b = pos_a.first - 1;
                }
                
                if (!check_lines_per_row(op_start_cycle, operation_duration, row_a, row_b))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t column_a = 0;
                size_t column_b = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                
                if (!check_lines_per_column(op_start_cycle, operation_duration, column_a, column_b))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            size_t column_a = 0;
            size_t column_b = 0;
                
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                
                if (!check_lines_per_column(op_start_cycle, operation_duration, column_a, column_b))
                {
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
            }
            else
            {
                // Qubit lines used to ma e a auxiliary shuttle between waves
                if (pos_a.second - 1 >= 0 && crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (pos_a.second + 1 <= n - 2 && crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                else
                {
                    // Both sites are empty
                    DOUT("    " << name << " resource busy ...");
                    return false;
                }
                
                if (!check_lines_per_column(op_start_cycle, operation_duration, column_a, column_b))
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
            
            if (!check_lines_per_row(op_start_cycle, operation_duration, pos_a.first, pos_b.first))
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
            size_t column_a = 0;
            size_t column_b = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_left_down") == 0)
            {
                column_a = pos_a.second;
                column_b = pos_a.second - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                column_a = pos_a.second;
                column_b = pos_a.second + 1;
            }
            
            if (!check_lines_per_column(op_start_cycle, operation_duration, column_a, column_b))
            {
                DOUT("    " << name << " resource busy ...");
                return false;
            }
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            size_t row_a = 0;
            size_t row_b = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_right_up") == 0)
            {
                row_a = pos_a.first;
                row_b = pos_a.first + 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                row_a = pos_a.first;
                row_b = pos_a.first - 1;
            }
            
            if (!check_lines_per_row(op_start_cycle, operation_duration, row_a, row_b))
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
        size_t n = crossbar_state->board_state.size();
        std::pair<size_t, size_t> pos_a = crossbar_state->positions[ins->operands[0]];
        
        if (instruction_type.compare("shuttle") == 0)
        {
            // Shuttling
            if (operation_name.compare("shuttle_up") == 0 || operation_name.compare("shuttle_down") == 0)
            {
                size_t row_a = 0;
                size_t row_b = 0;
                if (operation_name.compare("shuttle_up") == 0)
                {
                    row_a = pos_a.first;
                    row_b = pos_a.first + 1;
                }
                else if (operation_name.compare("shuttle_down") == 0)
                {
                    row_a = pos_a.first;
                    row_b = pos_a.first - 1;
                }
                
                reserve_line_per_row(op_start_cycle, operation_duration, row_a, row_b);
            }
            else if (operation_name.compare("shuttle_left") == 0 || operation_name.compare("shuttle_right") == 0)
            {
                size_t column_a = 0;
                size_t column_b = 0;
                if (operation_name.compare("shuttle_left") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (operation_name.compare("shuttle_right") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                
                reserve_line_per_column(op_start_cycle, operation_duration, column_a, column_b);
            }
        }
        else if (instruction_type.compare("one_qubit_gate") == 0)
        {
            // One qubit gate
            size_t column_a = 0;
            size_t column_b = 0;
            
            // Z gate by shuttling
            if (operation_name.compare("z_shuttle_left") == 0 || operation_name.compare("z_shuttle_right") == 0)
            {
                if (operation_name.compare("z_shuttle_left") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (operation_name.compare("z_shuttle_right") == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                
                reserve_line_per_column(op_start_cycle, operation_duration, column_a, column_b);
            }
            else
            {
                // Qubit lines used to ma e a auxiliary shuttle between waves
                if (pos_a.second - 1 >= 0 && crossbar_state->board_state[pos_a.first][pos_a.second - 1] == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second - 1;
                }
                else if (pos_a.second + 1 <= n - 2 && crossbar_state->board_state[pos_a.first][pos_a.second + 1] == 0)
                {
                    column_a = pos_a.second;
                    column_b = pos_a.second + 1;
                }
                else
                {
                    // This should never happen
                }
                
                reserve_line_per_column(op_start_cycle, operation_duration, column_a, column_b);
            }
        }
        else if (instruction_type.compare("two_qubit_gate") == 0)
        {
            // Two qubit gate
            std::pair<size_t, size_t> pos_b = crossbar_state->positions[ins->operands[1]];
            
            reserve_line_per_row(op_start_cycle, operation_duration, pos_a.first, pos_b.first);
        }
        else if (instruction_type.compare("measurement") == 0)
        {
            // Measurement
            
            // ------------------------------
            // 1. Qubit lines for first phase
            // ------------------------------
            size_t column_a = 0;
            size_t column_b = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_left_down") == 0)
            {
                column_a = pos_a.second;
                column_b = pos_a.second - 1;
            }
            else if (instruction_type.compare("measurement_right_up") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                column_a = pos_a.second;
                column_b = pos_a.second + 1;
            }
            
            reserve_line_per_column(op_start_cycle, operation_duration, column_a, column_b);
            
            // ------------------------------
            // 2. Qubit lines for second phase
            // ------------------------------
            size_t row_a = 0;
            size_t row_b = 0;
            
            if (instruction_type.compare("measurement_left_up") == 0 || instruction_type.compare("measurement_right_up") == 0)
            {
                row_a = pos_a.first;
                row_b = pos_a.first + 1;
            }
            else if (instruction_type.compare("measurement_left_down") == 0
                || instruction_type.compare("measurement_right_down") == 0)
            {
                row_a = pos_a.first;
                row_b = pos_a.first - 1;
            }
            
            reserve_line_per_row(op_start_cycle, operation_duration, row_a, row_b);
        }
    }
    
private:    
    bool check_line(size_t op_start_cycle, size_t operation_duration, size_t index)
    {
        if (direction == forward_scheduling)
        {
            if (op_start_cycle < line_busy[index])
            {
                return false;
            }
        }
        else
        {
            if (op_start_cycle + operation_duration > line_busy[index])
            {
                return false;
            }
        }
        
        return true;
    }
    
    bool check_lines(size_t op_start_cycle, size_t operation_duration,
            size_t left_line, size_t right_line)
    {
        return check_line(op_start_cycle, operation_duration, left_line)
            && check_line(op_start_cycle, operation_duration, right_line);
    }
    
    bool check_lines_per_column(size_t op_start_cycle, size_t operation_duration, size_t column_a, size_t column_b)
    {
        for (size_t i = 0; i < crossbar_state->board_state.size(); i++)
        {
            // Calculate qubit line
            int qubit_line_left = column_a - i;
            int qubit_line_right = column_b - i;
            if (!check_lines(op_start_cycle, operation_duration, qubit_line_left, qubit_line_right))
            {
                return false;                
            }
        }
        
        return true;
    }
    
    bool check_lines_per_row(size_t op_start_cycle, size_t operation_duration, size_t row_a, size_t row_b)
    {
        for (size_t j = 0; j < crossbar_state->board_state.size(); j++)
        {
            // Calculate qubit line
            int qubit_line_left = j - row_a;
            int qubit_line_right = j - row_b;
            if (!check_lines(op_start_cycle, operation_duration, qubit_line_left, qubit_line_right))
            {
                return false;                
            }
        }
        
        return true;
    }
    
    void reserve_line(size_t op_start_cycle, size_t operation_duration, size_t index)
    {
        line_busy[index] = (direction == forward_scheduling) ? op_start_cycle + operation_duration : op_start_cycle;
    }
    
    void reserve_line_per_column(size_t op_start_cycle, size_t operation_duration, size_t column_a, size_t column_b)
    {
        for (size_t i = 0; i < crossbar_state->board_state.size(); i++)
        {
            // Calculate qubit line
            int qubit_line_left = column_a - i;
            int qubit_line_right = column_b - i;
            reserve_line(op_start_cycle, operation_duration, qubit_line_left);
            reserve_line(op_start_cycle, operation_duration, qubit_line_right);
        }
    }
    
    void reserve_line_per_row(size_t op_start_cycle, size_t operation_duration, size_t row_a, size_t row_b)
    {
        for (size_t j = 0; j < crossbar_state->board_state.size(); j++)
        {
            // Calculate qubit line
            int qubit_line_left = j - row_a;
            int qubit_line_right = j - row_b;
            reserve_line(op_start_cycle, operation_duration, qubit_line_left);
            reserve_line(op_start_cycle, operation_duration, qubit_line_right);
        }
    }
};

}
}
}

#endif // QL_CROSSBAR_QUBIT_LINE_RESOURCE_H
