/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2013, Mate Soos and collaborators. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
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
#include <boost/multi_array.hpp>
#include "time_mem.h"
#include "avgcalc.h"
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
    double avgDecLevelVarLT;
    double avgTrailLevelVarLT;
    double avgDecLevelVar;
    double avgTrailLevelVar;
};

class Searcher : public PropEngine
{
    public:
        Searcher(const SolverConf& _conf, Solver* solver);
        ~Searcher();

        //History
        struct Hist {
            //About the search
            AvgCalc<uint32_t>   branchDepthHist;     ///< Avg branch depth in current restart
            AvgCalc<uint32_t>   branchDepthHistLT;

            AvgCalc<uint32_t>   branchDepthDeltaHist;
            AvgCalc<uint32_t>   branchDepthDeltaHistLT;

            bqueue<uint32_t>   trailDepthHist;
            AvgCalc<uint32_t>   trailDepthHistLT;

            AvgCalc<uint32_t>   trailDepthDeltaHist;
            AvgCalc<uint32_t>   trailDepthDeltaHistLT;

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
            AvgCalc<bool>       conflictAfterConflictLT;

            AvgCalc<size_t>     watchListSizeTraversed;
            AvgCalc<size_t>     watchListSizeTraversedLT;

            AvgCalc<bool>       litPropagatedSomething;
            AvgCalc<bool>       litPropagatedSomethingLT;
            #endif

            size_t getMemUsed() const
            {
                size_t used = 0;
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
                litPropagatedSomething.clear();
                #endif
            }

            void setSize(const size_t shortTermHistorySize)
            {
                glueHist.clearAndResize(shortTermHistorySize);
                trailDepthHist.clearAndResize(shortTermHistorySize);
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
                << "/" << std::left  << branchDepthHistLT.avgPrint(1, 5)
                << " branchdd"

                << " " << std::right << branchDepthDeltaHist.avgPrint(1, 4)
                << "/" << std::left << branchDepthDeltaHistLT.avgPrint(1, 4)

                << " traild"
                << " " << std::right << trailDepthHist.getLongtTerm().avgPrint(0, 7)
                << "/" << std::left << trailDepthHistLT.avgPrint(0, 7)

                << " traildd"
                << " " << std::right << trailDepthDeltaHist.avgPrint(0, 5)
                << "/" << std::left << trailDepthDeltaHistLT.avgPrint(0, 5)
                ;

                cout << std::right;
            }

            uint64_t memUsed() const
            {
                uint64_t mem = sizeof(Hist);
                mem += glueHist.usedMem();
                return mem;
            }
        };

        //////////////////////////////
        // Problem specification:
        Var newVar(bool dvar = true); // Add a new variable that can be decided on or not

        ///////////////////////////////
        // Solving:
        ///Search for a model that respects a given set of assumptions.
        lbool solve(
            const vector<Lit>& assumps
            , const uint64_t maxConfls = std::numeric_limits<uint64_t>::max()
        );

        ///Search without assumptions.
        lbool solve(
            const uint64_t maxConfls = std::numeric_limits<uint64_t>::max()
        );
        vector<lbool> solution;     ///<Filled only if solve() returned l_True
        vector<Lit>   conflict;     ///<If problem is unsatisfiable (possibly under assumptions), this vector represent the final conflict clause expressed in the assumptions.
        PropBy propagate(
            Solver* solver = NULL
            #ifdef STATS_NEEDED
            , AvgCalc<size_t>* watchListSizeTraversed = NULL
            //, AvgCalc<bool>* litPropagatedSomething
            #endif
        );

        ///////////////////////////////
        // Stats
        //Restart print status
        uint64_t lastRestartPrint;
        uint64_t lastRestartPrintHeader;
        void     check_if_print_restart_stat(const lbool status);
        void     printRestartHeader() const;
        void     printRestartStats() const;
        void     printBaseStats() const;
        void     printClauseStats() const;
        uint64_t sumConflicts() const;
        uint64_t sumRestarts() const;
        const Hist& getHistory() const;

        void     setNeedToInterrupt();
        uint32_t getSavedActivity(Var var) const;
        uint32_t getVarInc() const;

        struct Stats
        {
            Stats() :
                // Stats
                numRestarts(0)

                //Decisions
                , decisions(0)
                , decisionsAssump(0)
                , decisionsRand(0)
                , decisionFlippedPolar(0)

