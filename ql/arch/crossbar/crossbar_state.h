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
    std::map<size_t, bool> is_ancilla_map;
    
    crossbar_state_t() {}
    
    crossbar_state_t(size_t m, size_t n) {
        for (size_t i = 0; i< m; i++)
        {
            for (size_t j = 0; j < n; j++)
            {
                this->board_state[i][j] = 0;
            }
        }
    }
    
    crossbar_state_t* clone() const & { return new crossbar_state_t(*this);}
    crossbar_state_t* clone() && { return new crossbar_state_t(std::move(*this)); }
    
    void add_qubit(size_t i, size_t j, size_t qubit_index, bool is_ancilla = false)
    {
        this->positions[qubit_index] = std::make_pair(i, j);
        this->board_state[i][j]++;
        this->is_ancilla_map[qubit_index] = is_ancilla;
    }
    
    std::pair<size_t, size_t> get_position_by_qubit(size_t qubit_index)
    {
        return this->positions[qubit_index];
    }
    
    std::pair<size_t, size_t> get_position_by_site(size_t site_index)
    {
        size_t i = floor(site_index / this->board_state.size());
        size_t j = site_index - (i * this->board_state.size());
        return std::make_pair(i, j);
    }
    
    size_t get_count_by_site(size_t site_index)
    {
        size_t i = floor(site_index / this->board_state.size());
        size_t j = site_index - (i * this->board_state.size());
        return this->board_state[i][j];
    }
    
    size_t get_count_by_position(size_t i, size_t j)
    {
        return this->board_state[i][j];
    }
    
    size_t get_site_by_pos(size_t i, size_t j)
    {
        return (i * this->board_state.size()) + j;
    }
    
    void shuttle_up(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second]--;
        this->board_state[pos.first + 1][pos.second]++;
        this->positions[qubit_index].first++;
    }
    
    void shuttle_down(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second]--;
        this->board_state[pos.first - 1][pos.second]++;
        this->positions[qubit_index].first--;
    }
    
    void shuttle_left(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second]--;
        this->board_state[pos.first][pos.second - 1]++;
        this->positions[qubit_index].second--;
    }
    
    void shuttle_right(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second]--;
        this->board_state[pos.first][pos.second + 1]++;
        this->positions[qubit_index].second++;
    }
    
};

}
}
}

#endif // QL_CROSSBAR_STATE_H
