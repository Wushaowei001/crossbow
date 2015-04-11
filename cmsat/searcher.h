/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2014, Mate Soos. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.0 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301  USA
*/

#ifndef __SEARCHER_H__
#define __SEARCHER_H__

#include "propengine.h"
#include "solvertypes.h"

#ifdef STATS_NEEDED_EXTRA
#include <boost/multi_array.hpp>
#endif

#include "time_mem.h"
#include "avgcalc.h"
#include "hyperengine.h"
#include "MersenneTwister.h"
#include "minisat_rnd.h"

namespace CMSat {

class Solver;
class SQLStats;
class VarReplacer;

using std::string;
using std::cout;
using std::endl;

struct OTFClause
{
    Lit lits[3];
    unsigned size;
};

struct VariableVariance
{
    double avgDecLevelVarLT = 0;
    double avgTrailLevelVarLT= 0;
    double avgDecLevelVar = 0;
    double avgTrailLevelVar = 0;
};

class Searcher : public HyperEngine
{
    public:
        Searcher(const SolverConf* _conf, Solver* solver, bool* _needToInterrupt);
        virtual ~Searcher();

        //History
        struct Hist {
            //About the search
            AvgCalc<uint32_t>   branchDepthHist;     ///< Avg branch depth in current restart
            AvgCalc<uint32_t>   branchDepthDeltaHist;

            bqueue<uint32_t>   trailDepthHist;
            bqueue<uint32_t>   trailDepthHistLonger;
            AvgCalc<uint32_t>   trailDepthDeltaHist;

            //About the confl generated
            bqueue<uint32_t>    glueHist;            ///< Set of last decision levels in (glue of) conflict clauses
            AvgCalc<uint32_t>   glueHistLT;

            AvgCalc<uint32_t>   conflSizeHist;       ///< Conflict size history
            AvgCalc<uint32_t>   conflSizeHistLT;

            AvgCalc<uint32_t>   numResolutionsHist;  ///< Number of resolutions during conflict analysis
            AvgCalc<uint32_t>   numResolutionsHistLT;

            //lits, vars
            AvgCalc<double, double>  agilityHist;
            AvgCalc<double, double>  agilityHistLT;

            #ifdef STATS_NEEDED
            AvgCalc<bool>       conflictAfterConflict;
            AvgCalc<size_t>     watchListSizeTraversed;
            #endif

            size_t mem_used() const
            {
                uint64_t used = sizeof(Hist);
                used += sizeof(AvgCalc<uint32_t>)*16;
                used += sizeof(AvgCalc<bool>)*4;
                used += sizeof(AvgCalc<size_t>)*2;
                used += sizeof(AvgCalc<double, double>)*2;
                used += glueHist.usedMem();

                return used;
            }

            void clear()
            {
                //About the search
                branchDepthHist.clear();
                branchDepthDeltaHist.clear();
                trailDepthHist.clear();
                trailDepthDeltaHist.clear();

                //conflict generated
                glueHist.clear();
                conflSizeHist.clear();
                numResolutionsHist.clear();

                //lits, vars
                agilityHist.clear();

                #ifdef STATS_NEEDED
                conflictAfterConflict.clear();
                watchListSizeTraversed.clear();
                #endif
            }

            void setSize(const size_t shortTermHistorySize, const size_t blocking_trail_hist_size)
            {
                glueHist.clearAndResize(shortTermHistorySize);
                trailDepthHist.clearAndResize(shortTermHistorySize);
                trailDepthHistLonger.clearAndResize(blocking_trail_hist_size);
            }

