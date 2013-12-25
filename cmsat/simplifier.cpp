/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2013, Mate Soos and collaborators. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
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

#include "time_mem.h"
#include "assert.h"
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <set>
#include <algorithm>
#include <fstream>
#include <set>
#include <iostream>
#include <limits>


#include "simplifier.h"
#include "clause.h"
#include "solver.h"
#include "clausecleaner.h"
#include "constants.h"
#include "solutionextender.h"
#include "gatefinder.h"
#include "varreplacer.h"
#include "varupdatehelper.h"
#include "completedetachreattacher.h"
#include "subsumestrengthen.h"
#include "watchalgos.h"
#include "clauseallocator.h"
#include "xorfinderabst.h"
#include "subsumeimplicit.h"

#ifdef USE_M4RI
#include "xorfinder.h"
#endif

//#define VERBOSE_DEBUG
#ifdef VERBOSE_DEBUG
#define BIT_MORE_VERBOSITY
#define VERBOSE_ORGATE_REPLACE
#define VERBOSE_ASYMTE
#define VERBOSE_GATE_REMOVAL
#define VERBOSE_XORGATE_MIX
#define VERBOSE_DEBUG_XOR_FINDER
#define VERBOSE_DEBUG_VARELIM
#endif

using namespace CMSat;
using std::cout;
using std::endl;

//#define VERBOSE_DEBUG_VARELIM
//#define VERBOSE_DEBUG_XOR_FINDER
//#define BIT_MORE_VERBOSITY
//#define TOUCH_LESS
//#define VERBOSE_ORGATE_REPLACE
//#define VERBOSE_DEBUG_ASYMTE
//#define VERBOSE_GATE_REMOVAL
//#define VERBOSE_XORGATE_MIX

Simplifier::Simplifier(Solver* _solver):
    solver(_solver)
    , seen(solver->seen)
    , seen2(solver->seen2)
    , toClear(solver->toClear)
    , varElimOrder(VarOrderLt(varElimComplexity))
    , var_bva_order(VarBVAOrder(watch_irred_sizes))
    , xorFinder(NULL)
    , gateFinder(NULL)
    , anythingHasBeenBlocked(false)
    , blockedMapBuilt(false)
{
    xorFinder = new XorFinderAbst();
    #ifdef USE_M4RI
    if (solver->conf.doFindXors) {
        delete xorFinder;
        xorFinder = new XorFinder(this, solver);
    }
    #endif
    subsumeStrengthen = new SubsumeStrengthen(this, solver);

    if (solver->conf.doGateFind) {
        gateFinder = new GateFinder(this, solver);
    }
}

Simplifier::~Simplifier()
{
    delete xorFinder;
    delete subsumeStrengthen;
    delete gateFinder;
}

void Simplifier::newVar(const Var orig_outer)
{
    if (solver->conf.doGateFind
        && solver->nVars() > 10ULL*1000ULL*1000ULL
    ) {
        if (solver->conf.verbosity >= 2) {
            cout
            << "c [simp] gate finder switched off due to"
            << " excessive number of variables (we may run out of memory)"
            << endl;
        }
        delete gateFinder;
        gateFinder = NULL;
        solver->conf.doGateFind = false;
    }

    if (solver->conf.doGateFind) {
        gateFinder->newVar(orig_outer);
    }
}

void Simplifier::saveVarMem()
{
    if (gateFinder)
        gateFinder->saveVarMem();
}

void Simplifier::print_blocked_clauses_reverse() const
{
    for(vector<BlockedClause>::const_reverse_iterator
        it = blockedClauses.rbegin(), end = blockedClauses.rend()
        ; it != end
        ; it++
    ) {
        if (it->dummy) {
            cout
            << "dummy blocked clause for literal " << it->blockedOn
            << endl;
        } else {
            cout
            << "blocked clause " << it->lits
            << " blocked on var "
            << solver->map_outer_to_inter(it->blockedOn.var()) + 1
            << endl;
        }
    }
}

void Simplifier::extendModel(SolutionExtender* extender)
{
    //Either a variable is not eliminated, or its value is undef
    for(size_t i = 0; i < solver->nVarsReal(); i++) {
        const Var outer = solver->map_inter_to_outer(i);
        assert(solver->varData[i].removed != Removed::elimed
            || (solver->value(i) == l_Undef && solver->model[outer] == l_Undef)
        );
    }

    cleanBlockedClauses();
    #ifdef VERBOSE_DEBUG_RECONSTRUCT
    cout << "Number of blocked clauses:" << blockedClauses.size() << endl;
    print_blocked_clauses_reverse();
    #endif

    //go through in reverse order
    for (vector<BlockedClause>::const_reverse_iterator
        it = blockedClauses.rbegin(), end = blockedClauses.rend()
        ; it != end
        ; it++
    ) {
        if (it->dummy) {
            extender->dummyBlocked(it->blockedOn);
        } else {
            extender->addClause(it->lits, it->blockedOn);
        }
    }
}

/**
@brief Removes&free-s a clause from everywhere
*/
void Simplifier::unlinkClause(const ClOffset offset, bool doDrup)
{
    Clause& cl = *solver->clAllocator.getPointer(offset);
    if (solver->drup->enabled() && doDrup) {
       (*solver->drup) << del << cl << fin;
    }

    //Remove from occur
    for (uint32_t i = 0; i < cl.size(); i++) {
        *limit_to_decrease -= 2*solver->watches[cl[i].toInt()].size();

        removeWCl(solver->watches[cl[i].toInt()], offset);

        if (!cl.red())
            touched.touch(cl[i]);
    }

    if (cl.red()) {
        solver->litStats.redLits -= cl.size();
    } else {
        solver->litStats.irredLits -= cl.size();
    }

    //Free and set to NULL
    solver->clAllocator.clauseFree(&cl);
}

lbool Simplifier::cleanClause(ClOffset offset)
{
    assert(solver->ok);

    bool satisfied = false;
    Clause& cl = *solver->clAllocator.getPointer(offset);
    (*solver->drup) << deldelay << cl << fin;
    #ifdef VERBOSE_DEBUG
    cout << "Clause to clean: " << cl << endl;
    for(size_t i = 0; i < cl.size(); i++) {
        cout << cl[i] << " : "  << solver->value(cl[i]) << " , ";
    }
    cout << endl;
    #endif

    Lit* i = cl.begin();
    Lit* j = cl.begin();
    const Lit* end = cl.end();
    *limit_to_decrease -= cl.size();
    for(; i != end; i++) {
        if (solver->value(*i) == l_Undef) {
            *j++ = *i;
            continue;
        }

        if (solver->value(*i) == l_True)
            satisfied = true;

        if (solver->value(*i) == l_True
            || solver->value(*i) == l_False
        ) {
            removeWCl(solver->watches[i->toInt()], offset);
        }
    }
    cl.shrink(i-j);

    if (satisfied) {
        #ifdef VERBOSE_DEBUG
        cout << "Clause cleaning -- satisfied, removing" << endl;
        #endif
        (*solver->drup) << findelay;
        unlinkClause(offset, false);
        return l_True;
    }

    //Update lits stat
    if (cl.red())
        solver->litStats.redLits -= i-j;
    else
        solver->litStats.irredLits -= i-j;

    if (solver->conf.verbosity >= 6 || bva_verbosity) {
        cout << "-> Clause became after cleaning:" << cl << endl;
    }

    if (i-j > 0) {
        (*solver->drup) << cl << fin << findelay;
    }

    switch(cl.size()) {
        case 0:
            unlinkClause(offset, false);
            solver->ok = false;
            return l_False;

        case 1:
            solver->enqueue(cl[0]);
            #ifdef STATS_NEEDED
            solver->propStats.propsUnit++;
            #endif
            unlinkClause(offset, false);
            return l_True;

        case 2:
            solver->attachBinClause(cl[0], cl[1], cl.red());
            unlinkClause(offset, false);
            return l_True;

        case 3:
            solver->attachTriClause(cl[0], cl[1], cl[2], cl.red());
            unlinkClause(offset, false);
            return l_True;

        default:
            cl.setStrenghtened();
            return l_Undef;
    }
}

uint64_t Simplifier::calc_mem_usage_of_occur(const vector<ClOffset>& toAdd) const
{
     uint64_t memUsage = 0;
    for (vector<ClOffset>::const_iterator
        it = toAdd.begin(), end = toAdd.end()
        ; it !=  end
        ; it++
    ) {
        Clause* cl = solver->clAllocator.getPointer(*it);
        //*2 because of the overhead of allocation
        memUsage += cl->size()*sizeof(Watched)*2;
    }

    //Estimate malloc overhead
    memUsage += solver->numActiveVars()*2*40;

    return memUsage;
}

void Simplifier::print_mem_usage_of_occur(bool irred, uint64_t memUsage) const
{
    if (solver->conf.verbosity >= 2) {
        cout
        << "c [simp] mem usage for occur of "
        << (irred ?  "irred" : "red  ")
        << " " << std::setw(6) << memUsage/(1024ULL*1024ULL) << " MB"
        << endl;
    }
}

void Simplifier::print_linkin_data(const LinkInData link_in_data) const
{
    if (solver->conf.verbosity < 2)
        return;

    double val;
    if (link_in_data.cl_linked + link_in_data.cl_not_linked == 0) {
        val = 0;
    } else {
        val = (double)link_in_data.cl_not_linked/(double)(link_in_data.cl_linked+link_in_data.cl_not_linked)*100.0;
    }

    cout
    << "c [simp] Not linked in red "
    << link_in_data.cl_not_linked << "/"
    << (link_in_data.cl_linked + link_in_data.cl_not_linked)
    << " ("
    << std::setprecision(2) << std::fixed
    << val
    << " %)"
    << endl;
}


Simplifier::LinkInData Simplifier::link_in_clauses(
    const vector<ClOffset>& toAdd
    , bool irred
    , bool alsoOccur
) {
    LinkInData link_in_data;
    uint64_t linkedInLits = 0;
    for (vector<ClOffset>::const_iterator
        it = toAdd.begin(), end = toAdd.end()
        ; it !=  end
        ; it++
    ) {
        Clause* cl = solver->clAllocator.getPointer(*it);

        //Sanity check that the value given as irred is correct
        assert(
            (irred && !cl->red())
            || (!irred && cl->red())
        );

        if (alsoOccur
            //If irreduntant or (small enough AND link in limit not reached)
            && (irred
                || (cl->size() < solver->conf.maxRedLinkInSize
                    && linkedInLits < (solver->conf.maxOccurRedLitLinkedM*1000ULL*1000ULL))
            )
        ) {
            linkInClause(*cl);
            link_in_data.cl_linked++;
            linkedInLits += cl->size();
        } else {
            assert(cl->red());
            cl->setOccurLinked(false);
            link_in_data.cl_not_linked++;
        }

        clauses.push_back(*it);
    }
    clause_lits_added += linkedInLits;

    return link_in_data;
}

bool Simplifier::decide_occur_limit(bool irred, uint64_t memUsage)
{
    //over + irred -> exit
    if (irred
        && memUsage/(1024ULL*1024ULL) > solver->conf.maxOccurIrredMB
    ) {
        if (solver->conf.verbosity >= 2) {
            cout
            << "c [simp] Not linking in irred due to excessive expected memory usage"
            << endl;
        }
        return false;
    }

    //over + red -> don't link
    if (!irred
        && memUsage/(1024ULL*1024ULL) > solver->conf.maxOccurRedMB
    ) {
        if (solver->conf.verbosity >= 2) {
            cout
            << "c [simp] Not linking in red due to excessive expected memory usage"
            << endl;
        }

        return false;
    }

    return true;
}

bool Simplifier::addFromSolver(
    vector<ClOffset>& toAdd
    , bool alsoOccur
    , bool irred
) {
    //solver->printWatchMemUsed();

    if (alsoOccur) {
        uint64_t memUsage = calc_mem_usage_of_occur(toAdd);
        print_mem_usage_of_occur(irred, memUsage);
        alsoOccur = decide_occur_limit(irred, memUsage);
        if (irred && !alsoOccur)
            return false;
    }

    if (!irred && alsoOccur) {
        std::sort(toAdd.begin(), toAdd.end(), ClauseSizeSorter(solver->clAllocator));
    }

    LinkInData link_in_data = link_in_clauses(toAdd, irred, alsoOccur);
    toAdd.clear();
    if (!irred)
        print_linkin_data(link_in_data);

    return true;
}