                //Conflict generation
                , litsLearntNonMin(0)
                , litsLearntFinal(0)
                , recMinCl(0)
                , recMinLitRem(0)
                , furtherShrinkAttempt(0)
                , binTriShrinkedClause(0)
                , cacheShrinkedClause(0)
                , furtherShrinkedSuccess(0)
                , stampShrinkAttempt(0)
                , stampShrinkCl(0)
                , stampShrinkLit(0)
                , moreMinimLitsStart(0)
                , moreMinimLitsEnd(0)
                , recMinimCost(0)

                //Learnt stats
                , learntUnits(0)
                , learntBins(0)
                , learntTris(0)
                , learntLongs(0)
                , otfSubsumed(0)
                , otfSubsumedImplicit(0)
                , otfSubsumedLong(0)
                , otfSubsumedLearnt(0)
                , otfSubsumedLitsGained(0)

                //Hyper-bin & transitive reduction
                , advancedPropCalled(0)
                , hyperBinAdded(0)
                , transReduRemIrred(0)
                , transReduRemRed(0)

                //Time
                , cpu_time(0)

            {};

            void clear()
            {
                Stats stats;
                *this = stats;
            }

            Stats& operator+=(const Stats& other)
            {
                numRestarts += other.numRestarts;

                //Decisions
                decisions += other.decisions;
                decisionsAssump += other.decisionsAssump;
                decisionsRand += other.decisionsRand;
                decisionFlippedPolar += other.decisionFlippedPolar;

                //Conflict minimisation stats
                litsLearntNonMin += other.litsLearntNonMin;
                litsLearntFinal += other.litsLearntFinal;
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

                //Learnt stats
                learntUnits += other.learntUnits;
                learntBins += other.learntBins;
                learntTris += other.learntTris;
                learntLongs += other.learntLongs;
                otfSubsumed += other.otfSubsumed;
                otfSubsumedImplicit += other.otfSubsumedImplicit;
                otfSubsumedLong += other.otfSubsumedLong;
                otfSubsumedLearnt += other.otfSubsumedLearnt;
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

                //Decisions
                decisions -= other.decisions;
                decisionsAssump -= other.decisionsAssump;
                decisionsRand -= other.decisionsRand;
                decisionFlippedPolar -= other.decisionFlippedPolar;

                //Conflict minimisation stats
                litsLearntNonMin -= other.litsLearntNonMin;
                litsLearntFinal -= other.litsLearntFinal;
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

                //Learnt stats
                learntUnits -= other.learntUnits;
                learntBins -= other.learntBins;
                learntTris -= other.learntTris;
                learntLongs -= other.learntLongs;
                otfSubsumed -= other.otfSubsumed;
                otfSubsumedImplicit -= other.otfSubsumedImplicit;
                otfSubsumedLong -= other.otfSubsumedLong;
                otfSubsumedLearnt -= other.otfSubsumedLearnt;
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
                printStatsLine("c restarts"
                    , numRestarts
                    , (double)conflStats.numConflicts/(double)numRestarts
                    , "confls per restart"

                );
                printStatsLine("c time", cpu_time);
                printStatsLine("c decisions", decisions
                    , (double)decisionsRand*100.0/(double)decisions
                    , "% random"
                );

                printStatsLine("c decisions/conflicts"
                    , (double)decisions/(double)conflStats.numConflicts
                );
            }

            void printShort() const
            {
                //Restarts stats
                printCommon();
                conflStats.printShort(cpu_time);

                printStatsLine("c conf lits non-minim"
                    , litsLearntNonMin
                    , (double)litsLearntNonMin/(double)conflStats.numConflicts
                    , "lit/confl"
                );

                printStatsLine("c conf lits final"
                    , (double)litsLearntFinal/(double)conflStats.numConflicts
                );
            }