            void print() const
            {
                cout
                << " glue"
                << " " << std::right << glueHist.getLongtTerm().avgPrint(1, 5)
                << "/" << std::left << glueHistLT.avgPrint(1, 5)

                << " agil"
                << " " << std::right << agilityHist.avgPrint(3, 5)
                << "/" << std::left<< agilityHistLT.avgPrint(3, 5)

                << " confllen"
                << " " << std::right << conflSizeHist.avgPrint(1, 5)
                << "/" << std::left << conflSizeHistLT.avgPrint(1, 5)

                << " branchd"
                << " " << std::right << branchDepthHist.avgPrint(1, 5)
                << " branchdd"

                << " " << std::right << branchDepthDeltaHist.avgPrint(1, 4)

                << " traild"
                << " " << std::right << trailDepthHist.getLongtTerm().avgPrint(0, 7)

                << " traildd"
                << " " << std::right << trailDepthDeltaHist.avgPrint(0, 5)
                ;

                cout << std::right;
            }
        };

        ///////////////////////////////
        // Solving
        //
        lbool solve(
            uint64_t maxConfls = std::numeric_limits<uint64_t>::max()
        );
        void finish_up_solve(lbool status);
        void setup_restart_print();
        void reduce_db_if_needed();
        void clean_clauses_if_needed();
        lbool perform_scc_and_varreplace_if_needed();
        void save_search_loop_stats();
        bool must_abort(lbool status);
        void print_search_loop_num();
        uint64_t max_conflicts_geometric;
        uint64_t max_conflicts;
        uint64_t loop_num;
        MiniSatRnd mtrand; ///< random number generator


        vector<lbool>  model;
        vector<Lit>   conflict;     ///<If problem is unsatisfiable (possibly under assumptions), this vector represent the final conflict clause expressed in the assumptions.
        template<bool update_bogoprops = true>
        PropBy propagate(
            #ifdef STATS_NEEDED
            AvgCalc<size_t>* watchListSizeTraversed = NULL
            #endif
        );

        ///////////////////////////////
        // Stats
        //Restart print status
        uint64_t lastRestartPrint;
        uint64_t lastRestartPrintHeader;
        void     print_restart_stat();
        void     print_iteration_solving_stats();
        void     printRestartHeader() const;
        void     print_restart_stat_line() const;
        void     printBaseStats() const;
        void     print_clause_stats() const;
        uint64_t sumConflicts() const;
        uint64_t sumRestarts() const;
        const Hist& getHistory() const;

        struct Stats
        {
            void clear()
            {
                Stats tmp;
                *this = tmp;
            }

            Stats& operator+=(const Stats& other)
            {
                numRestarts += other.numRestarts;
                blocked_restart += other.blocked_restart;
                blocked_restart_same += other.blocked_restart_same;

                //Decisions
                decisions += other.decisions;
                decisionsAssump += other.decisionsAssump;
                decisionsRand += other.decisionsRand;
                decisionFlippedPolar += other.decisionFlippedPolar;

                //Conflict minimisation stats
                litsRedNonMin += other.litsRedNonMin;
                litsRedFinal += other.litsRedFinal;
                recMinCl += other.recMinCl;
                recMinLitRem += other.recMinLitRem;

                furtherShrinkAttempt  += other.furtherShrinkAttempt;
                binTriShrinkedClause += other.binTriShrinkedClause;
                cacheShrinkedClause += other.cacheShrinkedClause;
                furtherShrinkedSuccess += other.furtherShrinkedSuccess;


                stampShrinkAttempt += other.stampShrinkAttempt;
                stampShrinkCl += other.stampShrinkCl;
                stampShrinkLit += other.stampShrinkLit;
                moreMinimLitsStart += other.moreMinimLitsStart;
                moreMinimLitsEnd += other.moreMinimLitsEnd;
                recMinimCost += other.recMinimCost;

                //Red stats
                learntUnits += other.learntUnits;
                learntBins += other.learntBins;
                learntTris += other.learntTris;
                learntLongs += other.learntLongs;
                otfSubsumed += other.otfSubsumed;
                otfSubsumedImplicit += other.otfSubsumedImplicit;
                otfSubsumedLong += other.otfSubsumedLong;
                otfSubsumedRed += other.otfSubsumedRed;
                otfSubsumedLitsGained += other.otfSubsumedLitsGained;

                //Hyper-bin & transitive reduction
                advancedPropCalled += other.advancedPropCalled;
                hyperBinAdded += other.hyperBinAdded;
                transReduRemIrred += other.transReduRemIrred;
                transReduRemRed += other.transReduRemRed;

                //Stat structs
                resolvs += other.resolvs;
                conflStats += other.conflStats;

                //Time
                cpu_time += other.cpu_time;

                return *this;
            }