bool Simplifier::check_varelim_when_adding_back_cl(const Clause* cl) const
{
    bool notLinkedNeedFree = false;
    for (Clause::const_iterator
        it2 = cl->begin(), end2 = cl->end()
        ; it2 != end2
        ; it2++
    ) {
        //The clause was too long, and wasn't linked in
        //but has been var-elimed, so remove it
        if (!cl->getOccurLinked()
            && solver->varData[it2->var()].removed == Removed::elimed
        ) {
            notLinkedNeedFree = true;
        }

        if (cl->getOccurLinked()
            && solver->varData[it2->var()].removed != Removed::none
            && solver->varData[it2->var()].removed != Removed::queued_replacer
        ) {
            cout
            << "ERROR! Clause " << *cl
            << " red: " << cl->red()
            << " contains lit " << *it2
            << " which has removed status"
            << removed_type_to_string(solver->varData[it2->var()].removed)
            << endl;

            assert(false);
            exit(-1);
        }
    }

    return notLinkedNeedFree;
}

void Simplifier::addBackToSolver()
{
    for (vector<ClOffset>::const_iterator
        it = clauses.begin(), end = clauses.end()
        ; it != end
        ; it++
    ) {
        Clause* cl = solver->clAllocator.getPointer(*it);
        if (cl->getFreed())
            continue;

        //All clauses are larger than 2-long
        assert(cl->size() > 3);

        bool notLinkedNeedFree = check_varelim_when_adding_back_cl(cl);
        if (notLinkedNeedFree) {
            //The clause wasn't linked in but needs removal now
            if (cl->red()) {
                solver->litStats.redLits -= cl->size();
            } else {
                solver->litStats.irredLits -= cl->size();
            }
            solver->clAllocator.clauseFree(cl);
            continue;
        }

        if (completeCleanClause(*cl)) {
            solver->attachClause(*cl);
            if (cl->red()) {
                solver->longRedCls.push_back(*it);
            } else {
                solver->longIrredCls.push_back(*it);
            }
        } else {
            solver->clAllocator.clauseFree(cl);
        }
    }
}

bool Simplifier::completeCleanClause(Clause& cl)
{
    assert(cl.size() > 3);
    (*solver->drup) << deldelay << cl << fin;

    //Remove all lits from stats
    //we will re-attach the clause either way
    if (cl.red()) {
        solver->litStats.redLits -= cl.size();
    } else {
        solver->litStats.irredLits -= cl.size();
    }

    Lit *i = cl.begin();
    Lit *j = i;
    for (Lit *end = cl.end(); i != end; i++) {
        if (solver->value(*i) == l_True) {

            (*solver->drup) << findelay;
            return false;
        }

        if (solver->value(*i) == l_Undef) {
            *j++ = *i;
        }
    }
    cl.shrink(i-j);

    //Drup
    if (i - j > 0) {
        (*solver->drup) << cl << fin << findelay;
    }

    switch (cl.size()) {
        case 0:
            solver->ok = false;
            return false;

        case 1:
            solver->enqueue(cl[0]);
            #ifdef STATS_NEEDED
            solver->propStats.propsUnit++;
            #endif
            return false;

        case 2:
            solver->attachBinClause(cl[0], cl[1], cl.red());
            return false;

        case 3:
            solver->attachTriClause(cl[0], cl[1], cl[2], cl.red());
            return false;

        default:
            return true;
    }

    return true;
}

void Simplifier::removeAllLongsFromWatches()
{
    for (watch_array::iterator
        it = solver->watches.begin(), end = solver->watches.end()
        ; it != end
        ; ++it
    ) {
        watch_subarray ws = *it;

        watch_subarray::iterator i = ws.begin();
        watch_subarray::iterator j = i;
        for (watch_subarray::iterator end2 = ws.end(); i != end2; i++) {
            if (i->isClause()) {
                continue;
            } else {
                assert(i->isBinary() || i->isTri());
                *j++ = *i;
            }
        }
        ws.shrink(i - j);
    }
}

void Simplifier::eliminate_empty_resolvent_vars()
{
    uint32_t var_elimed = 0;
    double myTime = cpuTime();
    limit_to_decrease = &empty_varelim_time_limit;

    size_t num = 0;
    for(size_t var = solver->mtrand.randInt(solver->nVars())
        ; num < solver->nVars()
        && var < solver->nVars()
        && *limit_to_decrease > 0
        ; var = (var + 1) % solver->nVars(), num++
    ) {
        if (!can_eliminate_var(var))
            continue;

        const Lit lit = Lit(var, false);
        if (!checkEmptyResolvent(lit))
            continue;

        create_dummy_blocked_clause(lit);
        rem_cls_from_watch_due_to_varelim(solver->watches[lit.toInt()], lit);
        rem_cls_from_watch_due_to_varelim(solver->watches[(~lit).toInt()], ~lit);
        set_var_as_eliminated(var, lit);
        var_elimed++;
    }

    if (solver->conf.verbosity >= 2) {
        cout
        << "c Empty resolvent elimed: " << var_elimed
        << " T:" << (cpuTime() - myTime)
        << " T-out: " << (*limit_to_decrease <= 0 ? "Y" : "N")
        << endl;
    }
}

bool Simplifier::can_eliminate_var(const Var var) const
{
    if (solver->value(var) != l_Undef
        || solver->varData[var].removed != Removed::none
        ||  solver->assumptionsSet[var]
    ) {
        return false;
    }

    return true;
}

bool Simplifier::eliminateVars()
{
    //Set-up
    double myTime = cpuTime();
    size_t vars_elimed = 0;
    size_t wenThrough = 0;
    limit_to_decrease = &norm_varelim_time_limit;

    order_vars_for_elim();
    if (solver->conf.verbosity >= 5) {
        cout << "c #order size:" << varElimOrder.size() << endl;
    }

    //Go through the ordered list of variables to eliminate
    while(!varElimOrder.empty()
        && *limit_to_decrease > 0
        && varelim_num_limit > 0
    ) {
        assert(limit_to_decrease == &norm_varelim_time_limit);
        Var var = varElimOrder.removeMin();

        //Stats
        *limit_to_decrease -= 20;
        wenThrough++;

        //Print status
        if (solver->conf.verbosity >= 5
            && wenThrough % 200 == 0
        ) {
            cout << "toDecrease: " << *limit_to_decrease << endl;
        }

        if (!can_eliminate_var(var))
            continue;

        //Try to eliminate
        if (maybeEliminate(var)) {
            vars_elimed++;
            varelim_num_limit--;
        }
        if (!solver->ok)
            goto end;
    }

end:
    if (solver->conf.verbosity >= 2) {
        cout << "c  #try to eliminate: " << wenThrough << endl
        << "c  #var-elim: " << vars_elimed << endl
        << "c  #T-out: " << ((*limit_to_decrease <= 0) ? "Y" : "N") << endl
        << "c  #T: " << (cpuTime() - myTime) << endl;
    }
    assert(limit_to_decrease == &norm_varelim_time_limit);

    runStats.varElimTimeOut += (*limit_to_decrease <= 0);
    runStats.varElimTime += cpuTime() - myTime;

    return solver->ok;
}

bool Simplifier::propagate()
{
    if (!solver->okay())
        return false;

    while (solver->qhead < solver->trail.size()) {
        const Lit p = solver->trail[solver->qhead];
        solver->qhead++;
        watch_subarray ws = solver->watches[(~p).toInt()];

        //Go through each occur
        for (watch_subarray::const_iterator
            it = ws.begin(), end = ws.end()
            ; it != end
            ; it++
        ) {
            if (it->isClause()) {
                if (!propagate_long_clause(it->getOffset()))
                    return false;
            }

            if (it->isTri()) {
                if (!propagate_tri_clause(*it))
                    return false;
            }

            if (it->isBinary()) {
                if (!propagate_binary_clause(*it))
                    return false;
            }
        }
    }

    return true;
}

bool Simplifier::propagate_tri_clause(const Watched& ws)
{
    const lbool val2 = solver->value(ws.lit2());
    const lbool val3 = solver->value(ws.lit3());
    if (val2 == l_True
        || val3 == l_True
    ) {
        return true;
    }

    if (val2 == l_Undef
        && val3 == l_Undef
    ) {
        return true;
    }

    if (val2 == l_False
        && val3 == l_False
    ) {
        solver->ok = false;
        return false;
    }

    #ifdef STATS_NEEDED
    if (ws.red())
        solver->propStats.propsTriRed++;
    else
        solver->propStats.propsTriIrred++;
    #endif

    if (val2 == l_Undef) {
        solver->enqueue(ws.lit2());
    } else {
        solver->enqueue(ws.lit3());
    }
    return true;
}

bool Simplifier::propagate_binary_clause(const Watched& ws)
{
    const lbool val = solver->value(ws.lit2());
    if (val == l_False) {
        solver->ok = false;
        return false;
    }

    if (val == l_Undef) {
        solver->enqueue(ws.lit2());
        #ifdef STATS_NEEDED
        if (ws.red())
            solver->propStats.propsBinRed++;
        else
            solver->propStats.propsBinIrred++;
        #endif
    }

    return true;
}

bool Simplifier::propagate_long_clause(const ClOffset offset)
{
    const Clause& cl = *solver->clAllocator.getPointer(offset);
    assert(!cl.getFreed() && "Cannot be already removed in occur");

    Lit lastUndef = lit_Undef;
    uint32_t numUndef = 0;
    bool satisfied = false;
    for (const Lit lit: cl) {
        const lbool val = solver->value(lit);
        if (val == l_True) {
            satisfied = true;
            break;
        }
        if (val == l_Undef) {
            numUndef++;
            if (numUndef > 1) break;
            lastUndef = lit;
        }
    }
    if (satisfied)
        return true;

    //Problem is UNSAT
    if (numUndef == 0) {
        solver->ok = false;
        return false;
    }

    if (numUndef > 1)
        return true;

    solver->enqueue(lastUndef);
    #ifdef STATS_NEEDED
    if (cl.size() == 3)
        if (cl.red())
            solver->propStats.propsTriRed++;
        else
            solver->propStats.propsTriIrred++;
    else {
        if (cl.red())
            solver->propStats.propsLongRed++;
        else
            solver->propStats.propsLongIrred++;
    }
    #endif

    return true;
}

void Simplifier::subsumeReds()
{
    double myTime = cpuTime();

    //Test & debug
    solver->testAllClauseAttach();
    solver->checkNoWrongAttach();
    assert(solver->varReplacer->getNewToReplaceVars() == 0
            && "Cannot work in an environment when elimnated vars could be replaced by other vars");

    //If too many clauses, don't do it
    if (solver->getNumLongClauses() > 10000000UL
        || solver->litStats.irredLits > 50000000UL
    )  return;

    //Setup
    clause_lits_added = 0;
    runStats.clear();
    clauses.clear();
    limit_to_decrease = &strengthening_time_limit;
    size_t origTrailSize = solver->trail.size();

    //Remove all long clauses from watches
    removeAllLongsFromWatches();

    //Add red to occur
    runStats.origNumRedLongClauses = solver->longRedCls.size();
    addFromSolver(
        solver->longRedCls
        , true //try to add to occur
        , false //irreduntant?
    );
    solver->longRedCls.clear();
    runStats.origNumFreeVars = solver->getNumFreeVars();
    setLimits();

    //Print link-in and startup time
    double linkInTime = cpuTime() - myTime;
    runStats.linkInTime += linkInTime;

    //Carry out subsume0
    subsumeStrengthen->performSubsumption();

    //Add irred to occur, but only temporarily
    runStats.origNumIrredLongClauses = solver->longIrredCls.size();
    addFromSolver(solver->longIrredCls
        , false //try to add to occur
        , true //irreduntant?
    );
    solver->longIrredCls.clear();

    //Add back clauses to solver etc
    finishUp(origTrailSize);

    if (solver->conf.verbosity >= 1) {
        subsumeStrengthen->getRunStats().printShort();
    }
}

void Simplifier::checkAllLinkedIn()
{
    for(vector<ClOffset>::const_iterator
        it = clauses.begin(), end = clauses.end()
        ; it != end
        ; it++
    ) {
        Clause& cl = *solver->clAllocator.getPointer(*it);

        assert(cl.red() || cl.getOccurLinked());
        if (cl.freed() || cl.red())
            continue;

        for(size_t i = 0; i < cl.size(); i++) {
            Lit lit = cl[i];
            bool found = findWCl(solver->watches[lit.toInt()], *it);
            assert(found);
        }
    }
}


