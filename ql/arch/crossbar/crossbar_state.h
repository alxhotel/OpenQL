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
    std::map<int, std::pair<int, int>> positions;
    std::map<int, std::map<int, int>> board_state;
    
    crossbar_state_t() {}
};

}
}
}

#endif // QL_CROSSBAR_STATE_H