            Stats& operator-=(const Stats& other)
            {
                numRestarts -= other.numRestarts;
                blocked_restart -= other.blocked_restart;
                blocked_restart_same -= other.blocked_restart_same;

                //Decisions
                decisions -= other.decisions;
                decisionsAssump -= other.decisionsAssump;
                decisionsRand -= other.decisionsRand;
                decisionFlippedPolar -= other.decisionFlippedPolar;

                //Conflict minimisation stats
                litsRedNonMin -= other.litsRedNonMin;
                litsRedFinal -= other.litsRedFinal;
                recMinCl -= other.recMinCl;
                recMinLitRem -= other.recMinLitRem;

                furtherShrinkAttempt  -= other.furtherShrinkAttempt;
                binTriShrinkedClause -= other.binTriShrinkedClause;
                cacheShrinkedClause -= other.cacheShrinkedClause;
                furtherShrinkedSuccess -= other.furtherShrinkedSuccess;

                stampShrinkAttempt -= other.stampShrinkAttempt;
                stampShrinkCl -= other.stampShrinkCl;
                stampShrinkLit -= other.stampShrinkLit;
                moreMinimLitsStart -= other.moreMinimLitsStart;
                moreMinimLitsEnd -= other.moreMinimLitsEnd;
                recMinimCost -= other.recMinimCost;

                //Red stats
                learntUnits -= other.learntUnits;
                learntBins -= other.learntBins;
                learntTris -= other.learntTris;
                learntLongs -= other.learntLongs;
                otfSubsumed -= other.otfSubsumed;
                otfSubsumedImplicit -= other.otfSubsumedImplicit;
                otfSubsumedLong -= other.otfSubsumedLong;
                otfSubsumedRed -= other.otfSubsumedRed;
                otfSubsumedLitsGained -= other.otfSubsumedLitsGained;

                //Hyper-bin & transitive reduction
                advancedPropCalled -= other.advancedPropCalled;
                hyperBinAdded -= other.hyperBinAdded;
                transReduRemIrred -= other.transReduRemIrred;
                transReduRemRed -= other.transReduRemRed;

                //Stat structs
                resolvs -= other.resolvs;
                conflStats -= other.conflStats;

                //Time
                cpu_time -= other.cpu_time;

                return *this;
            }

            Stats operator-(const Stats& other) const
            {
                Stats result = *this;
                result -= other;
                return result;
            }

            void printCommon() const
            {
                print_stats_line("c restarts"
                    , numRestarts
                    , (double)conflStats.numConflicts/(double)numRestarts
                    , "confls per restart"

                );
                print_stats_line("c blocked restarts"
                    , blocked_restart
                    , (double)blocked_restart/(double)numRestarts
                    , "per normal restart"

                );
                print_stats_line("c time", cpu_time);
                print_stats_line("c decisions", decisions
                    , stats_line_percent(decisionsRand, decisions)
                    , "% random"
                );

                print_stats_line("c decisions/conflicts"
                    , (double)decisions/(double)conflStats.numConflicts
                );
            }

            void print_short() const
            {
                //Restarts stats
                printCommon();
                conflStats.print_short(cpu_time);

                print_stats_line("c conf lits non-minim"
                    , litsRedNonMin
                    , (double)litsRedNonMin/(double)conflStats.numConflicts
                    , "lit/confl"
                );

                print_stats_line("c conf lits final"
                    , (double)litsRedFinal/(double)conflStats.numConflicts
                );
            }