            void print() const
            {
                uint64_t mem_used = memUsed();
                printCommon();
                conflStats.print(cpu_time);

                /*assert(numConflicts
                    == conflsBin + conflsTri + conflsLongIrred + conflsLongRed);*/

                cout << "c LEARNT stats" << endl;
                printStatsLine("c units learnt"
                    , learntUnits
                    , (double)learntUnits/(double)conflStats.numConflicts*100.0
                    , "% of conflicts");

                printStatsLine("c bins learnt"
                    , learntBins
                    , (double)learntBins/(double)conflStats.numConflicts*100.0
                    , "% of conflicts");

                printStatsLine("c tris learnt"
                    , learntTris
                    , (double)learntTris/(double)conflStats.numConflicts*100.0
                    , "% of conflicts");

                printStatsLine("c long learnt"
                    , learntLongs
                    , (double)learntLongs/(double)conflStats.numConflicts*100.0
                    , "% of conflicts"
                );

                printStatsLine("c otf-subs"
                    , otfSubsumed
                    , (double)otfSubsumed/(double)conflStats.numConflicts
                    , "/conflict"
                );

                printStatsLine("c otf-subs implicit"
                    , otfSubsumedImplicit
                    , (double)otfSubsumedImplicit/(double)otfSubsumed*100.0
                    , "%"
                );

                printStatsLine("c otf-subs long"
                    , otfSubsumedLong
                    , (double)otfSubsumedLong/(double)otfSubsumed*100.0
                    , "%"
                );

                printStatsLine("c otf-subs learnt"
                    , otfSubsumedLearnt
                    , (double)otfSubsumedLearnt/(double)otfSubsumed*100.0
                    , "% otf subsumptions"
                );

                printStatsLine("c otf-subs lits gained"
                    , otfSubsumedLitsGained
                    , (double)otfSubsumedLitsGained/(double)otfSubsumed
                    , "lits/otf subsume"
                );

                cout << "c SEAMLESS HYPERBIN&TRANS-RED stats" << endl;
                printStatsLine("c advProp called"
                    , advancedPropCalled
                );
                printStatsLine("c hyper-bin add bin"
                    , hyperBinAdded
                    , (double)hyperBinAdded/(double)advancedPropCalled
                    , "bin/call"
                );
                printStatsLine("c trans-red rem irred bin"
                    , transReduRemIrred
                    , (double)transReduRemIrred/(double)advancedPropCalled
                    , "bin/call"
                );
                printStatsLine("c trans-red rem red bin"
                    , transReduRemRed
                    , (double)transReduRemRed/(double)advancedPropCalled
                    , "bin/call"
                );

                cout << "c CONFL LITS stats" << endl;
                printStatsLine("c orig "
                    , litsLearntNonMin
                    , (double)litsLearntNonMin/(double)conflStats.numConflicts
                    , "lit/confl"
                );

                printStatsLine("c rec-min effective"
                    , recMinCl
                    , (double)recMinCl/(double)conflStats.numConflicts*100.0
                    , "% attempt successful"
                );

                printStatsLine("c rec-min lits"
                    , recMinLitRem
                    , (double)recMinLitRem/(double)litsLearntNonMin*100.0
                    , "% less overall"
                );

                printStatsLine("c further-min call%"
                    , (double)furtherShrinkAttempt/(double)conflStats.numConflicts*100.0
                    , (double)furtherShrinkedSuccess/(double)furtherShrinkAttempt*100.0
                    , "% attempt successful"
                );

                printStatsLine("c bintri-min lits"
                    , binTriShrinkedClause
                    , (double)binTriShrinkedClause/(double)litsLearntNonMin*100.0
                    , "% less overall"
                );

                printStatsLine("c cache-min lits"
                    , cacheShrinkedClause
                    , (double)cacheShrinkedClause/(double)litsLearntNonMin*100.0
                    , "% less overall"
                );

                printStatsLine("c stamp-min call%"
                    , (double)stampShrinkAttempt/(double)conflStats.numConflicts*100.0
                    , (double)stampShrinkCl/(double)stampShrinkAttempt*100.0
                    , "% attempt successful"
                );

                printStatsLine("c stamp-min lits"
                    , stampShrinkLit
                    , (double)stampShrinkLit/(double)litsLearntNonMin*100.0
                    , "% less overall"
                );

                printStatsLine("c final avg"
                    , (double)litsLearntFinal/(double)conflStats.numConflicts
                );

                //General stats
                printStatsLine("c Memory used", (double)mem_used / 1048576.0, " MB");
                #if !defined(_MSC_VER) && defined(RUSAGE_THREAD)
                printStatsLine("c single-thread CPU time", cpu_time, " s");
                #else
                printStatsLine("c all-threads sum CPU time", cpu_time, " s");
                #endif
            }