bool Simplifier::simplify()
{
    assert(solver->okay());

    //Test & debug
    solver->testAllClauseAttach();
    solver->checkNoWrongAttach();
    assert(solver->varReplacer->getNewToReplaceVars() == 0
            && "Cannot work in an environment when elimnated vars could be replaced by other vars");

    //Clean the clauses before playing with them
    solver->clauseCleaner->removeAndCleanAll();

    //If too many clauses, don't do it
    if (solver->getNumLongClauses() > 10ULL*1000ULL*1000ULL
        || solver->litStats.irredLits > 50ULL*1000ULL*1000ULL
    ) {
        return solver->okay();
    }

    //Setup
    clause_lits_added = 0;
    runStats.clear();
    runStats.numCalls++;
    clauses.clear();
    limit_to_decrease = &strengthening_time_limit;

    double myTime = cpuTime();
    removeAllLongsFromWatches();
    if (!fill_occur())
        return solver->okay();
    sanityCheckElimedVars();
    const double linkInTime = cpuTime() - myTime;

    //Print memory usage after occur link-in
    if (solver->conf.verbosity >= 2) {
        solver->printWatchMemUsed(memUsedTotal());
    }

    setLimits();
    runStats.linkInTime += linkInTime;
    runStats.origNumFreeVars = solver->getNumFreeVars();
    const size_t origBlockedSize = blockedClauses.size();
    const size_t origTrailSize = solver->trail.size();

    //subsumeStrengthen->subsumeWithTris();
    subsumeStrengthen->performSubsumption();
    if (!subsumeStrengthen->performStrengthening())
        goto end;

    #ifdef USE_M4RI
    if (solver->conf.doFindXors
        && xorFinder != NULL
        && !xorFinder->findXors()
    ) {
        goto end;
    }
    #endif

    /*if (solver->conf.doAsymmTE) {
        asymmTE();
    }*/

    if (!propagate()) {
        goto end;
    }

    solver->clauseCleaner->clean_implicit_clauses();
    if (solver->conf.doVarElim) {
        eliminate_empty_resolvent_vars();
        if (!eliminateVars())
            goto end;
    }

    if (!propagate()) {
        goto end;
    }

    if (!bounded_var_addition()) {
        goto end;
    }

    if (solver->conf.doCache && solver->conf.doGateFind) {
        if (!gateFinder->doAll())
            goto end;
    }

end:

    remove_by_drup_recently_blocked_clauses(origBlockedSize);
    finishUp(origTrailSize);

    //Print stats
    if (solver->conf.verbosity >= 1) {
        if (solver->conf.verbosity >= 3)
            runStats.print(solver->nVars());
        else
            runStats.printShort(solver->conf.doVarElim);
    }

    return solver->ok;
}

bool Simplifier::fill_occur()
{
    //Try to add irreducible to occur
    runStats.origNumIrredLongClauses = solver->longIrredCls.size();
    bool ret = addFromSolver(solver->longIrredCls
        , true //try to add to occur list
        , true //it is irred
    );

    //Memory limit reached, irreduntant clauses cannot
    //be added to occur --> exit
    if (!ret) {
        CompleteDetachReatacher detRet(solver);
        detRet.reattachLongs(true);
        return false;
    }

    //Add redundant to occur
    runStats.origNumRedLongClauses = solver->longRedCls.size();
    addFromSolver(solver->longRedCls
        , true //try to add to occur list
        , false //irreduntant?
    );

    return true;
}

bool Simplifier::unEliminate(Var var)
{
    assert(solver->decisionLevel() == 0);
    assert(solver->okay());

    //Check that it was really eliminated
    assert(solver->varData[var].removed == Removed::elimed);
    assert(!solver->varData[var].is_decision);
    assert(solver->value(var) == l_Undef);

    if (!blockedMapBuilt) {
        cleanBlockedClauses();
        buildBlockedMap();
    }

    //Uneliminate it in theory
    globalStats.numVarsElimed--;
    solver->varData[var].removed = Removed::none;
    solver->setDecisionVar(var);
    if (solver->conf.doStamp) {
        solver->stamp.remove_from_stamps(var);
    }

    //Find if variable is really needed to be eliminated
    var = solver->map_inter_to_outer(var);
    map<Var, vector<size_t> >::iterator it = blk_var_to_cl.find(var);
    if (it == blk_var_to_cl.end())
        return solver->okay();

    //Eliminate it in practice
    //NOTE: Need to eliminate in theory first to avoid infinite loops
    for(size_t i = 0; i < it->second.size(); i++) {
        size_t at = it->second[i];

        //Mark for removal from blocked list
        blockedClauses[at].toRemove = true;
        assert(blockedClauses[at].blockedOn.var() == var);

        if (blockedClauses[at].dummy)
            continue;

        //Re-insert into Solver
        #ifdef VERBOSE_DEBUG_RECONSTRUCT
        cout
        << "Uneliminating cl " << blockedClauses[at].lits
        << " on var " << var+1
        << endl;
        #endif
        solver->addClause(blockedClauses[at].lits);
        if (!solver->okay())
            return false;
    }

    return solver->okay();
}

void Simplifier::remove_by_drup_recently_blocked_clauses(size_t origBlockedSize)
{
    if (!(*solver->drup).enabled())
        return;

    if (solver->conf.verbosity >= 6) {
        cout << "c Deleting blocked clauses for DRUP" << endl;
    }

    for(size_t i = origBlockedSize; i < blockedClauses.size(); i++) {
        if (blockedClauses[i].dummy)
            continue;

        //If doing stamping or caching, we cannot delete binary redundant
        //clauses, because they are stored in the stamp/cache and so
        //will be used -- and DRUP will complain when used
        if (blockedClauses[i].lits.size() <= 2
            && (solver->conf.doCache
                || solver->conf.doStamp)
        ) {
            continue;
        }

        (*solver->drup) << del;
        for(vector<Lit>::const_iterator
            it = blockedClauses[i].lits.begin(), end = blockedClauses[i].lits.end()
            ; it != end
            ; it++
        ) {
            (*solver->drup) << *it;
        }
        (*solver->drup) << fin;
    }
}

void Simplifier::buildBlockedMap()
{
    blk_var_to_cl.clear();
    for(size_t i = 0; i < blockedClauses.size(); i++) {
        const BlockedClause& blocked = blockedClauses[i];
        map<Var, vector<size_t> >::iterator it
            = blk_var_to_cl.find(blocked.blockedOn.var());

        if (it == blk_var_to_cl.end()) {
            vector<size_t> tmp;
            tmp.push_back(i);
            blk_var_to_cl[blocked.blockedOn.var()] = tmp;
        } else {
            it->second.push_back(i);
        }
    }
    blockedMapBuilt = true;
}

void Simplifier::finishUp(
    size_t origTrailSize
) {
    bool somethingSet = (solver->trail.size() - origTrailSize) > 0;

    runStats.zeroDepthAssings = solver->trail.size() - origTrailSize;
    double myTime = cpuTime();

    //Add back clauses to solver
    propagate();
    removeAllLongsFromWatches();
    addBackToSolver();
    propagate();
    if (solver->ok) {
        solver->clauseCleaner->removeAndCleanAll();
    }

    //Sanity checks
    if (solver->ok && somethingSet) {
        solver->testAllClauseAttach();
        solver->checkNoWrongAttach();
        solver->checkStats();
        solver->checkImplicitPropagated();
    }

    //Update global stats
    runStats.finalCleanupTime += cpuTime() - myTime;
    globalStats += runStats;
    subsumeStrengthen->finishedRun();

    if (solver->ok) {
        checkElimedUnassignedAndStats();
    }
}

void Simplifier::sanityCheckElimedVars()
{
    //First, sanity-check the long clauses
    for (vector<ClOffset>::const_iterator
        it =  clauses.begin(), end = clauses.end()
        ; it != end
        ; it++
    ) {
        const Clause* cl = solver->clAllocator.getPointer(*it);

        //Already removed
        if (cl->getFreed())
            continue;

        for (const Lit lit: *cl) {
            if (solver->varData[lit.var()].removed == Removed::elimed) {
                cout
                << "Error: elimed var -- Lit " << lit << " in clause"
                << endl
                << "wrongly left in clause: " << *cl
                << endl;
                exit(-1);
            }
        }
    }

    //Then, sanity-check the binary clauses
    size_t wsLit = 0;
    for (watch_array::const_iterator
        it = solver->watches.begin(), end = solver->watches.end()
        ; it != end
        ; ++it, wsLit++
    ) {
        Lit lit = Lit::toLit(wsLit);
        watch_subarray_const ws = *it;
        for (watch_subarray_const::const_iterator
            it2 = ws.begin(), end2 = ws.end()
            ; it2 != end2
            ; it2++
        ) {
            if (it2->isBinary()) {
                if (solver->varData[lit.var()].removed == Removed::elimed
                        || solver->varData[it2->lit2().var()].removed == Removed::elimed
                ) {
                    cout
                    << "Error: A var is elimed in a binary clause: "
                    << lit << " , " << it2->lit2()
                    << endl;
                    exit(-1);
                }
            }
        }
    }
}

/*const bool Simplifier::mixXorAndGates()
{
    assert(solver->ok);
    uint32_t fixed = 0;
    uint32_t ored = 0;
    double myTime = cpuTime();
    uint32_t oldTrailSize = solver->trail.size();
    vector<Lit> lits;
    vector<Lit> tmp;

    uint32_t index = 0;
    for (vector<Xor>::iterator it = xors.begin(), end = xors.end(); it != end; it++, index++) {
        const Xor& thisXor = *it;
        if (thisXor.vars.size() != 3) continue;

        for (uint32_t i = 0; i < thisXor.vars.size(); i++) {
            seen[thisXor.vars[i]] = true;
        }

//         for (uint32_t i = 0; i < thisXor.vars.size(); i++) {
//             Var var = thisXor.vars[i];
//             const vector<uint32_t>& occ1 = gateOccEq[Lit(var, true).toInt()];
//             for (vector<uint32_t>::const_iterator it = occ1.begin(), end = occ1.end(); it != end; it++) {
//                 const OrGate& orGate = orGates[*it];
//                 uint32_t OK = 0;
//                 for (uint32_t i2 = 0; i2 < orGate.lits.size(); i2++) {
//                     if (orGate.lits[i2].sign() &&
//                         seen[orGate.lits[i2].var()]) OK++;
//                 }
//                 if (OK>1) {
//                     cout << "XOR to look at:" << thisXor << endl;
//                     cout << "gate to look at : " << orGate << endl;
//                     cout << "---------------" << endl;
//                 }
//             }
//         }

        for (uint32_t i = 0; i < thisXor.vars.size(); i++) {
            Var var = thisXor.vars[i];
            Lit eqLit = Lit(var, true);
            const vector<uint32_t>& occ = gateOccEq[eqLit.toInt()];
            for (vector<uint32_t>::const_iterator it = occ.begin(), end = occ.end(); it != end; it++) {
                const OrGate& orGate = orGates[*it];
                assert(orGate.eqLit == eqLit);
                uint32_t OK = 0;
                lits.clear();
                bool sign = false;
                for (uint32_t i2 = 0; i2 < orGate.lits.size(); i2++) {
                    if (seen[orGate.lits[i2].var()]) {
                        OK++;
                        lits.push_back(orGate.lits[i2]  ^ true);
                        sign ^= !orGate.lits[i2].sign();
                    }
                }
                if (OK == 2) {
                    #ifdef VERBOSE_XORGATE_MIX
                    cout << "XOR to look at:" << thisXor << endl;
                    cout << "gate to look at : " << orGate << endl;
                    #endif

                    if (!thisXor.rhs^sign) {
                        fixed++;
                        tmp.clear();
                        tmp.push_back(~lits[0]);
                        #ifdef VERBOSE_XORGATE_MIX
                        cout << "setting: " << tmp[0] << endl;
                        #endif
                        solver->addClauseInt(tmp);
                        if (!solver->ok) goto end;

                        tmp.clear();
                        tmp.push_back(~lits[1]);
                        #ifdef VERBOSE_XORGATE_MIX
                        cout << "setting: " << tmp[0] << endl;
                        #endif
                        solver->addClauseInt(tmp);
                        if (!solver->ok) goto end;
                    } else {
                        ored++;
                        tmp.clear();
                        tmp.push_back(lits[0]);
                        tmp.push_back(lits[1]);
                        #ifdef VERBOSE_XORGATE_MIX
                        cout << "orIng: " << tmp << endl;
                        #endif
                        Clause* c = solver->addClauseInt(tmp, true);
                        assert(c == NULL);
                        if (!solver->ok) goto end;
                    }

                    #ifdef VERBOSE_XORGATE_MIX
                    cout << "---------------" << endl;
                    #endif
                }
            }
        }

        end:
        for (uint32_t i = 0; i < thisXor.vars.size(); i++) {
            seen[thisXor.vars[i]] = false;
        }
        if (!solver->ok) break;
    }

    if (solver->conf.verbosity >= 1) {
        cout << "c OrXorMix"
        << " Or: " << std::setw(6) << ored
        << " Fix: " << std::setw(6) << fixed
        << " Fixed: " << std::setw(4) << (solver->trail.size() - oldTrailSize)
        << " T: " << std::setprecision(2) << std::setw(5) << (cpuTime() - myTime) << " s"
        << endl;
    }

    return solver->ok;
}*/