            void print() const
            {
                printCommon();
                conflStats.print(cpu_time);

                /*assert(numConflicts
                    == conflsBin + conflsTri + conflsLongIrred + conflsLongRed);*/

                cout << "c LEARNT stats" << endl;
                print_stats_line("c units learnt"
                    , learntUnits
                    , stats_line_percent(learntUnits, conflStats.numConflicts)
                    , "% of conflicts");

                print_stats_line("c bins learnt"
                    , learntBins
                    , stats_line_percent(learntBins, conflStats.numConflicts)
                    , "% of conflicts");

                print_stats_line("c tris learnt"
                    , learntTris
                    , stats_line_percent(learntTris, conflStats.numConflicts)
                    , "% of conflicts");

                print_stats_line("c long learnt"
                    , learntLongs
                    , stats_line_percent(learntLongs, conflStats.numConflicts)
                    , "% of conflicts"
                );

                print_stats_line("c otf-subs"
                    , otfSubsumed
                    , ratio_for_stat(otfSubsumed, conflStats.numConflicts)
                    , "/conflict"
                );

                print_stats_line("c otf-subs implicit"
                    , otfSubsumedImplicit
                    , stats_line_percent(otfSubsumedImplicit, otfSubsumed)
                    , "%"
                );

                print_stats_line("c otf-subs long"
                    , otfSubsumedLong
                    , stats_line_percent(otfSubsumedLong, otfSubsumed)
                    , "%"
                );

                print_stats_line("c otf-subs learnt"
                    , otfSubsumedRed
                    , stats_line_percent(otfSubsumedRed, otfSubsumed)
                    , "% otf subsumptions"
                );

                print_stats_line("c otf-subs lits gained"
                    , otfSubsumedLitsGained
                    , ratio_for_stat(otfSubsumedLitsGained, otfSubsumed)
                    , "lits/otf subsume"
                );

                cout << "c SEAMLESS HYPERBIN&TRANS-RED stats" << endl;
                print_stats_line("c advProp called"
                    , advancedPropCalled
                );
                print_stats_line("c hyper-bin add bin"
                    , hyperBinAdded
                    , ratio_for_stat(hyperBinAdded, advancedPropCalled)
                    , "bin/call"
                );
                print_stats_line("c trans-red rem irred bin"
                    , transReduRemIrred
                    , ratio_for_stat(transReduRemIrred, advancedPropCalled)
                    , "bin/call"
                );
                print_stats_line("c trans-red rem red bin"
                    , transReduRemRed
                    , ratio_for_stat(transReduRemRed, advancedPropCalled)
                    , "bin/call"
                );

                cout << "c CONFL LITS stats" << endl;
                print_stats_line("c orig "
                    , litsRedNonMin
                    , ratio_for_stat(litsRedNonMin, conflStats.numConflicts)
                    , "lit/confl"
                );

                print_stats_line("c rec-min effective"
                    , recMinCl
                    , stats_line_percent(recMinCl, conflStats.numConflicts)
                    , "% attempt successful"
                );

                print_stats_line("c rec-min lits"
                    , recMinLitRem
                    , stats_line_percent(recMinLitRem, litsRedNonMin)
                    , "% less overall"
                );

                print_stats_line("c further-min call%"
                    , stats_line_percent(furtherShrinkAttempt, conflStats.numConflicts)
                    , stats_line_percent(furtherShrinkedSuccess, furtherShrinkAttempt)
                    , "% attempt successful"
                );

                print_stats_line("c bintri-min lits"
                    , binTriShrinkedClause
                    , stats_line_percent(binTriShrinkedClause, litsRedNonMin)
                    , "% less overall"
                );

                print_stats_line("c cache-min lits"
                    , cacheShrinkedClause
                    , stats_line_percent(cacheShrinkedClause, litsRedNonMin)
                    , "% less overall"
                );

                print_stats_line("c stamp-min call%"
                    , stats_line_percent(stampShrinkAttempt, conflStats.numConflicts)
                    , stats_line_percent(stampShrinkCl, stampShrinkAttempt)
                    , "% attempt successful"
                );

                print_stats_line("c stamp-min lits"
                    , stampShrinkLit
                    , stats_line_percent(stampShrinkLit, litsRedNonMin)
                    , "% less overall"
                );

                print_stats_line("c final avg"
                    , ratio_for_stat(litsRedFinal, conflStats.numConflicts)
                );

                //General stats
                //print_stats_line("c Memory used", (double)mem_used / 1048576.0, " MB");
                #if !defined(_MSC_VER) && defined(RUSAGE_THREAD)
                print_stats_line("c single-thread CPU time", cpu_time, " s");
                #else
                print_stats_line("c all-threads sum CPU time", cpu_time, " s");
                #endif
            }

