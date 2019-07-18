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
#include <set>
#include <sstream>

namespace ql
{
namespace arch
{
namespace crossbar
{

class crossbar_state_t
{
public:
    // Qubit to position
    std::map<size_t, std::pair<size_t, size_t>> positions;
    // Position to count
    std::map<size_t, std::map<size_t, std::set<size_t>>> board_state;
    // Qubit to is_ancilla
    std::map<size_t, bool> is_ancilla_map;
    
    crossbar_state_t() {}
    
    crossbar_state_t(size_t m, size_t n) {
        for (size_t i = 0; i < m; i++)
        {
            this->board_state[i] = {};
            for (size_t j = 0; j < n; j++)
            {
                this->board_state[i][j] = {};
            }
        }
    }
    
    crossbar_state_t* clone() const & { return new crossbar_state_t(*this);}
    crossbar_state_t* clone() && { return new crossbar_state_t(std::move(*this)); }
    
    size_t get_y_size()
    {
        return this->board_state.size();
    }
    
    size_t get_x_size()
    {
        return this->board_state[0].size();
    }
    
    size_t get_total_sites()
    {
        return this->get_x_size() * this->get_y_size();
    }
    
    size_t get_count_qubits()
    {
        return this->positions.size();
    }
    
    void add_qubit(size_t i, size_t j, size_t qubit_index, bool is_ancilla = false)
    {
        this->positions[qubit_index] = std::make_pair(i, j);
        this->board_state[i][j].insert(qubit_index);
        this->is_ancilla_map[qubit_index] = is_ancilla;
    }
    
    std::pair<size_t, size_t> get_pos_by_qubit(size_t qubit_index)
    {
        return this->positions[qubit_index];
    }
    
    std::pair<size_t, size_t> get_pos_by_site(size_t site_index)
    {
        size_t i = floor(site_index / this->get_x_size());
        size_t j = site_index % this->get_x_size();
        return std::make_pair(i, j);
    }
    
    size_t get_site_by_qubit(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        return (this->get_x_size() * pos.first) + pos.second;
    }
    
    size_t get_site_by_pos(size_t i, size_t j)
    {
        return (i * this->get_x_size()) + j;
    }
    
    size_t get_qubit_by_site(size_t site_index)
    {
        std::pair<size_t, size_t> pos = this->get_pos_by_site(site_index);
        return *this->board_state[pos.first][pos.second].begin();
    }
    
    size_t get_qubit_by_pos(size_t i, size_t j)
    {
        return *this->board_state[i][j].begin();
    }
    
    size_t get_count_by_site(size_t site_index)
    {
        std::pair<size_t, size_t> pos = this->get_pos_by_site(site_index);
        return this->board_state[pos.first][pos.second].size();
    }
    
    size_t get_count_by_position(size_t i, size_t j)
    {
        return this->board_state[i][j].size();
    }
    
    /**
     * Map fake sites (checkboard) to sites
     */
    std::pair<size_t, size_t> get_pos_by_fake_site(size_t fake_site)
    {
        size_t i = 0;
        size_t j = 0;
        if (this->get_x_size() % 2 == 0)
        {
            // Even size
            i = floor(fake_site / (this->get_x_size() / 2));
            size_t temp = fake_site % this->get_x_size();
            size_t offset;
            if (temp >= (this->get_x_size() / 2)) offset = 1;
            else offset = 0;
            j = (2 * fake_site + offset) % this->get_x_size();
        }
        else
        {
            // Odd size
            i = floor(2 * fake_site / this->get_x_size());
            j = (2 * fake_site) % this->get_x_size();
        }
        return std::make_pair(i, j);
    }
    
    size_t get_site_by_fake_site(size_t fake_site)
    {
        std::pair<size_t, size_t> pos = this->get_pos_by_fake_site(fake_site);
        return this->get_site_by_pos(pos.first, pos.second);
    }
    
    size_t get_qubit_by_fake_site(size_t fake_site)
    {
        size_t site = this->get_site_by_fake_site(fake_site);
        return this->get_qubit_by_site(site);
    }
    
    void swap_qubits(size_t qubit_a, size_t qubit_b)
    {
        std::pair<size_t, size_t> pos_a = this->positions[qubit_a];
        std::pair<size_t, size_t> pos_b = this->positions[qubit_b];
        
        // Erase
        this->board_state[pos_a.first][pos_a.second].erase(qubit_a);
        this->board_state[pos_b.first][pos_b.second].erase(qubit_b);
        
        // Add
        this->board_state[pos_b.first][pos_b.second].insert(qubit_a);
        this->board_state[pos_a.first][pos_a.second].insert(qubit_b);
        
        // Change positions
        this->positions[qubit_a] = pos_b;
        this->positions[qubit_b] = pos_a;
    }
    
    void shuttle_up(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second].erase(qubit_index);
        this->board_state[pos.first + 1][pos.second].insert(qubit_index);
        this->positions[qubit_index].first++;
    }
    
    void shuttle_down(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second].erase(qubit_index);
        this->board_state[pos.first - 1][pos.second].insert(qubit_index);
        this->positions[qubit_index].first--;
    }
    
    void shuttle_left(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second].erase(qubit_index);
        this->board_state[pos.first][pos.second - 1].insert(qubit_index);
        this->positions[qubit_index].second--;
    }
    
    void shuttle_right(size_t qubit_index)
    {
        std::pair<size_t, size_t> pos = this->positions[qubit_index];
        this->board_state[pos.first][pos.second].erase(qubit_index);
        this->board_state[pos.first][pos.second + 1].insert(qubit_index);
        this->positions[qubit_index].second++;
    }
    
    bool equals(crossbar_state_t* other_crossbar_state)
    {
        std::map<size_t, std::pair<size_t, size_t>> other_map = other_crossbar_state->positions;
        for (auto& entry : this->positions)
        {
            size_t index = entry.first;
            
            if (other_map.find(index) == other_map.end())
            {
                return false;
            }
            else if (this->positions[index] != other_map[index])
            {
                return false;
            }
        }

        return true;
    }
    
    std::string get_str()
    {
        std::stringstream str;
        for (size_t k = this->get_y_size(); k > 0; k--)
        {
            size_t i = k - 1;
            for (size_t j = 0; j < this->get_x_size(); j++)
            {
                if (this->get_count_by_position(i, j) > 0)
                {
                    str << this->get_qubit_by_pos(i,j) << "\t" << std::flush;   
                }
                else
                {
                    str << "X" << "\t" << std::flush;                    
                }
            }
            
            str << std::endl << std::flush;
        }
        
        return str.str();
    }
    
    void print()
    {
        std::cout << this->get_str() << std::flush;
    }
    
};

}
}
}

#endif // QL_CROSSBAR_STATE_H