void Simplifier::asymmTE()
{
    assert(false && "asymmTE has a bug (unknown), cannot be used");
    //Random system would die here
    if (clauses.empty())
        return;

    blockedMapBuilt = false;

    const double myTime = cpuTime();
    uint32_t asymmSubsumed = 0;
    uint32_t removed = 0;
    size_t wenThrough = 0;

    vector<Lit> tmpCl;
    limit_to_decrease = &asymm_time_limit;
    while(*limit_to_decrease > 0
        && wenThrough < 2*clauses.size()
    ) {
        *limit_to_decrease -= 2;
        wenThrough++;

        //Print status
        if (solver->conf.verbosity >= 5
            && wenThrough % 10000 == 0
        ) {
            cout << "toDecrease: " << *limit_to_decrease << endl;
        }

        size_t num = solver->mtrand.randInt(clauses.size()-1);
        ClOffset offset = clauses[num];
        Clause& cl = *solver->clAllocator.getPointer(offset);

        //Already removed or redundant
        if (cl.getFreed() || cl.red())
            continue;


        *limit_to_decrease -= cl.size()*2;

        //Fill tmpCl, seen
        tmpCl.clear();
        for (const Lit *l = cl.begin(), *end = cl.end(); l != end; l++) {
            seen[l->toInt()] = true;
            tmpCl.push_back(*l);
        }

        //add to tmpCl literals that could be added through reverse strengthening
        //ONLY irred
        //TODO stamping
        /*for (const Lit *l = cl.begin(), *end = cl.end(); l != end; l++) {
            const vector<LitExtra>& cache = solver->implCache[l->toInt()].lits;
            *toDecrease -= cache.size();
            for (vector<LitExtra>::const_iterator cacheLit = cache.begin(), endCache = cache.end(); cacheLit != endCache; cacheLit++) {
                if (cacheLit->getOnlyIrredBin()
                    && !seen[(~cacheLit->getLit()).toInt()]
                ) {
                    const Lit toAdd = ~(cacheLit->getLit());
                    tmpCl.push_back(toAdd);
                    seen[toAdd.toInt()] = true;
                }
            }
        }*/


        //subsumption with binary clauses
        bool toRemove = false;
        if (solver->conf.doExtBinSubs) {
            //for (vector<Lit>::const_iterator l = tmpCl.begin(), end = tmpCl.end(); l != end; l++) {
//            for (const Lit* l = cl.begin(), *end = cl.end(); l != end; l++) {

                //TODO stamping
                /*const vector<LitExtra>& cache = solver->implCache[l->toInt()].lits;
                *toDecrease -= cache.size();
                for (vector<LitExtra>::const_iterator cacheLit = cache.begin(), endCache = cache.end(); cacheLit != endCache; cacheLit++) {
                    if ((cacheLit->getOnlyIrredBin() || cl.red()) //subsume irred with irred
                        && seen[cacheLit->getLit().toInt()]
                    ) {
                        toRemove = true;
                        asymmSubsumed++;
                        #ifdef VERBOSE_DEBUG_ASYMTE
                        cout << "c AsymLitAdd removing: " << cl << endl;
                        #endif
                        goto next;
                    }
                }*/
//            }
        }

        if (cl.red())
            goto next;

        /*
        //subsumption with irred larger clauses
        CL_ABST_TYPE abst;
        abst = calcAbstraction(tmpCl);
        *toDecrease -= tmpCl.size()*2;
        for (vector<Lit>::const_iterator it = tmpCl.begin(), end = tmpCl.end(); it != end; it++) {
            const Occur& occ = occur[it->toInt()];
            *toDecrease -= occ.size();
            for (Occur::const_iterator it2 = occ.begin(), end2 = occ.end(); it2 != end2; it2++) {
                if (it2->index != index
                    && subsetAbst(clauseData[it2->index].abst, abst)
                    && clauses[it2->index] != NULL
                    && !clauses[it2->index]->red()
                    && subsetReverse(*clauses[it2->index])
                )  {
                    #ifdef VERBOSE_DEBUG_ASYMTE
                    cout << "c AsymTE removing: " << cl << " -- subsumed by cl: " << *clauses[it2->index] << endl;
                    #endif
                    toRemove = true;
                    goto next;
                }
            }
        }*/

        next:
        if (toRemove) {
            unlinkClause(offset);
            removed++;
        }

        //Clear seen
        for (vector<Lit>::const_iterator l = tmpCl.begin(), end = tmpCl.end(); l != end; l++) {
            seen[l->toInt()] = false;
        }
    }

    if (solver->conf.verbosity >= 1) {
        cout << "c AsymmTElim"
        << " asymm subsumed: " << asymmSubsumed
        << " T : " << std::fixed << std::setprecision(2) << std::setw(6) << (cpuTime() - myTime)
        << endl;
    }
    runStats.asymmSubs += asymmSubsumed;
    runStats.asymmTime += cpuTime() - myTime;
}

void Simplifier::setLimits()
{
    subsumption_time_limit     = 850LL*1000LL*1000LL;
    strengthening_time_limit   = 400LL*1000LL*1000LL;
//     numMaxTriSub      = 600LL*1000LL*1000LL;
    norm_varelim_time_limit    = 800LL*1000LL*1000LL;
    empty_varelim_time_limit   = 200LL*1000LL*1000LL;
    asymm_time_limit           = 40LL *1000LL*1000LL;
    aggressive_elim_time_limit = 300LL *1000LL*1000LL;
    bounded_var_elim_time_limit= 400LL *1000LL*1000LL;

    //numMaxElim = 0;
    //numMaxElim = std::numeric_limits<int64_t>::max();

    //If variable elimination isn't going so well
    if (globalStats.testedToElimVars > 0
        && (double)globalStats.numVarsElimed/(double)globalStats.testedToElimVars < 0.1
    ) {
        norm_varelim_time_limit /= 2;
    }

    #ifdef BIT_MORE_VERBOSITY
    cout << "c addedClauseLits: " << clause_lits_added_limit << endl;
    #endif
    if (clause_lits_added < 10ULL*1000ULL*1000ULL) {
        norm_varelim_time_limit *= 2;
        empty_varelim_time_limit *= 2;
        subsumption_time_limit *= 2;
        strengthening_time_limit *= 2;
        bounded_var_elim_time_limit *= 2;
    }

    if (clause_lits_added < 3ULL*1000ULL*1000ULL) {
        norm_varelim_time_limit *= 2;
        empty_varelim_time_limit *= 2;
        subsumption_time_limit *= 2;
        strengthening_time_limit *= 2;
    }

    varelim_num_limit = ((double)solver->getNumFreeVars() * solver->conf.varElimRatioPerIter);
    if (globalStats.numCalls > 0) {
        varelim_num_limit = (double)varelim_num_limit * (globalStats.numCalls+0.5);
    }
    runStats.origNumMaxElimVars = varelim_num_limit;

    if (!solver->conf.doSubsume1) {
        strengthening_time_limit = 0;
    }

    //For debugging

    //numMaxSubsume0 = 0;
    //numMaxSubsume1 = 0;
    //numMaxElimVars = 0;
    //numMaxElim = 0;
    //numMaxSubsume0 = std::numeric_limits<int64_t>::max();
    //numMaxSubsume1 = std::numeric_limits<int64_t>::max();
    //numMaxElimVars = std::numeric_limits<int32_t>::max();
    //numMaxElim     = std::numeric_limits<int64_t>::max();
}

void Simplifier::cleanBlockedClauses()
{
    assert(solver->decisionLevel() == 0);
    vector<BlockedClause>::iterator i = blockedClauses.begin();
    vector<BlockedClause>::iterator j = blockedClauses.begin();
    size_t at = 0;

    for (vector<BlockedClause>::iterator
        end = blockedClauses.end()
        ; i != end
        ; i++, at++
    ) {
        const Var blockedOn = solver->map_outer_to_inter(i->blockedOn.var());
        if (solver->varData[blockedOn].removed == Removed::elimed
            && solver->value(blockedOn) != l_Undef
        ) {
            cout
            << "ERROR: lit " << *i << " elimed,"
            << " value: " << solver->value(blockedOn)
            << endl;
            assert(false);
            exit(-1);
        }

        if (blockedClauses[at].toRemove) {
            blockedMapBuilt = false;
        } else {
            assert(solver->varData[blockedOn].removed == Removed::elimed);
            *j++ = *i;
        }
    }
    blockedClauses.resize(blockedClauses.size()-(i-j));
}

size_t Simplifier::rem_cls_from_watch_due_to_varelim(
    watch_subarray_const todo
    , const Lit lit
) {
    blockedMapBuilt = false;
    vector<Lit> lits;
    const size_t orig_blocked_cls_size = blockedClauses.size();

    //Copy todo --> it will be manipulated below
    vector<Watched> todo_copy;
    for(Watched tmp: todo) {
        todo_copy.push_back(tmp);
    }

    for (const Watched watch :todo_copy) {
        lits.clear();
        bool red = false;

        if (watch.isClause()) {
            ClOffset offset = watch.getOffset();
            Clause& cl = *solver->clAllocator.getPointer(offset);

            //Update stats
            if (!cl.red()) {
                runStats.clauses_elimed_long++;
                runStats.clauses_elimed_sumsize += cl.size();

                lits.resize(cl.size());
                std::copy(cl.begin(), cl.end(), lits.begin());
                add_clause_to_blck(lit, lits);
            } else {
                red = true;
                runStats.longRedClRemThroughElim++;
            }

            //Remove -- only DRUP the ones that are redundant
            //The irred will be removed thanks to 'blocked' system
            unlinkClause(offset, cl.red());
        }

        if (watch.isBinary()) {

            //Update stats
            if (!watch.red()) {
                runStats.clauses_elimed_bin++;
                runStats.clauses_elimed_sumsize += 2;
            } else {
                red = true;
                runStats.binRedClRemThroughElim++;
            }

            //Put clause into blocked status
            lits.resize(2);
            lits[0] = lit;
            lits[1] = watch.lit2();
            if (!watch.red()) {
                add_clause_to_blck(lit, lits);
                touched.touch(watch.lit2());
            } else {
                //If redundant, delayed blocked-based DRUP deletion will not work
                //so delete explicitly

                //Drup
                if (!solver->conf.doStamp
                    && !solver->conf.doCache
                ) {
                   (*solver->drup) << del << lits[0] << lits[1] << fin;
                }
            }

            //Remove
            *limit_to_decrease -= solver->watches[lits[0].toInt()].size();
            *limit_to_decrease -= solver->watches[lits[1].toInt()].size();
            solver->detachBinClause(lits[0], lits[1], watch.red());
        }

        if (watch.isTri()) {

            //Update stats
            if (!watch.red()) {
                runStats.clauses_elimed_tri++;
                runStats.clauses_elimed_sumsize += 3;
            } else {
                red = true;
                runStats.triRedClRemThroughElim++;
            }

            //Put clause into blocked status
            lits.resize(3);
            lits[0] = lit;
            lits[1] = watch.lit2();
            lits[2] = watch.lit3();
            if (!watch.red()) {
                add_clause_to_blck(lit, lits);
                touched.touch(watch.lit2());
                touched.touch(watch.lit3());
            } else {
                //If redundant, delayed blocked-based DRUP deletion will not work
                //so delete explicitly
                (*solver->drup) << del << lits[0] << lits[1] << lits[2] << fin;
            }

            //Remove
            *limit_to_decrease -= solver->watches[lits[0].toInt()].size();
            *limit_to_decrease -= solver->watches[lits[1].toInt()].size();
            *limit_to_decrease -= solver->watches[lits[2].toInt()].size();
            solver->detachTriClause(lits[0], lits[1], lits[2], watch.red());
        }

        if (solver->conf.verbosity >= 3 && !lits.empty()) {
            cout
            << "Eliminated clause " << lits << " (red: " << red << ")"
            << " on var " << lit.var()+1
            << endl;
        }
    }

    return blockedClauses.size() - orig_blocked_cls_size;
}

void Simplifier::add_clause_to_blck(Lit lit, const vector<Lit>& lits)
{
    lit = solver->map_inter_to_outer(lit);
    vector<Lit> lits_outer = lits;
    solver->map_inter_to_outer(lits_outer);
    blockedClauses.push_back(BlockedClause(lit, lits_outer));
}

uint32_t Simplifier::numIrredBins(const Lit lit) const
{
    uint32_t num = 0;
    watch_subarray_const ws = solver->watches[lit.toInt()];
    for (watch_subarray::const_iterator it = ws.begin(), end = ws.end(); it != end; it++) {
        if (it->isBinary() && !it->red()) num++;
    }

    return num;
}