            //Restart stats
            uint64_t blocked_restart = 0;
            uint64_t blocked_restart_same = 0;
            uint64_t numRestarts = 0;

            //Decisions
            uint64_t  decisions = 0;
            uint64_t  decisionsAssump = 0;
            uint64_t  decisionsRand = 0;
            uint64_t  decisionFlippedPolar = 0;

            //Clause shrinking
            uint64_t litsRedNonMin = 0;
            uint64_t litsRedFinal = 0;
            uint64_t recMinCl = 0;
            uint64_t recMinLitRem = 0;
            uint64_t furtherShrinkAttempt = 0;
            uint64_t binTriShrinkedClause = 0;
            uint64_t cacheShrinkedClause = 0;
            uint64_t furtherShrinkedSuccess = 0;
            uint64_t stampShrinkAttempt = 0;
            uint64_t stampShrinkCl = 0;
            uint64_t stampShrinkLit = 0;
            uint64_t moreMinimLitsStart = 0;
            uint64_t moreMinimLitsEnd = 0;
            uint64_t recMinimCost = 0;

            //Learnt clause stats
            uint64_t learntUnits = 0;
            uint64_t learntBins = 0;
            uint64_t learntTris = 0;
            uint64_t learntLongs = 0;
            uint64_t otfSubsumed = 0;
            uint64_t otfSubsumedImplicit = 0;
            uint64_t otfSubsumedLong = 0;
            uint64_t otfSubsumedRed = 0;
            uint64_t otfSubsumedLitsGained = 0;

            //Hyper-bin & transitive reduction
            uint64_t advancedPropCalled = 0;
            uint64_t hyperBinAdded = 0;
            uint64_t transReduRemIrred = 0;
            uint64_t transReduRemRed = 0;

            //Resolution Stats
            ResolutionTypes<uint64_t> resolvs;

            //Stat structs
            ConflStats conflStats;

            //Time
            double cpu_time = 0.0;
        };

        size_t hyper_bin_res_all(const bool check_for_set_values = true);
        std::pair<size_t, size_t> remove_useless_bins(bool except_marked = false);
        bool var_inside_assumptions(const Var var) const
        {
            if (assumptionsSet.empty()) {
                return false;
            }

            assert(var < assumptionsSet.size());
            return assumptionsSet[var];
        }
        template<bool also_insert_varorder = true>
        void cancelUntil(uint32_t level); ///<Backtrack until a certain level.

    protected:
        void new_var(const bool bva, const Var orig_outer) override;
        void new_vars(const size_t n) override;
        void save_on_var_memory();
        void updateVars(
            const vector<uint32_t>& outerToInter
            , const vector<uint32_t>& interToOuter
        );

        struct AssumptionPair {
            AssumptionPair(const Lit _inter, const Lit _outer):
                lit_inter(_inter)
                , lit_orig_outside(_outer)
            {
            }

            Lit lit_inter;
            Lit lit_orig_outside; //not outer, but outside(!)

