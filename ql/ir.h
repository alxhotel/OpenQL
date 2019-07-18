/**
 * @file   ir.h
 * @date   02/2018
 * @author Imran Ashraf
 * @brief  common IR implementation
 */

#ifndef IR_H
#define IR_H

#include <vector>
#include <iostream>
#include <fstream>
#include <list>

#include "gate.h"

namespace ql
{
    namespace ir
    {
        typedef std::list<ql::gate *> section_t;
        
        class bundle_t
        {
        public:
            size_t start_cycle;                         // start cycle for all gates in parallel_sections
            size_t duration_in_cycles;                  // the maximum gate duration in parallel_sections
            std::list<section_t> parallel_sections;
        };

        typedef std::list<bundle_t> bundles_t;           // note that subsequent bundles can overlap in time

        ql::ir::bundles_t bundle(ql::circuit& circuit, size_t cycle_time)
        {
            ql::ir::bundles_t bundles;

            size_t total_cycles = 0;
            std::map<size_t, std::vector<ql::gate*> > ins_all_cycles;
            for (auto& gp : circuit)
            {
                if (gp->type() != ql::gate_type_t::__wait_gate__)
                {
                    ins_all_cycles[gp->cycle].push_back(gp);
                    total_cycles = std::max(total_cycles, gp->cycle);
                }
            }

            for (size_t curr_cycle = 0; curr_cycle <= total_cycles; curr_cycle++)
            {
                if (ins_all_cycles.find(curr_cycle) != ins_all_cycles.end())
                {
                    std::vector<ql::gate*> par_ins = ins_all_cycles[curr_cycle];
                    ql::ir::bundle_t new_bundle;
                    size_t duration = 0;
                    for (auto& ins : par_ins)
                    {
                        ql::ir::section_t new_section;
                        new_section.push_back(ins);
                        new_bundle.parallel_sections.push_back(new_section);
                        duration = std::max(duration, ins->duration);
                    }
                    new_bundle.start_cycle = curr_cycle;
                    // Dont know why we need to add "cycle_time - 1"
                    new_bundle.duration_in_cycles = (duration + cycle_time - 1) / cycle_time; 
                    bundles.push_back(new_bundle);
                }
            }

            return bundles;
        }
        
        std::string qasm(bundles_t & bundles)
        {
            std::stringstream ssqasm;
            size_t curr_cycle = 1;

            for (auto& abundle : bundles)
            {
                auto st_cycle = abundle.start_cycle;
                auto delta = st_cycle - curr_cycle;
                
                // Start of all bundles
                if (delta > 1) ssqasm << "    wait " << delta - 1 << '\n';

                auto ngates = 0;
                for (auto& sec : abundle.parallel_sections)
                {
                    ngates += sec.size();
                }
                ssqasm << "    ";
                if (ngates > 1) ssqasm << "{ "; 
                auto isfirst = 1;
                for (auto& sec : abundle.parallel_sections)
                {
                    for (auto& gp : sec)
                    {
                        if (isfirst == 0) ssqasm << " | ";
                        ssqasm << gp->qasm();
                        isfirst = 0;
                    }
                }
                if (ngates > 1) ssqasm << " }"; 
                curr_cycle += delta;
                ssqasm << "\n";
            }

            if (!bundles.empty())
            {
                // End of all bundles
                auto& last_bundle = bundles.back();
                int lsduration = last_bundle.duration_in_cycles;
                if (lsduration > 1) ssqasm << "    wait " << lsduration -1 << '\n';
            }

            return ssqasm.str();
        }

        void write_qasm(bundles_t & bundles)
        {
            std::ofstream fout;
            std::string fname( ql::options::get("output_dir") + "/ir.qasm" );
            fout.open(fname, std::ios::binary);
            if (fout.fail())
            {
                EOUT("Error opening file " << fname << std::endl
                         << "Make sure the output directory ("<< ql::options::get("output_dir") << ") exists");
                return;
            }

            fout << qasm(bundles);
            fout.close();
        }
    } // namespace ir
} //namespace ql

#endif