            uint64_t  numRestarts;      ///<Num restarts

            //Decisions
            uint64_t  decisions;        ///<Number of decisions made
            uint64_t  decisionsAssump;
            uint64_t  decisionsRand;    ///<Numer of random decisions made
            uint64_t  decisionFlippedPolar; ///<While deciding, we flipped polarity

            uint64_t litsLearntNonMin;
            uint64_t litsLearntFinal;
            uint64_t recMinCl;
            uint64_t recMinLitRem;
            uint64_t furtherShrinkAttempt;
            uint64_t binTriShrinkedClause;
            uint64_t cacheShrinkedClause;
            uint64_t furtherShrinkedSuccess;
            uint64_t stampShrinkAttempt;
            uint64_t stampShrinkCl;
            uint64_t stampShrinkLit;
            uint64_t moreMinimLitsStart;
            uint64_t moreMinimLitsEnd;
            uint64_t recMinimCost;

            //Learnt stats
            uint64_t learntUnits;
            uint64_t learntBins;
            uint64_t learntTris;
            uint64_t learntLongs;
            uint64_t otfSubsumed;
            uint64_t otfSubsumedImplicit;
            uint64_t otfSubsumedLong;
            uint64_t otfSubsumedLearnt;
            uint64_t otfSubsumedLitsGained;

            //Hyper-bin & transitive reduction
            uint64_t advancedPropCalled;
            uint64_t hyperBinAdded;
            uint64_t transReduRemIrred;
            uint64_t transReduRemRed;

            //Resolution Stats
            ResolutionTypes<uint64_t> resolvs;

            //Stat structs
            ConflStats conflStats;

            //Time
            double cpu_time;
        };

    protected:
        friend class CalcDefPolars;
        friend class VarReplacer;
        void filterOrderHeap();
        void redoOrderHeap();

        //For connection with Solver
        void  resetStats();
        void  addInPartialSolvingStat();

        //For hyper-bin and transitive reduction
        size_t hyperBinResAll();
        std::pair<size_t, size_t> removeUselessBins();

        Hist hist;
        #ifdef STATS_NEEDED
        vector<uint32_t>    clauseSizeDistrib;
        vector<uint32_t>    clauseGlueDistrib;
        boost::multi_array<uint32_t, 2> sizeAndGlue;
        #endif

        /////////////////
        //Settings
        Solver*   solver;          ///< Thread control class
        MTRand           mtrand;           ///< random number generator
        SolverConf       conf;             ///< Solver config for this thread
        bool             needToInterrupt;  ///<If set to TRUE, interrupt cleanly ASAP

        //Stats printing
        void printAgilityStats();

        /////////////////
        // Searching
        /// Search for a given number of conflicts.
        lbool search(
            uint64_t* geom_max
        );
        lbool burstSearch();
        bool  handle_conflict(PropBy confl);// Handles the conflict clause
        lbool new_decision();  // Handles the case when decision must be made
        void  checkNeedRestart(uint64_t* geom_max);     // Helper function to decide if we need to restart during search
        RestartType decide_restart_type() const;
        Lit   pickBranchLit();                             // Return the next decision variable.

        ///////////////
        // Conflicting
        struct SearchParams
        {
            SearchParams() :
                rest_type(no_restart)
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
            RestartType rest_type;
        };
        SearchParams params;
        void     cancelUntil      (uint32_t level);                        ///<Backtrack until a certain level.
        vector<Lit> learnt_clause;
        Clause* analyze(
            PropBy confl //The conflict that we are investigating
            , uint32_t& out_btlevel      //backtrack level
            , uint32_t &nblevels         //glue of the learnt clause
            , ResolutionTypes<uint16_t> &resolutions   //number of resolutions mades
            , bool fromProber = false
        );

        vector<std::pair<Lit, size_t> > lastDecisionLevel; //for glue-based extra var activity bumping

        //OTF subsumption
        vector<ClOffset> toAttachLater;
        void doOTFSubsume(PropBy confl);
        vector<OTFClause> otfMustAttach;
        //set<Lit> learnt_clause2;
        size_t learnt_clause2_size;
        CL_ABST_TYPE learnt_clause2_abst;