            bool operator<(const AssumptionPair& other) const
            {
                //Yes, we need reverse in terms of inverseness
                return ~lit_inter < ~other.lit_inter;
            }
        };
        void renumber_assumptions(const vector<Var>& outerToInter);
        void fill_assumptions_set_from(const vector<AssumptionPair>& fill_from);
        void unfill_assumptions_set_from(const vector<AssumptionPair>& unfill_from);
        vector<char> assumptionsSet; //Needed so checking is fast -- we cannot eliminate / component-handle such vars
        vector<AssumptionPair> assumptions; ///< Current set of assumptions provided to solve by the user.
        void update_assump_conflict_to_orig_outside(vector<Lit>& out_conflict);

        void add_in_partial_solving_stats();

        //For connection with Solver
        void  resetStats();

        Hist hist;
        #ifdef STATS_NEEDED_EXTRA
        vector<uint32_t>    clauseSizeDistrib;
        vector<uint32_t>    clauseGlueDistrib;
        boost::multi_array<uint32_t, 2> sizeAndGlue;
        #endif

        /////////////////
        //Settings
        Solver*   solver;          ///< Thread control class

        //Stats printing
        void printAgilityStats();

        /////////////////
        // Searching
        /// Search for a given number of conflicts.
        bool last_decision_ended_in_conflict;
        lbool search();
        lbool burst_search();
        bool  handle_conflict(PropBy confl);// Handles the conflict clause
        void  update_history_stats(size_t backtrack_level, size_t glue);
        void  attach_and_enqueue_learnt_clause(Clause* cl);
        void  print_learning_debug_info() const;
        void  print_learnt_clause() const;
        void  add_otf_subsume_long_clauses();
        void  add_otf_subsume_implicit_clause();
        Clause* handle_last_confl_otf_subsumption(Clause* cl, const size_t glue);
        lbool new_decision();  // Handles the case when decision must be made
        void  check_need_restart();     // Helper function to decide if we need to restart during search
        Lit   pickBranchLit();
        lbool otf_hyper_prop_first_dec_level(bool& must_continue);
        void  hyper_bin_update_cache(vector<Lit>& to_enqueue_toplevel);

        ///////////////
        // Conflicting
        struct SearchParams
        {
            SearchParams()
            {
                clear();
            }

            void clear()
            {
                update = true;
                needToStopSearch = false;
                conflictsDoneThisRestart = 0;
                numAgilityNeedRestart = 0;
            }

            bool needToStopSearch;
            bool update;
            uint64_t conflictsDoneThisRestart;
            uint64_t conflictsToDo;
            uint64_t numAgilityNeedRestart;
            Restart rest_type = restart_type_never;
        };
        SearchParams params;
        vector<Lit> learnt_clause;
        Clause* analyze_conflict(
            PropBy confl //The conflict that we are investigating
            , uint32_t& out_btlevel      //backtrack level
            , uint32_t &glue         //glue of the learnt clause
        );
        void update_clause_glue_from_analysis(Clause* cl);
        void minimize_learnt_clause();
        void mimimize_learnt_clause_more_maybe();
        void print_fully_minimized_learnt_clause() const;
        size_t find_backtrack_level_of_learnt();
        void bump_var_activities_based_on_last_decision_level(const uint32_t glue);
        Clause* otf_subsume_last_resolved_clause(Clause* last_resolved_long_cl);
        void print_debug_resolution_data(const PropBy confl);
        Clause* create_learnt_clause(PropBy confl);
        int pathC;
        ResolutionTypes<uint16_t> resolutions;

        vector<std::pair<Lit, uint32_t> > lastDecisionLevel; //for glue-based extra var activity bumping

        //OTF subsumption
        vector<ClOffset> otf_subsuming_long_cls;
        vector<OTFClause> otf_subsuming_short_cls;
        void check_otf_subsume(PropBy confl);
        void create_otf_subsuming_implicit_clause(const Clause& cl);
        void create_otf_subsuming_long_clause(
           Clause& cl
            , ClOffset offset
        );
        Clause* add_literals_from_confl_to_learnt(
            const PropBy confl
            , const Lit p
        );
        void debug_print_resolving_clause(const PropBy confl) const;
        size_t tmp_learnt_clause_size;
        cl_abst_type tmp_learnt_clause_abst;