int Simplifier::test_elim_and_fill_resolvents(const Var var)
{
    assert(solver->ok);
    assert(solver->varData[var].removed == Removed::none);
    assert(solver->value(var) == l_Undef);

    //Gather data
    HeuristicData pos = calcDataForHeuristic(Lit(var, false));
    HeuristicData neg = calcDataForHeuristic(Lit(var, true));

    //Heuristic calculation took too much time
    if (*limit_to_decrease < 0) {
        return 1000;
    }

    //Check if we should do agressive check or not
    bool agressive = (aggressive_elim_time_limit > 0);
    runStats.usedAgressiveCheckToELim += agressive;

    //set-up
    const Lit lit = Lit(var, false);
    watch_subarray poss = solver->watches[lit.toInt()];
    watch_subarray negs = solver->watches[(~lit).toInt()];
    std::sort(poss.begin(), poss.end(), WatchSorter());
    std::sort(negs.begin(), negs.end(), WatchSorter());
    resolvents.clear();

    //Pure literal, no resolvents
    //we look at "pos" and "neg" (and not poss&negs) because we don't care about redundant clauses
    if (pos.totalCls() == 0 || neg.totalCls() == 0) {
        return -100;
    }

    //Too expensive to check, it's futile
    if (pos.totalCls() >= 40 && neg.totalCls() >= 40) {
        return 1000;
    }

    // Count clauses/literals after elimination
    uint32_t before_clauses = pos.bin + pos.tri + pos.longer + neg.bin + neg.tri + neg.longer;
    uint32_t after_clauses = 0;
    uint32_t after_long = 0;
    uint32_t after_bin = 0;
    uint32_t after_tri = 0;
    uint32_t after_literals = 0;
    for (watch_subarray::const_iterator
        it = poss.begin(), end = poss.end()
        ; it != end
        ; it++
    ) {
        *limit_to_decrease -= 3;
        if (solver->redundant(*it))
            continue;

        for (watch_subarray::const_iterator
            it2 = negs.begin(), end2 = negs.end()
            ; it2 != end2
            ; it2++
        ) {
            *limit_to_decrease -= 3;
            if (solver->redundant(*it2))
                continue;

            //Resolve the two clauses
            bool tautological = resolve_clauses(*it, *it2, lit, agressive);
            if (tautological)
                continue;

            #ifdef VERBOSE_DEBUG_VARELIM
            cout << "Adding new clause due to varelim: " << dummy << endl;
            #endif

            //Update after-stats
            after_clauses++;
            after_literals += dummy.size();
            if (dummy.size() > 3)
                after_long++;
            if (dummy.size() == 3)
                after_tri++;
            if (dummy.size() == 2)
                after_bin++;

            //Early-abort or over time
            if (after_clauses > before_clauses
                //Over-time
                || *limit_to_decrease < -10LL*1000LL
            ) {
                return 1000;
            }

            //Calculate new clause stats
            ClauseStats stats;
            if ((it->isBinary() || it->isTri()) && it2->isClause())
                stats = solver->clAllocator.getPointer(it2->getOffset())->stats;
            else if ((it2->isBinary() || it2->isTri()) && it->isClause())
                stats = solver->clAllocator.getPointer(it->getOffset())->stats;
            else if (it->isClause() && it2->isClause())
                stats = ClauseStats::combineStats(
                    solver->clAllocator.getPointer(it->getOffset())->stats
                    , solver->clAllocator.getPointer(it2->getOffset())->stats
            );

            resolvents.push_back(std::make_pair(dummy, stats));
        }
    }

    //Smaller value returned, the better
    int cost = (int)after_long + (int)after_tri + (int)after_bin*(int)3
        - (int)pos.longer - (int)neg.longer
        - (int)pos.tri - (int)neg.tri
        - (int)pos.bin*3 - (int)neg.bin*(int)3;

    return cost;
}

void Simplifier::printOccur(const Lit lit) const
{
    for(size_t i = 0; i < solver->watches[lit.toInt()].size(); i++) {
        const Watched& w = solver->watches[lit.toInt()][i];
        if (w.isBinary()) {
            cout
            << "Bin   --> "
            << lit << ", "
            << w.lit2()
            << "(red: " << w.red()
            << ")"
            << endl;
        }

        if (w.isTri()) {
            cout
            << "Tri   --> "
            << lit << ", "
            << w.lit2() << " , " << w.lit3()
            << "(red: " << w.red()
            << ")"
            << endl;
        }

        if (w.isClause()) {
            cout
            << "Clause--> "
            << *solver->clAllocator.getPointer(w.getOffset())
            << "(red: " << solver->clAllocator.getPointer(w.getOffset())->red()
            << ")"
            << endl;
        }
    }
}

void Simplifier::print_var_eliminate_stat(const Lit lit) const
{
    //Eliminate:
    if (solver->conf.verbosity < 5)
        return;

    cout
    << "Eliminating var " << lit
    << " with occur sizes "
    << solver->watches[lit.toInt()].size() << " , "
    << solver->watches[(~lit).toInt()].size()
    << endl;

    cout << "POS: " << endl;
    printOccur(lit);
    cout << "NEG: " << endl;
    printOccur(~lit);
}

void Simplifier::check_if_new_2_long_subsumes_3_long(const vector<Lit>& lits)
{
    assert(lits.size() == 2);
    for(watch_subarray::const_iterator
        it2 = solver->watches[lits[0].toInt()].begin()
        , end2 = solver->watches[lits[0].toInt()].end()
        ; it2 != end2
        ; it2++
    ) {
        if (it2->isTri() && !it2->red()
            && (it2->lit2() == lits[1]
                || it2->lit3() == lits[1])
        ) {
            if (solver->conf.verbosity >= 6) {
                cout
                << "Removing irred tri-clause due to addition of"
                << " irred bin: "
                << lits[0]
                << ", " << it2->lit2()
                << ", " << it2->lit3()
                << endl;
            }

            touched.touch(it2->lit2());
            touched.touch(it2->lit3());

            runStats.subsumedByVE++;
            solver->detachTriClause(
                lits[0]
                , it2->lit2()
                , it2->lit3()
                , it2->red()
            );

            //We have to break: we just modified the stuff we are
            //going through...
            break;
        }
    }
}

bool Simplifier::add_varelim_resolvent(
    vector<Lit>& finalLits
    , const ClauseStats& stats
) {
    runStats.newClauses++;

    //Check if a new 2-long would subsume a 3-long
    if (finalLits.size() == 2) {
        check_if_new_2_long_subsumes_3_long(finalLits);
    }

    //Add clause and do subsumption
    Clause* newCl = solver->addClauseInt(
        finalLits //Literals in new clause
        , false //Is the new clause redundant?
        , stats //Statistics for this new clause (usage, etc.)
        , false //Should clause be attached?
        , &finalLits //Return final set of literals here
    );

    if (!solver->ok)
        return false;

    if (newCl != NULL) {
        linkInClause(*newCl);
        ClOffset offset = solver->clAllocator.getOffset(newCl);
        clauses.push_back(offset);
        runStats.subsumedByVE += subsumeStrengthen->subsume0(offset);
    } else if (finalLits.size() == 3 || finalLits.size() == 2) {
        //Subsume long
        SubsumeStrengthen::Sub0Ret ret = subsumeStrengthen->subsume0AndUnlink(
            std::numeric_limits<uint32_t>::max() //Index of this implicit clause (non-existent)
            , finalLits //Literals in this binary clause
            , calcAbstraction(finalLits) //Abstraction of literals
            , true //subsume implicit ones
        );
        runStats.subsumedByVE += ret.numSubsumed;
        if (ret.numSubsumed > 0) {
            if (solver->conf.verbosity >= 5) {
                cout << "Subsumed: " << ret.numSubsumed << endl;
            }
        }
    }

    //Touch every var of the new clause, so we re-estimate
    //elimination complexity for this var
    for(Lit lit: finalLits)
        touched.touch(lit);

    return true;
}

void Simplifier::update_varelim_complexity_heap(const Var var)
{
    //Update var elim complexity heap
    if (!solver->conf.updateVarElimComplexityOTF)
        return;

    for(Var touchVar: touched.getTouchedList()) {
        //No point in updating the score of this var
        //it's eliminated already, or not to be eliminated at all
        if (touchVar == var
            || !varElimOrder.inHeap(touchVar)
            || solver->value(touchVar) != l_Undef
            || solver->varData[touchVar].removed != Removed::none
        ) {
            continue;
        }

        varElimComplexity[touchVar] = strategyCalcVarElimScore(touchVar);
        varElimOrder.update(touchVar);
    }
}

void Simplifier::print_var_elim_complexity_stats(const Var var) const
{
    if (solver->conf.verbosity < 5)
        return;

    cout << "trying complexity: "
    << varElimComplexity[var].first
    << ", " << varElimComplexity[var].second
    << endl;
}

void Simplifier::set_var_as_eliminated(const Var var, const Lit lit)
{
    if (solver->conf.verbosity >= 5) {
        cout << "Elimination of var "
        <<  solver->map_inter_to_outer(lit)
        << " finished " << endl;
    }
    solver->varData[var].removed = Removed::elimed;
    runStats.numVarsElimed++;
    solver->unsetDecisionVar(var);
}

void Simplifier::create_dummy_blocked_clause(const Lit lit)
{
    blockedClauses.push_back(
        BlockedClause(solver->map_inter_to_outer(lit))
    );
}

bool Simplifier::maybeEliminate(const Var var)
{
    assert(solver->ok);
    print_var_elim_complexity_stats(var);
    runStats.testedToElimVars++;

    if (test_elim_and_fill_resolvents(var) == 1000) {
        return false;
    }
    runStats.triedToElimVars++;

    const Lit lit = Lit(var, false);
    print_var_eliminate_stat(lit);

    //Remove clauses
    touched.clear();
    create_dummy_blocked_clause(lit);
    rem_cls_from_watch_due_to_varelim(solver->watches[lit.toInt()], lit);
    rem_cls_from_watch_due_to_varelim(solver->watches[(~lit).toInt()], ~lit);

    //Add resolvents
    for(auto& resolvent: resolvents) {
        bool ok = add_varelim_resolvent(resolvent.first, resolvent.second);
        if (!ok)
            goto end;
    }
    update_varelim_complexity_heap(var);

end:
    set_var_as_eliminated(var, lit);

    return solver->ok;
}

/*void Simplifier::addRedBinaries(const Var var)
{
    vector<Lit> tmp(2);
    Lit lit = Lit(var, false);
    watch_subarray_const ws = solver->watches[(~lit).toInt()];
    watch_subarray_const ws2 = solver->watches[lit.toInt()];

    for (watch_subarray::const_iterator w1 = ws.begin(), end1 = ws.end(); w1 != end1; w1++) {
        if (!w1->isBinary()) continue;
        const bool numOneIsRed = w1->red();
        const Lit lit1 = w1->lit2();
        if (solver->value(lit1) != l_Undef || var_elimed[lit1.var()]) continue;

        for (watch_subarray::const_iterator w2 = ws2.begin(), end2 = ws2.end(); w2 != end2; w2++) {
            if (!w2->isBinary()) continue;
            const bool numTwoIsRed = w2->red();
            if (!numOneIsRed && !numTwoIsRed) {
                //At least one must be redundant
                continue;
            }

            const Lit lit2 = w2->lit2();
            if (solver->value(lit2) != l_Undef || var_elimed[lit2.var()]) continue;

            tmp[0] = lit1;
            tmp[1] = lit2;
            Clause* tmpOK = solver->addClauseInt(tmp, true);
            runStats.numRedBinVarRemAdded++;
            release_assert(tmpOK == NULL);
            release_assert(solver->ok);
        }
    }
    assert(solver->value(lit) == l_Undef);
}*/

void Simplifier::add_pos_lits_to_dummy_and_seen(
    const Watched ps
    , const Lit posLit
) {
    if (ps.isBinary() || ps.isTri()) {
        *limit_to_decrease -= 1;
        assert(ps.lit2() != posLit);

        seen[ps.lit2().toInt()] = 1;
        dummy.push_back(ps.lit2());
    }

    if (ps.isTri()) {
        assert(ps.lit2() < ps.lit3());

        seen[ps.lit3().toInt()] = 1;
        dummy.push_back(ps.lit3());
    }

    if (ps.isClause()) {
        Clause& cl = *solver->clAllocator.getPointer(ps.getOffset());
        //assert(!clauseData[ps.clsimp.index].defOfOrGate);
        *limit_to_decrease -= cl.size();
        for (uint32_t i = 0; i < cl.size(); i++){
            //Skip noPosLit
            if (cl[i] == posLit)
                continue;

            seen[cl[i].toInt()] = 1;
            dummy.push_back(cl[i]);
        }
    }
}

