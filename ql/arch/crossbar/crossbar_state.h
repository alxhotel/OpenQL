/**
 * @file   crossbar_state.h
 * @date   01/2019
 * @author Alejandro Morais
 * @brief  Crossbar state
 */

#ifndef QL_CROSSBAR_STATE_H
#define QL_CROSSBAR_STATE_H

#include <map>
#include <utility>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_state_t
{
public:
    std::map<size_t, std::pair<size_t, size_t>> positions;
    std::map<size_t, std::map<size_t, size_t>> board_state;
    
    crossbar_state_t() {}
    
    void shuttle_up(size_t qubit_index)
    {
        std::pair<size_t, size_t> position = this->positions[qubit_index];
        this->board_state[position.first][position.second]--;
        this->board_state[position.first + 1][position.second]++;
        this->positions[qubit_index].first++;
    }
    
    void shuttle_down(size_t qubit_index)
    {
        std::pair<size_t, size_t> position = this->positions[qubit_index];
        this->board_state[position.first][position.second]--;
        this->board_state[position.first - 1][position.second]++;
        this->positions[qubit_index].first--;
    }
    
    void shuttle_left(size_t qubit_index)
    {
        std::pair<size_t, size_t> position = this->positions[qubit_index];
        this->board_state[position.first][position.second]--;
        this->board_state[position.first][position.second - 1]++;
        this->positions[qubit_index].second--;
    }
    
    void shuttle_right(size_t qubit_index)
    {
        std::pair<size_t, size_t> position = this->positions[qubit_index];
        this->board_state[position.first][position.second]--;
        this->board_state[position.first][position.second + 1]++;
        this->positions[qubit_index].second++;
    }
    
};

}
}
}

#endif // QL_CROSSBAR_STATE_H