        void add_lit_to_learnt(Lit lit);
        void analyze_final_confl_with_assumptions(const Lit p, vector<Lit>& out_conflict);

        //////////////
        // Conflict minimisation
        bool litRedundant(Lit p, uint32_t abstract_levels);
        void recursiveConfClauseMin();
        void normalClMinim();
        MyStack<Lit> analyze_stack;
        //void            prune_removable(vector<Lit>& out_learnt);
        //void            find_removable(const vector<Lit>& out_learnt, uint32_t abstract_level);
        //int             quick_keeper(Lit p, uint32_t abstract_level, bool maykeep);
        //int             dfs_removable(Lit p, uint32_t abstract_level);
        //void            mark_needed_removable(Lit p);
        //int             res_removable();
        uint32_t        abstractLevel(const Var x) const;
        //vector<PropBy> trace_reasons; // clauses to resolve to give CC
        //vector<Lit>     trace_lits_minim; // lits maybe used in minimization


        /////////////////
        //Graphical conflict generation
        void   create_graphviz_confl_graph     (PropBy conflPart);
        string analyze_confl_for_graphviz_graph (PropBy conflHalf, uint32_t& out_btlevel, uint32_t &glue);
        void print_edges_for_graphviz_file(std::ofstream& file) const;
        void print_vertex_definitions_for_graphviz_file(std::ofstream& file);
        void fill_seen_for_lits_connected_to_conflict_graph(
            vector<Lit>& lits
        );
        vector<Lit> get_lits_from_conflict(const PropBy conflPart);



        /////////////////
        // Variable activity
        vector<double> activities;
        double var_inc;
        void              insertVarOrder(const Var x);  ///< Insert a variable in heap


        uint64_t more_red_minim_limit_binary_actual;
        uint64_t more_red_minim_limit_cache_actual;
        const Stats& get_stats() const;
        size_t mem_used() const;

    private:
        bool blocked_restart = false;
        void check_blocking_restart();
        bool must_consolidate_mem = false;
        void print_solution_varreplace_status() const;
        void dump_search_sql(const double myTime);
        uint32_t num_search_called = 0;
        void reset_reason_levels_of_vars_to_zero();
        void rearrange_clauses_watches();
        Lit find_good_blocked_lit(const Clause& c) const override;

        ////////////
        // Transitive on-the-fly self-subsuming resolution
        void   minimise_redundant_more(vector<Lit>& cl);
        void   binary_based_more_minim(vector<Lit>& cl);
        void   cache_based_more_minim(vector<Lit>& cl);
        void   stamp_based_more_minim(vector<Lit>& cl);

        //For printint longest decision trail
        vector<Lit> longest_dec_trail;
        size_t last_confl_longest_dec_trail_printed = 0;
        void handle_longest_decision_trail();

        void calculate_and_set_polars();
        void restore_order_heap();

        //Variable activities
        struct VarFilter { ///Filter out vars that have been set or is not decision from heap
            const Searcher* cc;
            const Solver* solver;
            VarFilter(const Searcher* _cc, Solver* _solver) :
                cc(_cc)
                ,solver(_solver)
            {}
            bool operator()(uint32_t var) const;
        };

        ///Decay all variables with the specified factor. Implemented by increasing the 'bump' value instead.
        void     varDecayActivity ();
        ///Increase a variable with the current 'bump' value.
        void     bump_var_activitiy  (Var v);
        struct VarOrderLt { ///Order variables according to their activities
            const vector<double>&  activities;
            bool operator () (const Var x, const Var y) const
            {
                return activities[x] > activities[y];
            }

            VarOrderLt(const vector<double>& _activities) :
                activities(_activities)
            {}
        };
        ///activity-ordered heap of decision variables
        Heap<VarOrderLt> order_heap;