bool Simplifier::add_neg_lits_to_dummy_and_seen(
    const Watched qs
    , const Lit posLit
) {
    if (qs.isBinary() || qs.isTri()) {
        *limit_to_decrease -= 2;
        assert(qs.lit2() != ~posLit);

        if (seen[(~qs.lit2()).toInt()]) {
            return true;
        }
        if (!seen[qs.lit2().toInt()]) {
            dummy.push_back(qs.lit2());
            seen[qs.lit2().toInt()] = 1;
        }
    }

    if (qs.isTri()) {
        assert(qs.lit2() < qs.lit3());

        if (seen[(~qs.lit3()).toInt()]) {
            return true;
        }
        if (!seen[qs.lit3().toInt()]) {
            dummy.push_back(qs.lit3());
            seen[qs.lit3().toInt()] = 1;
        }
    }

    if (qs.isClause()) {
        Clause& cl = *solver->clAllocator.getPointer(qs.getOffset());
        *limit_to_decrease -= cl.size();
        for (const Lit lit: cl) {
            if (lit == ~posLit)
                continue;

            if (seen[(~lit).toInt()]) {
                return true;
            }

            //Add the literal
            if (!seen[lit.toInt()]) {
                dummy.push_back(lit);
                seen[lit.toInt()] = 1;
            }
        }
    }

    return false;
}

bool Simplifier::reverse_vivification_of_dummy(
    const Watched ps
    , const Watched qs
    , const Lit posLit
) {
    /*
    //TODO
    //Use watchlists
    if (numMaxVarElimAgressiveCheck > 0) {
        if (agressiveCheck(lit, noPosLit, retval))
            goto end;
    }*/

    //Cache can only be used if none are binary
    if (ps.isBinary()
        || qs.isBinary()
        || !solver->conf.doCache
    ) {
        return false;
    }

    for (size_t i = 0
        ; i < toClear.size() && aggressive_elim_time_limit > 0
        ; i++
    ) {
        aggressive_elim_time_limit -= 3;
        const Lit lit = toClear[i];
        assert(lit.var() != posLit.var());

        //Use cache
        const vector<LitExtra>& cache = solver->implCache[lit.toInt()].lits;
        aggressive_elim_time_limit -= cache.size()/3;
        for(const LitExtra litextra: cache) {
            //If redundant, that doesn't help
            if (!litextra.getOnlyIrredBin())
                continue;

            const Lit otherLit = litextra.getLit();
            if (otherLit.var() == posLit.var())
                continue;

            //If (a) was in original clause
            //then (a V b) means -b can be put inside
            if (!seen[(~otherLit).toInt()]) {
                toClear.push_back(~otherLit);
                seen[(~otherLit).toInt()] = 1;
            }

            //If (a V b) is irred in the clause, then done
            if (seen[otherLit.toInt()]) {
                return true;
            }
        }
    }

    return false;
}

bool Simplifier::subsume_dummy_through_stamping(
    const Watched ps
    , const Watched qs
) {
    //only if none of the clauses were binary
    //Otherwise we cannot tell if the value in the cache is dependent
    //on the binary clause itself, so that would cause a circular de-
    //pendency

    if (!ps.isBinary() && !qs.isBinary()) {
        aggressive_elim_time_limit -= 20;
        if (solver->stamp.stampBasedClRem(toClear)) {
            return true;
        }
    }

    return false;
}

bool Simplifier::resolve_clauses(
    const Watched ps
    , const Watched qs
    , const Lit posLit
    , const bool aggressive
) {
    //If clause has already been freed, skip
    if (ps.isClause()
        && solver->clAllocator.getPointer(ps.getOffset())->freed()
    ) {
        return false;
    }
    if (qs.isClause()
        && solver->clAllocator.getPointer(qs.getOffset())->freed()
    ) {
        return false;
    }

    dummy.clear();
    toClear.clear();
    add_pos_lits_to_dummy_and_seen(ps, posLit);
    bool tautological = add_neg_lits_to_dummy_and_seen(qs, posLit);
    toClear = dummy;

    if (!tautological && aggressive
        && solver->conf.doAsymmTE
    ) {
        tautological = reverse_vivification_of_dummy(ps, qs, posLit);
    }

    if (!tautological && aggressive
        && solver->conf.doAsymmTE
        && solver->conf.doStamp
    ) {
        tautological = subsume_dummy_through_stamping(ps, qs);
    }

    *limit_to_decrease -= toClear.size()/2 + 1;
    for (const Lit lit: toClear) {
        seen[lit.toInt()] = 0;
    }

    return tautological;
}

bool Simplifier::agressiveCheck(
    const Lit lit
    , const Lit noPosLit
    , bool& retval
) {
    watch_subarray_const ws = solver->watches[lit.toInt()];
    aggressive_elim_time_limit -= ws.size()/3 + 2;
    for(watch_subarray::const_iterator it =
        ws.begin(), end = ws.end()
        ; it != end
        ; it++
    ) {
        //Can't do much with clauses, too expensive
        if (it->isClause())
            continue;

        //handle tri
        if (it->isTri() && !it->red()) {

            //See if any of the literals is in
            Lit otherLit = lit_Undef;
            unsigned inside = 0;
            if (seen[it->lit2().toInt()]) {
                otherLit = it->lit3();
                inside++;
            }

            if (seen[it->lit3().toInt()]) {
                otherLit = it->lit2();
                inside++;
            }

            //Could subsume
            if (inside == 2) {
                retval = false;
                return true;
            }

            //None is in, skip
            if (inside == 0)
                continue;

            if (otherLit.var() == noPosLit.var())
                continue;

            //Extend clause
            if (!seen[(~otherLit).toInt()]) {
                toClear.push_back(~otherLit);
                seen[(~otherLit).toInt()] = 1;
            }

            continue;
        }

        //Handle binary
        if (it->isBinary() && !it->red()) {
            const Lit otherLit = it->lit2();
            if (otherLit.var() == noPosLit.var())
                continue;

            //If (a V b) is irred, and in the clause, then we can remove
            if (seen[otherLit.toInt()]) {
                retval = false;
                return true;
            }

            //If (a) is in clause
            //then (a V b) means -b can be put inside
            if (!seen[(~otherLit).toInt()]) {
                toClear.push_back(~otherLit);
                seen[(~otherLit).toInt()] = 1;
            }
        }
    }

    return false;
}

Simplifier::HeuristicData Simplifier::calcDataForHeuristic(const Lit lit)
{
    HeuristicData ret;

    watch_subarray_const ws_list = solver->watches[lit.toInt()];
    *limit_to_decrease -= ws_list.size() + 100;
    for (const Watched ws: ws_list) {
        //Skip redundant clauses
        if (solver->redundant(ws))
            continue;

        switch(ws.getType()) {
            case watch_binary_t:
                ret.bin++;
                ret.lit += 2;
                break;

            case CMSat::watch_tertiary_t:
                ret.tri++;
                ret.lit += 3;
                break;

            case watch_clause_t: {
                const Clause* cl = solver->clAllocator.getPointer(ws.getOffset());
                assert(!cl->freed() && "Inside occur, so cannot be freed");
                ret.longer++;
                ret.lit += cl->size();
                break;
            }

            default:
                assert(false);
                break;
        }
    }
    return ret;
}

bool Simplifier::checkEmptyResolvent(Lit lit)
{
    //Take the smaller of the two
    if (solver->watches[(~lit).toInt()].size() < solver->watches[lit.toInt()].size())
        lit = ~lit;

    int num_bits_set = checkEmptyResolventHelper(
        lit
        , ResolvCount::set
        , 0
    );

    int num_resolvents = std::numeric_limits<int>::max();

    //Can only count if the POS was small enough
    //otherwise 'seen' cannot properly store the data
    if (num_bits_set < 16) {
        num_resolvents = checkEmptyResolventHelper(
            ~lit
            , ResolvCount::count
            , num_bits_set
        );
    }

    //Clear the 'seen' array
    checkEmptyResolventHelper(
        lit
        , ResolvCount::unset
        , 0
    );

    //Okay, this would be great
    return (num_resolvents == 0);
}


int Simplifier::checkEmptyResolventHelper(
    const Lit lit
    , const ResolvCount action
    , const int otherSize
) {
    uint16_t at = 1;
    int count = 0;
    size_t numCls = 0;

    watch_subarray_const watch_list = solver->watches[lit.toInt()];
    *limit_to_decrease -= watch_list.size()*2;
    for (const Watched& ws: watch_list) {
        if (numCls >= 16
            && (action == ResolvCount::set
                || action == ResolvCount::unset)
        ) {
            break;
        }

        if (count > 0
            && action == ResolvCount::count
        ) {
            break;
        }

        //Handle binary
        if (ws.isBinary()){
            //Only count irred
            if (!ws.red()) {
                *limit_to_decrease -= 4;
                switch(action) {
                    case ResolvCount::set:
                        seen[ws.lit2().toInt()] |= at;
                        break;

                    case ResolvCount::unset:
                        seen[ws.lit2().toInt()] = 0;
                        break;

                    case ResolvCount::count:
                        int num = __builtin_popcount(seen[(~ws.lit2()).toInt()]);
                        assert(num <= otherSize);
                        count += otherSize - num;
                        break;
                }
                at <<= 1;
                numCls++;
            }
            continue;
        }

        //Handle tertiary
        if (ws.isTri()) {
            //Only count irred
            if (!ws.red()) {
                *limit_to_decrease -= 4;
                switch(action) {
                    case ResolvCount::set:
                        seen[ws.lit2().toInt()] |= at;
                        seen[ws.lit3().toInt()] |= at;
                        break;

                    case ResolvCount::unset:
                        seen[ws.lit2().toInt()] = 0;
                        seen[ws.lit3().toInt()] = 0;
                        break;

                    case ResolvCount::count:
                        uint16_t tmp = seen[(~ws.lit2()).toInt()] | seen[(~ws.lit3()).toInt()];
                        int num = __builtin_popcount(tmp);
                        assert(num <= otherSize);
                        count += otherSize - num;
                        break;
                }
                at <<= 1;
                numCls++;
            }

            continue;
        }

        if (ws.isClause()) {
            const Clause* cl = solver->clAllocator.getPointer(ws.getOffset());

            //If in occur then it cannot be freed
            assert(!cl->freed());

            //Only irred is of relevance
            if (!cl->red()) {
                *limit_to_decrease -= cl->size()*2;
                uint16_t tmp = 0;
                for(const Lit l: *cl) {

                    //Ignore orig
                    if (l == lit)
                        continue;

                    switch (action) {
                        case ResolvCount::set:
                            seen[l.toInt()] |= at;
                            break;

                        case ResolvCount::unset:
                            seen[l.toInt()] = 0;
                            break;

                        case ResolvCount::count:
                            tmp |= seen[(~l).toInt()];
                            break;
                    }
                }
                at <<= 1;
                numCls++;

                //Count using tmp
                if (action == ResolvCount::count) {
                    int num = __builtin_popcount(tmp);
                    assert(num <= otherSize);
                    count += otherSize - num;
                }
            }

            continue;
        }

        //Only these types are possible
        assert(false);
    }

    switch(action) {
        case ResolvCount::count:
            return count;

        case ResolvCount::set:
            return numCls;

        case ResolvCount::unset:
            return 0;
    }

    assert(false);
    return std::numeric_limits<int>::max();
}



pair<int, int> Simplifier::heuristicCalcVarElimScore(const Var var)
{
    const Lit lit(var, false);
    const HeuristicData pos = calcDataForHeuristic(lit);
    const HeuristicData neg = calcDataForHeuristic(~lit);

    //Estimate cost
    int posTotalLonger = pos.longer + pos.tri;
    int negTotalLonger = neg.longer + neg.tri;
    int normCost;
    switch(solver->conf.varElimCostEstimateStrategy) {
        case 0:
            normCost =  posTotalLonger*negTotalLonger
                + pos.bin*negTotalLonger*2
                + neg.bin*posTotalLonger*2
                + pos.bin*neg.bin*3;
            break;

        case 1:
            normCost =  posTotalLonger*negTotalLonger
                + pos.bin*negTotalLonger*2
                + neg.bin*posTotalLonger*2
                + pos.bin*neg.bin*4;
            break;

        default:
            cout
            << "ERROR: Invalid var-elim cost estimation strategy"
            << endl;
            exit(-1);
            break;
    }


    /*if ((pos.longer + pos.tri + pos.bin) <= 2
        && (neg.longer + neg.tri + neg.bin) <= 2
    ) {
        normCost /= 2;
    }*/

    if ((pos.longer + pos.tri + pos.bin) == 0
        || (neg.longer + neg.tri + neg.bin) == 0
    ) {
        normCost = 0;
    }

    int litCost = pos.lit * neg.lit;

    return std::make_pair(normCost, litCost);
}