        void analyzeHelper(
            Lit lit
            , int& pathC
            , bool fromProber
        );
        void     analyzeFinal     (const Lit p, vector<Lit>& out_conflict);

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
        void         genConfGraph     (PropBy conflPart);
        string simplAnalyseGraph (PropBy conflHalf, uint32_t& out_btlevel, uint32_t &glue);

        /////////////////
        // Variable activity
        vector<uint32_t> activities;
        uint32_t var_inc;
        void              insertVarOrder(const Var x);  ///< Insert a variable in heap
        void  genRandomVarActMultDiv();

        ////////////
        // Transitive on-the-fly self-subsuming resolution
        void   minimiseLearntFurther(vector<Lit>& cl);
        void   stampBasedLearntMinim(vector<Lit>& cl);
        const Stats& getStats() const;
        uint64_t memUsedSearch() const;

    private:

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
        void     varBumpActivity  (Var v);
        struct VarOrderLt { ///Order variables according to their activities
            const vector<uint32_t>&  activities;
            bool operator () (const uint32_t x, const uint32_t y) const
            {
                return activities[x] > activities[y];
            }

            VarOrderLt(const vector<uint32_t>& _activities) :
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
        friend class SQLStats;
        vector<Var> calcVarsToDump() const;
        #ifdef STATS_NEEDED
        void printRestartSQL();
        void printVarStatsSQL();
        void printClauseDistribSQL();
        PropStats lastSQLPropStats;
        Stats lastSQLGlobalStats;
        void calcVariancesLT(
            double& avgDecLevelVar
            , double& avgTrailLevelVar
        );
        void calcVariances(
            double& avgDecLevelVar
            , double& avgTrailLevelVar
        );
        #endif

        //Assumptions
        vector<Lit> assumptions; ///< Current set of assumptions provided to solve by the user.

        //Picking polarity when doing decision
        bool     pickPolarity(const Var var);

        //Last time we clean()-ed the clauses, the number of zero-depth assigns was this many
        size_t   lastCleanZeroDepthAssigns;

        //Used for on-the-fly subsumption. Does A subsume B?
        //Uses 'seen' to do its work
        bool subset(const vector<Lit>& A, const Clause& B);

        double   startTime; ///<When solve() was started
        Stats    stats;
        size_t   origTrailSize;
        uint32_t var_inc_multiplier;
        uint32_t var_inc_divider;
};

inline void Searcher::varDecayActivity()
{
    var_inc *= var_inc_multiplier;
    var_inc /= var_inc_divider;
}
inline void Searcher::varBumpActivity(Var var)
{
    activities[var] += var_inc;
    if ( (activities[var]) > ((0x1U) << 24)
        || var_inc > ((0x1U) << 24)
    ) {
        // Rescale:
        for (vector<uint32_t>::iterator
            it = activities.begin()
            , end = activities.end()
            ; it != end
            ; it++
        ) {
            *it >>= 14;
        }

        //Reset var_inc
        var_inc >>= 14;

        //If var_inc is smaller than var_inc_start then this MUST be corrected
        //otherwise the 'varDecayActivity' may not decay anything in fact
        if (var_inc < conf.var_inc_start) {
            /*cout
            << "WHAAAAAAAAAAAAAT!!!? var_inc < conf.var_inc_start ! "
            << var_inc
            << ", "
            << conf.var_inc_start
            << endl;*/

            var_inc = conf.var_inc_start;
        }
    }

    // Update order_heap with respect to new activity:
    if (order_heap.inHeap(var))
        order_heap.decrease(var);
}

inline uint32_t Searcher::abstractLevel(const Var x) const
{
    return ((uint32_t)1) << (varData[x].level % 32);
}

inline lbool Searcher::solve(const uint64_t maxConfls)
{
    vector<Lit> tmp;
    return solve(tmp, maxConfls);
}

inline uint32_t Searcher::getSavedActivity(Var var) const
{
    return activities[var];
}

inline uint32_t Searcher::getVarInc() const
{
    return var_inc;
}

inline const Searcher::Stats& Searcher::getStats() const
{
    return stats;
}

inline void Searcher::addInPartialSolvingStat()
{
    stats.cpu_time = cpuTime() - startTime;
}

inline const Searcher::Hist& Searcher::getHistory() const
{
    return hist;
}

inline void Searcher::filterOrderHeap()
{
    order_heap.filter(VarFilter(this, solver));
}

} //end namespace

#endif //__SEARCHER_H__