        //Clause activites
        double clauseActivityIncrease;
        void decayClauseAct();
        void bumpClauseAct(Clause* cl);

        //Other
        uint64_t lastRestartConfl;


        //SQL
        vector<Var> calcVarsToDump() const;
        #ifdef STATS_NEEDED
        void dump_restart_sql();
        void printVarStatsSQL();
        void printClauseDistribSQL();
        PropStats lastSQLPropStats;
        Stats lastSQLGlobalStats;
        void calcVariances(
            const vector<VarData>& data
            , double& avgDecLevelVar
            , double& avgTrailLevelVar
        );
        #endif


        //Other
        void print_solution_type(const lbool status) const;

        //Picking polarity when doing decision
        bool     pickPolarity(const Var var);

        //Last time we clean()-ed the clauses, the number of zero-depth assigns was this many
        size_t   lastCleanZeroDepthAssigns;

        //Used for on-the-fly subsumption. Does A subsume B?
        //Uses 'seen' to do its work
        bool subset(const vector<Lit>& A, const Clause& B);

        double   startTime; ///<When solve() was started
        Stats    stats;
        double   var_decay;
};

inline uint32_t Searcher::abstractLevel(const Var x) const
{
    return ((uint32_t)1) << (varData[x].level & 31);
}

inline const Searcher::Stats& Searcher::get_stats() const
{
    return stats;
}

inline const Searcher::Hist& Searcher::getHistory() const
{
    return hist;
}

inline void Searcher::add_in_partial_solving_stats()
{
    stats.cpu_time = cpuTime() - startTime;
}

/**
@brief Revert to the state at given level
*/
template<bool also_insert_varorder>
inline void Searcher::cancelUntil(uint32_t level)
{
    #ifdef VERBOSE_DEBUG
    cout << "Canceling until level " << level;
    if (level > 0) cout << " sublevel: " << trail_lim[level];
    cout << endl;
    #endif

    if (decisionLevel() > level) {

        //Go through in reverse order, unassign & insert then
        //back to the vars to be branched upon
        for (int sublevel = trail.size()-1
            ; sublevel >= (int)trail_lim[level]
            ; sublevel--
        ) {
            #ifdef VERBOSE_DEBUG
            cout
            << "Canceling lit " << trail[sublevel]
            << " sublevel: " << sublevel
            << endl;
            #endif

            #ifdef ANIMATE3D
            std:cerr << "u " << var << endl;
            #endif

            const Var var = trail[sublevel].var();
            assert(value(var) != l_Undef);
            assigns[var] = l_Undef;
            if (also_insert_varorder) {
                insertVarOrder(var);
            }
        }
        qhead = trail_lim[level];
        trail.resize(trail_lim[level]);
        trail_lim.resize(level);
    }

    #ifdef VERBOSE_DEBUG
    cout
    << "Canceling finished. Now at level: " << decisionLevel()
    << " sublevel: " << trail.size()-1
    << endl;
    #endif
}

inline void Searcher::insertVarOrder(const Var x)
{
    if (!order_heap.in_heap(x)
    ) {
        #ifdef SLOW_DEUG
        //All active varibles are decision variables
        assert(varData[x].is_decision);
        #endif

        order_heap.insert(x);
    }
}


inline void Searcher::bumpClauseAct(Clause* cl)
{
    assert(!cl->getRemoved());

    cl->stats.activity += clauseActivityIncrease;
    if (cl->stats.activity > 1e20 ) {
        // Rescale
        for(ClOffset offs: longRedCls) {
            cl_alloc.ptr(offs)->stats.activity *= 1e-20;
        }
        clauseActivityIncrease *= 1e-20;
        if (clauseActivityIncrease == 0.0) {
            clauseActivityIncrease = 1.0;
        }
    }
}

inline void Searcher::decayClauseAct()
{
    clauseActivityIncrease *= conf.clauseDecayActivity;
}


} //end namespace

#endif //__SEARCHER_H__