void Simplifier::order_vars_for_elim()
{
    varElimOrder.clear();
    varElimComplexity.clear();
    varElimComplexity.resize(
        solver->nVars()
        , std::make_pair<int, int>(1000, 1000)
    );

    //Go through all vars
    for (
        size_t var = 0
        ; var < solver->nVars() && *limit_to_decrease > 0
        ; var++
    ) {
        if (!can_eliminate_var(var))
            continue;

        *limit_to_decrease -= 50;
        assert(!varElimOrder.inHeap(var));
        varElimComplexity[var] = strategyCalcVarElimScore(var);
        varElimOrder.insert(var);
    }
    assert(varElimOrder.heapProperty());

    //Print sorted listed list
    #ifdef VERBOSE_DEBUG_VARELIM
    cout << "-----------" << endl;
    for(size_t i = 0; i < varElimOrder.size(); i++) {
        cout
        << "varElimOrder[" << i << "]: "
        << " var: " << varElimOrder[i]+1
        << " val: " << varElimComplexity[varElimOrder[i]].first
        << " , " << varElimComplexity[varElimOrder[i]].second
        << endl;
    }
    #endif
}

std::pair<int, int> Simplifier::strategyCalcVarElimScore(const Var var)
{
    std::pair<int, int> cost;
    if (solver->conf.varelimStrategy == 0) {
        cost = heuristicCalcVarElimScore(var);
    } else {
        int ret = test_elim_and_fill_resolvents(var);

        cost.first = ret;
        cost.second = 0;
    }

    return cost;
}

void Simplifier::checkElimedUnassigned() const
{
    for (size_t i = 0; i < solver->nVarsReal(); i++) {
        if (solver->varData[i].removed == Removed::elimed) {
            assert(solver->value(i) == l_Undef);
        }
    }
}

void Simplifier::checkElimedUnassignedAndStats() const
{
    assert(solver->ok);
    int64_t checkNumElimed = 0;
    for (size_t i = 0; i < solver->nVarsReal(); i++) {
        if (solver->varData[i].removed == Removed::elimed) {
            checkNumElimed++;
            assert(solver->value(i) == l_Undef);
        }
    }
    if (globalStats.numVarsElimed != checkNumElimed) {
        cout
        << "ERROR: globalStats.numVarsElimed is "<<
        globalStats.numVarsElimed
        << " but checkNumElimed is: " << checkNumElimed
        << endl;

        assert(false);
    }
}

size_t Simplifier::memUsed() const
{
    size_t b = 0;
    b += seen.capacity()*sizeof(char);
    b += seen2.capacity()*sizeof(char);
    b += dummy.capacity()*sizeof(char);
    b += toClear.capacity()*sizeof(Lit);
    b += finalLits.capacity()*sizeof(Lit);
    b += subsumeStrengthen->memUsed();
    for(map<Var, vector<size_t> >::const_iterator
        it = blk_var_to_cl.begin(), end = blk_var_to_cl.end()
        ; it != end
        ; it++
    ) {
        b += it->second.capacity()*sizeof(size_t);
    }
    b += blockedClauses.capacity()*sizeof(BlockedClause);
    for(vector<BlockedClause>::const_iterator
        it = blockedClauses.begin(), end = blockedClauses.end()
        ; it != end
        ; it++
    ) {
        b += it->lits.capacity()*sizeof(Lit);
    }
    b += blk_var_to_cl.size()*(sizeof(Var)+sizeof(vector<size_t>)); //TODO under-counting
    b += varElimOrder.memUsed();
    b += varElimComplexity.capacity()*sizeof(int)*2;
    b += touched.memUsed();
    b += clauses.capacity()*sizeof(ClOffset);

    return b;
}

size_t Simplifier::memUsedXor() const
{
    if (xorFinder)
        return xorFinder->memUsed();
    else
        return 0;
}

void Simplifier::freeXorMem()
{
    delete xorFinder;
    xorFinder = NULL;
}

void Simplifier::linkInClause(Clause& cl)
{
    assert(cl.size() > 3);
    ClOffset offset = solver->clAllocator.getOffset(&cl);
    std::sort(cl.begin(), cl.end());
    for (const Lit lit: cl) {
        watch_subarray ws = solver->watches[lit.toInt()];
        *limit_to_decrease -= ws.size();

        ws.push(Watched(offset, cl.abst));
    }
    assert(cl.abst == calcAbstraction(cl));
    cl.setOccurLinked(true);
}


void Simplifier::printGateFinderStats() const
{
    if (gateFinder) {
        gateFinder->getStats().print(solver->nVarsReal());
    }
}

Lit Simplifier::least_occurring_except(const OccurClause& c)
{
    *limit_to_decrease -= m_lits.size();
    for(const lit_pair lits: m_lits) {
        seen[lits.lit1.toInt()] = 1;
        if (lits.lit2 != lit_Undef) {
            seen[lits.lit2.toInt()] = 1;
        }
    }

    Lit smallest = lit_Undef;
    size_t smallest_val = std::numeric_limits<size_t>::max();
    const auto check_smallest = [&] (const Lit lit) {
        //Must not be in m_lits
        if (seen[lit.toInt()] != 0)
            return;

        const size_t watch_size = solver->watches[lit.toInt()].size();
        if (watch_size < smallest_val) {
            smallest = lit;
            smallest_val = watch_size;
        }
    };
    solver->for_each_lit_except_watched(c, check_smallest, limit_to_decrease);

    for(const lit_pair lits: m_lits) {
        seen[lits.lit1.toInt()] = 0;
        if (lits.lit2 != lit_Undef) {
            seen[lits.lit2.toInt()] = 1;
        }
    }

    return smallest;
}

Simplifier::lit_pair Simplifier::lit_diff_watches(const OccurClause& a, const OccurClause& b)
{
    //assert(solver->cl_size(a.ws) == solver->cl_size(b.ws));
    assert(a.lit != b.lit);
    solver->for_each_lit(b, [&](const Lit lit) {seen[lit.toInt()] = 1;}, limit_to_decrease);

    size_t num = 0;
    lit_pair toret = lit_pair(lit_Undef, lit_Undef);
    const auto check_seen = [&] (const Lit lit) {
        if (seen[lit.toInt()] == 0) {
            if (num == 0)
                toret.lit1 = lit;
            else
                toret.lit2 = lit;

            num++;
        }
    };
    solver->for_each_lit(a, check_seen, limit_to_decrease);
    solver->for_each_lit(b, [&](const Lit lit) {seen[lit.toInt()] = 0;}, limit_to_decrease);

    if (num >= 1 && num <= 2)
        return toret;
    else
        return lit_Undef;
}

Simplifier::lit_pair Simplifier::most_occuring_lit_in_potential(size_t& largest)
{
    largest = 0;
    lit_pair most_occur = lit_pair(lit_Undef, lit_Undef);
    std::sort(potential.begin(), potential.end());

    lit_pair last_occur = lit_pair(lit_Undef, lit_Undef);
    size_t num = 0;
    for(const PotentialClause pot: potential) {
        if (last_occur != pot.lits) {
            if (num >= largest) {
                largest = num;
                most_occur = last_occur;
            }
            last_occur = pot.lits;
            num = 1;
        } else {
            num++;
        }
    }
    if (num >= largest) {
        largest = num;
        most_occur = last_occur;
    }

    if (solver->conf.verbosity >= 5) {
        cout
        << "c [bva] ---> Most occuring lit in p: " << most_occur.lit1 << ", " << most_occur.lit2
        << " occur num: " << largest
        << endl;
    }

    return most_occur;
}

bool Simplifier::inside(const vector<Lit>& lits, const Lit notin) const
{
    for(const Lit lit: lits) {
        if (lit == notin)
            return true;
    }
    return false;
}

bool Simplifier::simplifies_system(const size_t num_occur) const
{
    //If first run, at least 2 must match, nothing else matters
    if (m_lits.size() == 1) {
        return num_occur >= 2;
    }

    assert(m_lits.size() > 1);
    int orig_num_red = simplification_size(m_lits.size(), m_cls.size());
    int new_num_red = simplification_size(m_lits.size()+1, num_occur);

    if (new_num_red <= 0)
        return false;

    if (new_num_red < orig_num_red)
        return false;

    return true;
}

int Simplifier::simplification_size(
    const int m_lits_size
    , const int m_cls_size
) const {
    return m_lits_size*m_cls_size-m_lits_size-m_cls_size;
}

void Simplifier::fill_potential(const Lit lit)
{
    for(const OccurClause& c: m_cls) {
        if (*limit_to_decrease < 0)
            break;

        const Lit l_min = least_occurring_except(c);
        if (l_min == lit_Undef)
            continue;

        m_lits_this_cl = m_lits;
        for(const lit_pair lits: m_lits_this_cl) {
            seen2[lits.lit1.toInt()] = 1;
        }

        if (solver->conf.verbosity >= 6 || bva_verbosity) {
            cout
            << "c [bva] Examining clause for addition to 'potential':"
            << solver->watched_to_string(c.lit, c.ws)
            << " -- Least occurring in this CL: " << l_min
            << endl;
        }

        *limit_to_decrease -= solver->watches[l_min.toInt()].size();
        for(const Watched& d_ws: solver->watches[l_min.toInt()]) {
            if (*limit_to_decrease < 0)
                goto end;

            OccurClause d(l_min, d_ws);
            //cout << "Under scrutiny: "<< solver->watched_to_string(d.lit, d.ws) << endl;
            if (c.ws != d.ws
                && (solver->cl_size(c.ws) == solver->cl_size(d.ws)
                    || solver->cl_size(c.ws)+1 == solver->cl_size(d.ws))
                && !solver->redundant(d.ws)
                && lit_diff_watches(c, d) == lit
            ) {
                const lit_pair diff = lit_diff_watches(d, c);
                if (seen2[diff.lit1.toInt()] == 0) {
                    potential.push_back(PotentialClause(diff, c));
                    m_lits_this_cl.push_back(diff);
                    seen2[diff.lit1.toInt()] = 1;

                    if (solver->conf.verbosity >= 6 || bva_verbosity) {
                        cout
                        << "c [bva] Added to P: "
                        << potential.back().to_string(solver)
                        << endl;
                    }
                }
            }
        }

        end:
        for(const lit_pair lits: m_lits_this_cl) {
            seen2[lits.lit1.toInt()] = 0;
        }
    }
}


bool Simplifier::VarBVAOrder::operator()(const uint32_t lit1_uint, const uint32_t lit2_uint) const
{
    return watch_irred_sizes[lit1_uint] > watch_irred_sizes[lit2_uint];
}

size_t Simplifier::calc_watch_irred_size(const Lit lit) const
{
    size_t num = 0;
    watch_subarray_const ws = solver->watches[lit.toInt()];
    for(const Watched w: ws) {
        if (w.isBinary() || w.isTri()) {
            num += !w.red();
            continue;
        }

        assert(w.isClause());
        const Clause& cl = *solver->clAllocator.getPointer(w.getOffset());
        num += !cl.red();
    }

    return num;
}

vector<size_t> Simplifier::calc_watch_irred_sizes() const
{
    vector<size_t> watch_irred_sizes;
    for(size_t i = 0; i < solver->nVars()*2; i++) {
        const Lit lit = Lit::toLit(i);
        const size_t irred_size = calc_watch_irred_size(lit);
        watch_irred_sizes.push_back(irred_size);
    }

    return watch_irred_sizes;
}

bool Simplifier::bounded_var_addition()
{
    bva_verbosity = false;
    assert(solver->ok);
    if (!solver->conf.do_bounded_variable_addition)
        return solver->okay();

    if (solver->conf.verbosity >= 3 || bva_verbosity) {
        cout << "c [bva] Running BVA" << endl;
    }

    propagate();
    limit_to_decrease = &bounded_var_elim_time_limit;
    solver->clauseCleaner->clean_implicit_clauses();
    if (solver->conf.doStrSubImplicit) {
        solver->subsumeImplicit->subsume_implicit(false);
    }

    bva_worked = 0;
    bva_simp_size = 0;
    var_bva_order.clear();
    watch_irred_sizes = calc_watch_irred_sizes();
    for(size_t i = 0; i < solver->nVars()*2; i++) {
        const Lit lit = Lit::toLit(i);
        if (solver->value(lit) != l_Undef
            || solver->varData[lit.var()].removed != Removed::none
        ) {
            continue;
        }
        var_bva_order.insert(lit.toInt());
    }

    double my_time = cpuTime();
    while(!var_bva_order.empty()) {
        if (*limit_to_decrease < 0)
            break;

        const Lit lit = Lit::toLit(var_bva_order.removeMin());
        if (solver->conf.verbosity >= 5 || bva_verbosity) {
            cout << "c [bva] trying lit " << lit << endl;
        }
        bool ok = try_bva_on_lit(lit);
        if (!ok)
            break;
    }

    if (solver->conf.verbosity >= 2) {
        cout
        << "c [bva] added: " << bva_worked
        << " simp: " << bva_simp_size
        << " T: " << cpuTime() - my_time
        << " T-out: " << (*limit_to_decrease <= 0 ? "Y" : "N")
        << endl;
    }

    return solver->okay();
}

void Simplifier::remove_duplicates_from_m_cls()
{
    if (m_cls.size() <= 1)
        return;

    std::function<bool (const OccurClause&, const OccurClause&)> mysort
        = [&] (const OccurClause& a, const OccurClause& b) {
            WatchType atype = a.ws.getType();
            WatchType btype = b.ws.getType();
            if (atype == watch_binary_t && btype != CMSat::watch_binary_t) {
                return true;
            }
            if (btype == watch_binary_t && atype != CMSat::watch_binary_t) {
                return false;
            }
            if (atype == watch_tertiary_t && btype != CMSat::watch_tertiary_t) {
                return true;
            }
            if (btype == watch_tertiary_t && atype != CMSat::watch_tertiary_t) {
                return false;
            }

            assert(atype == btype);
            switch(atype) {
                case CMSat::watch_binary_t: {
                    //subsumption could have time-outed
                    //assert(a.ws.lit2() != b.ws.lit2() && "Implicit has been cleaned of duplicates!!");
                    return a.ws.lit2() < b.ws.lit2();
                }
                case CMSat::watch_tertiary_t: {
                    if (a.ws.lit2() != b.ws.lit2()) {
                        return a.ws.lit2() < b.ws.lit2();
                    }
                    //subsumption could have time-outed
                    //assert(a.ws.lit3() != b.ws.lit3() && "Implicit has been cleaned of duplicates!!");
                    return a.ws.lit3() < b.ws.lit3();
                }
                case CMSat::watch_clause_t: {
                    *limit_to_decrease -= 20;
                    const Clause& cl_a = *solver->clAllocator.getPointer(a.ws.getOffset());
                    const Clause& cl_b = *solver->clAllocator.getPointer(b.ws.getOffset());
                    if (cl_a.size() != cl_b.size()) {
                        return cl_a.size() < cl_b.size();
                    }
                    //Clauses' lits are sorted, yay!
                    for(size_t i = 0; i < cl_a.size(); i++) {
                        *limit_to_decrease -= 1;
                        if (cl_a[i] != cl_b[i]) {
                            return cl_a[i] < cl_b[i];
                        }
                    }
                    return false;
                }
            }

            assert(false);
            return false;
    };

    *limit_to_decrease -= 2*m_cls.size()*std::sqrt(m_cls.size());
    std::sort(m_cls.begin(), m_cls.end(), mysort);
    size_t i = 0;
    size_t j = 0;
    for(; i+1 < m_cls.size(); i++) {
        const Watched& prev = m_cls[j].ws;
        const Watched& next = m_cls[i+1].ws;
        if (prev.getType() != next.getType()) {
            m_cls[j+1] = m_cls[i+1];
            j++;
            continue;
        }

        bool del = false;
        switch(prev.getType()) {
            case CMSat::watch_binary_t: {
                if (prev.lit2() == next.lit2())
                    del = true;
                break;
            }

            case CMSat::watch_tertiary_t: {
                if (prev.lit2() == next.lit2() && prev.lit3() == next.lit3())
                    del = true;
                break;
            }

            case CMSat::watch_clause_t: {
                *limit_to_decrease -= 10;
                const Clause& cl1 = *solver->clAllocator.getPointer(prev.getOffset());
                const Clause& cl2 = *solver->clAllocator.getPointer(next.getOffset());
                del = true;
                if (cl1.size() != cl2.size()) {
                    break;
                }
                for(size_t i = 0; i < cl1.size(); i++) {
                    *limit_to_decrease -= 1;
                    if (cl1[i] != cl2[i]) {
                        del = false;
                        break;
                    }
                }
                break;
            }
        }

        if (!del) {
            m_cls[j+1] = m_cls[i+1];
            j++;
        }
    }
    m_cls.resize(m_cls.size()-(i-j));

    if (solver->conf.verbosity >= 6 || bva_verbosity) {
        cout << "m_cls after cleaning: " << endl;
        for(const OccurClause& w: m_cls) {
            cout << "-> " << solver->watched_to_string(w.lit, w.ws) << endl;
        }
    }
}

bool Simplifier::try_bva_on_lit(const Lit lit)
{
    assert(solver->value(lit) == l_Undef);
    assert(solver->varData[lit.var()].removed == Removed::none);

    m_cls.clear();
    m_lits.clear();
    m_lits.push_back(lit);
    for(const Watched w: solver->watches[lit.toInt()]) {
        if (!solver->redundant(w)) {
            m_cls.push_back(OccurClause(lit, w));
            if (solver->conf.verbosity >= 6 || bva_verbosity) {
                cout << "1st adding to m_cls "
                << solver->watched_to_string(lit, w)
                << endl;
            }
        }
    }
    remove_duplicates_from_m_cls();

    while(true) {
        potential.clear();
        fill_potential(lit);
        if (*limit_to_decrease < 0)
            break;

        size_t num_occur;
        const lit_pair l_max = most_occuring_lit_in_potential(num_occur);
        if (simplifies_system(num_occur)) {
            m_lits.push_back(l_max);
            m_cls.clear();
            for(const PotentialClause pot: potential) {
                if (pot.lits == l_max) {
                    m_cls.push_back(pot.occur_cl);
                    if (solver->conf.verbosity >= 6 || bva_verbosity) {
                        cout << "-- max is : (" << l_max.lit1 << ", " << l_max.lit2 << "), adding to m_cls "
                        << solver->watched_to_string(pot.occur_cl.lit, pot.occur_cl.ws)
                        << endl;
                    }
                    assert(pot.occur_cl.lit == lit);
                }
            }
        } else {
            break;
        }
    }

    if (*limit_to_decrease < 0)
        return solver->okay();

    const int simp_size = simplification_size(m_lits.size(), m_cls.size());
    if (simp_size <= 0) {
        return solver->okay();
    }

    const bool ok = bva_simplify_system();
    return ok;
}

bool Simplifier::bva_simplify_system()
{
    touched.clear();
    int simp_size = simplification_size(m_lits.size(), m_cls.size());
    if (solver->conf.verbosity >= 6 || bva_verbosity) {
        cout
        << "c [bva] YES Simplification by "
        << simp_size
        << " with matching lits: ";
        for(const lit_pair l: m_lits) {
            cout << "(" << l.lit1;
            if (l.lit2 != lit_Undef) {
                cout << ", " << l.lit2;
            }
            cout << "), ";
        }
        cout << endl;
        cout << "c [bva] cls: ";
        for(OccurClause cl: m_cls) {
            cout
            << "(" << solver->watched_to_string(cl.lit, cl.ws) << ")"
            << ", ";
        }
        cout << endl;
    }
    bva_worked++;
    bva_simp_size += simp_size;

    solver->newVar(true);
    const Var newvar = solver->nVars()-1;
    const Lit new_lit(newvar, false);

    for(const lit_pair m_lit: m_lits) {
        vector<Lit> lits;
        lits.push_back(m_lit.lit1);
        if (m_lit.lit2 != lit_Undef) {
            lits.push_back(m_lit.lit2);
        }
        lits.push_back(new_lit);
        solver->addClauseInt(lits, false, ClauseStats(), false, &lits);
        touched.touch(lits);
    }

    for(const OccurClause m_cl: m_cls) {
        bool ok = add_longer_clause(~new_lit, m_cl);
        if (!ok)
            return false;
    }

    for(const lit_pair replace_lit: m_lits) {
       //cout << "Doing lit " << replace_lit << " replacing lit " << lit << endl;
        for(const OccurClause cl: m_cls) {
            remove_matching_clause(cl, replace_lit);
        }
    }

    update_touched_lits_in_bva();

    return solver->okay();
}

void Simplifier::update_touched_lits_in_bva()
{
    const vector<uint32_t>& touched_list = touched.getTouchedList();
    for(const uint32_t lit_uint: touched_list) {
        const Lit lit = Lit::toLit(lit_uint);
        if (var_bva_order.inHeap(lit.toInt())) {
            watch_irred_sizes[lit.toInt()] = calc_watch_irred_size(lit);
            var_bva_order.update(lit.toInt());
        }

        if (var_bva_order.inHeap((~lit).toInt())) {
            watch_irred_sizes[(~lit).toInt()] = calc_watch_irred_size(~lit);
            var_bva_order.update((~lit).toInt());
        }
    }
    touched.clear();
}

void Simplifier::remove_matching_clause(
    const OccurClause& cl
    , const lit_pair lit_replace
) {
    if (solver->conf.verbosity >= 6 || bva_verbosity) {
        cout
        << "c [bva] Removing cl "
        //<< solver->watched_to_string(lit_replace, cl.ws)
        << endl;
    }

    bool red;
    vector<Lit> torem;
    torem.push_back(lit_replace.lit1);
    if (lit_replace.lit2 != lit_Undef) {
        torem.push_back(lit_replace.lit2);
    }
    switch(cl.ws.getType()) {
        case CMSat::watch_binary_t:
            torem.push_back(cl.ws.lit2());
            red = cl.ws.red();
            break;

        case CMSat::watch_tertiary_t:
            torem.push_back(cl.ws.lit2());
            torem.push_back(cl.ws.lit3());
            red = cl.ws.red();
            break;

        case CMSat::watch_clause_t:
            const Clause* cl_orig = solver->clAllocator.getPointer(cl.ws.getOffset());
            for(const Lit lit: *cl_orig) {
                if (cl.lit != lit) {
                    torem.push_back(lit);
                }
            }
            red = cl_orig->red();
            break;
    }
    touched.touch(torem);

    switch(torem.size()) {
        case 2:
            solver->detachBinClause(torem[0], torem[1], red);
            break;

        case 3:
            solver->detachTriClause(torem[0], torem[1], torem[2], red);
            break;

        default:
            Clause* cl_new = find_cl_for_bva(torem, red);
            unlinkClause(solver->clAllocator.getOffset(cl_new));
            break;
    }
}

Clause* Simplifier::find_cl_for_bva(
    const vector<Lit>& torem
    , const bool red
) const {
    Clause* cl = NULL;
    for(const Lit lit: torem) {
        seen[lit.toInt()] = 1;
    }
    for(Watched w: solver->watches[torem[0].toInt()]) {
        if (!w.isClause())
            continue;

        cl = solver->clAllocator.getPointer(w.getOffset());
        if (cl->red() != red
            || cl->size() != torem.size()
        ) {
            continue;
        }

        bool OK = true;
        for(const Lit lit: *cl) {
            if (seen[lit.toInt()] == 0) {
                OK = false;
                break;
            }
        }

        if (OK)
            break;
    }

    for(const Lit lit: torem) {
        seen[lit.toInt()] = 0;
    }

    assert(cl != NULL);
    return cl;
}

bool Simplifier::add_longer_clause(const Lit new_lit, const OccurClause& cl)
{
    vector<Lit> lits;
    switch(cl.ws.getType()) {
        case CMSat::watch_binary_t: {
            lits.resize(2);
            lits[0] = new_lit;
            lits[1] = cl.ws.lit2();
            solver->addClauseInt(lits, false, ClauseStats(), false, &lits);
            break;
        }

        case CMSat::watch_tertiary_t: {
            lits.resize(3);
            lits[0] = new_lit;
            lits[1] = cl.ws.lit2();
            lits[2] = cl.ws.lit3();
            solver->addClauseInt(lits, false, ClauseStats(), false, &lits);
            break;
        }

        case CMSat::watch_clause_t: {
            const Clause& orig_cl = *solver->clAllocator.getPointer(cl.ws.getOffset());
            lits.resize(orig_cl.size());
            for(size_t i = 0; i < orig_cl.size(); i++) {
                if (orig_cl[i] == cl.lit) {
                    lits[i] = new_lit;
                } else {
                    lits[i] = orig_cl[i];
                }
            }
            Clause* newCl = solver->addClauseInt(lits, false, orig_cl.stats, false, &lits);
            if (newCl != NULL) {
                linkInClause(*newCl);
                ClOffset offset = solver->clAllocator.getOffset(newCl);
                clauses.push_back(offset);
            }
            break;
        }
    }
    touched.touch(lits);

    return solver->okay();
}

string Simplifier::PotentialClause::to_string(const Solver* solver) const
{
    std::stringstream ss;
    ss << solver->watched_to_string(occur_cl.lit, occur_cl.ws)
    << " -- lit: " << lits.lit1 << ", " << lits.lit2;

    return ss.str();
}

/*const GateFinder* Simplifier::getGateFinder() const
{
    return gateFinder;
}*/
