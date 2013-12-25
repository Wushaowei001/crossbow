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

#include "comphandler.h"
#include "compfinder.h"
#include "varreplacer.h"
#include "solver.h"
#include "varupdatehelper.h"
#include "watchalgos.h"
#include "clauseallocator.h"
#include "clausecleaner.h"
#include <iostream>
#include <assert.h>
#include <iomanip>

using namespace CMSat;
using std::make_pair;
using std::cout;
using std::endl;

//#define VERBOSE_DEBUG

CompHandler::CompHandler(Solver* _solver) :
    solver(_solver)
    , compFinder(NULL)
{
}

CompHandler::~CompHandler()
{
    if (compFinder != NULL) {
        delete compFinder;
    }
}

void CompHandler::newVar(const Var orig_outer)
{
    if (orig_outer == std::numeric_limits<Var>::max())
        savedState.push_back(l_Undef);
}

void CompHandler::saveVarMem()
{
}

void CompHandler::createRenumbering(const vector<Var>& vars)
{
    interToOuter.resize(solver->nVars());
    outerToInter.resize(solver->nVars());

    for(size_t i = 0, size = vars.size()
        ; i < size
        ; i++
    ) {
        outerToInter[vars[i]] = i;
        interToOuter[i] = vars[i];
    }
}

bool CompHandler::assumpsInsideComponent(const vector<Var>& vars)
{
    for(Var var: vars) {
        assert(solver->assumptionsSet.size() > var && "Variables that have been set must NOT be in a component");
        if (solver->assumptionsSet[var]) {
            return true;
        }
    }

    return false;
}

bool CompHandler::handle()
{
    assert(solver->okay());
    double myTime = cpuTime();
    solver->clauseCleaner->removeAndCleanAll();
    compFinder = new CompFinder(solver);
    if (!compFinder->findComps()) {
        return false;
    }
    if (compFinder->getTimedOut()) {
        return solver->okay();
    }

    const uint32_t num_comps = compFinder->getReverseTable().size();

    //If there is only one big comp, we can't do anything
    if (num_comps <= 1) {
        if (solver->conf.verbosity >= 3) {
            cout
            << "c [comp] Only one component, not handling it separately"
            << endl;
        }
        return true;
    }

    map<uint32_t, vector<Var> > reverseTable = compFinder->getReverseTable();
    assert(num_comps == compFinder->getReverseTable().size());

    //Get the sizes now
    vector<pair<uint32_t, uint32_t> > sizes;
    for (map<uint32_t, vector<Var> >::iterator
        it = reverseTable.begin()
        ; it != reverseTable.end()
        ; it++
    ) {
        sizes.push_back(make_pair(
            it->first //Comp number
            , (uint32_t)it->second.size() //Size of the table
        ));
    }

    //Sort according to smallest size first
    std::sort(sizes.begin(), sizes.end(), sort_pred());
    assert(sizes.size() > 1);

    size_t num_comps_solved = 0;
    size_t vars_solved = 0;
    for (uint32_t it = 0; it < sizes.size()-1; it++) {
        const uint32_t comp = sizes[it].first;
        vector<Var>& vars = reverseTable[comp];
        const bool cont = solve_component(it, comp, vars, num_comps);
        if (!cont) {
            break;
        }
        num_comps_solved++;
        vars_solved += vars.size();
    }

    if (!solver->okay())
        return false;

    //Coming back to the original instance now
    if (solver->conf.verbosity  >= 1) {
        cout
        << "c [comp] Coming back to original instance, solved "
        << num_comps_solved << " component(s), "
        << vars_solved << " vars"
        << " T: "
        << std::setprecision(2) << std::fixed
        << cpuTime() - myTime
        << endl;
    }

    //Filter out the variables that have been made non-decision
    solver->filterOrderHeap();
    check_local_vardata_sanity();

    delete compFinder;
    compFinder = NULL;
    return true;
}

bool CompHandler::solve_component(
    const uint32_t comp_at
    , const uint32_t comp
    , const vector<Var>& vars_orig
    , const size_t num_comps
) {
    for(const Var var: vars_orig) {
        assert(solver->value(var) == l_Undef);
    }

    if (vars_orig.size() > 100ULL*1000ULL) {
        //There too many variables -- don't create a sub-solver
        //I'm afraid that we will memory-out

        return true;
    }

    //Components with assumptions should not be removed
    if (assumpsInsideComponent(vars_orig))
        return true;

    vector<Var> vars(vars_orig);

    //Sort and renumber
    std::sort(vars.begin(), vars.end());
    /*for(Var var: vars) {
        cout << "var in component: " << solver->map_inter_to_outer(var) + 1 << endl;
    }*/
    createRenumbering(vars);

    //Print what we are going to do
    if (solver->conf.verbosity >= 1 && num_comps < 20) {
        cout
        << "c [comp] Solving component " << comp_at
        << " num vars: " << vars.size()
        << " ======================================="
        << endl;
    }

    //Set up new solver
    SolverConf conf;
    Solver newSolver(conf);
    configureNewSolver(&newSolver, vars.size());
    moveVariablesBetweenSolvers(&newSolver, vars, comp);

    //Move clauses over
    moveClausesImplicit(&newSolver, comp, vars);
    moveClausesLong(solver->longIrredCls, &newSolver, comp);
    moveClausesLong(solver->longRedCls, &newSolver, comp);

    const lbool status = newSolver.solve();
    //Out of time
    if (status == l_Undef) {
        readdRemovedClauses();
        return false;
    }

    if (status == l_False) {
        solver->ok = false;
        if (solver->conf.verbosity >= 2) {
            cout
            << "c [comp] The component is UNSAT -> problem is UNSAT"
            << endl;
        }
        return false;
    }

    check_solution_is_unassigned_in_main_solver(&newSolver, vars);
    save_solution_to_savedstate(&newSolver, vars, comp);
    move_decision_level_zero_vars_here(&newSolver, vars);

    if (solver->conf.verbosity >= 1 && num_comps < 20) {
        cout
        << "c [comp] Solved component " << comp_at
        << " ======================================="
        << endl;
    }
    return true;
}

void CompHandler::check_local_vardata_sanity()
{
    //Checking that all variables that are not in the remaining comp have
    //correct 'removed' flags, and none have been assigned

    for (Var var = 0; var < solver->nVars(); var++) {
        const Var outerVar = solver->map_inter_to_outer(var);
        if (savedState[outerVar] != l_Undef) {
            assert(solver->varData[var].is_decision == false);
            assert(solver->varData[var].removed == Removed::decomposed);
            assert(solver->value(var) == l_Undef || solver->varData[var].level == 0);
        }
    }
}

void CompHandler::check_solution_is_unassigned_in_main_solver(
    const Solver* newSolver
    , const vector<Var>& vars
) {
    for (size_t i = 0; i < vars.size(); i++) {
        Var var = vars[i];
        if (newSolver->model[updateVar(var)] != l_Undef) {
            assert(solver->value(var) == l_Undef);
        }
    }
}

void CompHandler::save_solution_to_savedstate(
    const Solver* newSolver
    , const vector<Var>& vars
    , const uint32_t comp
) {
    assert(savedState.size() == solver->nVarsReal());
    for (size_t i = 0; i < vars.size(); i++) {
        Var var = vars[i];
        Var outerVar = solver->map_inter_to_outer(var);
        if (newSolver->model[updateVar(var)] != l_Undef) {
            assert(savedState[outerVar] == l_Undef);
            assert(compFinder->getVarComp(var) == comp);

            savedState[outerVar] = newSolver->model[updateVar(var)];
        }
    }
}

void CompHandler::move_decision_level_zero_vars_here(
    const Solver* newSolver
    , const vector<Var>& vars
) {
    assert(newSolver->decisionLevel() == 0);
    assert(solver->decisionLevel() == 0);
    for (size_t i = 0; i < vars.size(); i++) {
        Var newSolverInternalVar = newSolver->map_outer_to_inter(i);

        //Is it 0-level assigned in newSolver?
        lbool val = newSolver->value(newSolverInternalVar);
        if (val != l_Undef) {
            assert(newSolver->varData[newSolverInternalVar].level == 0);

            //Use our 'solver'-s notation, i.e. 'var'
            Var var = vars[i];
            Lit lit(var, val == l_False);
            solver->varData[var].removed = Removed::none;
            const Var outer = solver->map_inter_to_outer(var);
            savedState[outer] = l_Undef;
            solver->enqueue(lit);

            /*cout
            << "0-level enqueueing var "
            << outer + 1
            << endl;*/

            //These vars are not meant to be in the orig solver
            //so they cannot cause UNSAT
            solver->ok = (solver->propagate().isNULL());
            assert(solver->ok);
        }
    }
}

/**
@brief Sets up the sub-solver with a specific configuration
*/
void CompHandler::configureNewSolver(
    Solver* newSolver
    , const size_t numVars
) const {
    newSolver->conf = solver->conf;
    newSolver->mtrand.seed(solver->mtrand.randInt());
    if (numVars < 60) {
        newSolver->conf.regularly_simplify_problem = false;
        newSolver->conf.doStamp = false;
        newSolver->conf.doCache = false;
        newSolver->conf.doProbe = false;
        newSolver->conf.otfHyperbin = false;
        newSolver->conf.verbosity = std::min(solver->conf.verbosity, 0);
    }

    //To small, don't clogger up the screen
    if (numVars < 20 && solver->conf.verbosity < 3) {
        newSolver->conf.verbosity = 0;
    }

    //Don't recurse
    newSolver->conf.doCompHandler = false;
}

/**
@brief Moves the variables to the new solver

This implies making the right variables decision in the new solver,
and making it non-decision in the old solver.
*/
void CompHandler::moveVariablesBetweenSolvers(
    Solver* newSolver
    , const vector<Var>& vars
    , const uint32_t comp
) {
    for(const Var var: vars) {
        //Misc check
        #ifdef VERBOSE_DEBUG
        if (!solver->varData[var].is_decision) {
            cout
            << "var " << var + 1
            << " is non-decision, but in comp... strange."
            << endl;
        }
        #endif //VERBOSE_DEBUG

        newSolver->new_external_var();
        assert(compFinder->getVarComp(var) == comp);

        assert(solver->varData[var].removed == Removed::none);
        assert(solver->varData[var].is_decision);
        solver->unsetDecisionVar(var);
        solver->varData[var].removed = Removed::decomposed;
    }
}

void CompHandler::moveClausesLong(
    vector<ClOffset>& cs
    , Solver* newSolver
    , const uint32_t comp
) {
    vector<Lit> tmp;

    vector<ClOffset>::iterator i, j, end;
    for (i = j = cs.begin(), end = cs.end()
        ; i != end
        ; i++
    ) {
        Clause& cl = *solver->clAllocator.getPointer(*i);

        //Irred, different comp
        if (!cl.red()) {
            if (compFinder->getVarComp(cl[0].var()) != comp) {
                //different comp, move along
                *j++ = *i;
                continue;
            }
        }

        if (cl.red()) {
            //Check which comp(s) it belongs to
            bool thisComp = false;
            bool otherComp = false;
            for (Lit* l = cl.begin(), *end2 = cl.end(); l != end2; l++) {
                if (compFinder->getVarComp(l->var()) == comp)
                    thisComp = true;

                if (compFinder->getVarComp(l->var()) != comp)
                    otherComp = true;
            }

            //In both comps, remove it
            if (thisComp && otherComp) {
                solver->detachClause(cl);
                solver->clAllocator.clauseFree(&cl);
                continue;
            }

            //In one comp, but not this one
            if (!thisComp) {
                //different comp, move along
                *j++ = *i;
                continue;
            }
            assert(thisComp && !otherComp);
        }

        //Let's move it to the other solver!
        #ifdef VERBOSE_DEBUG
        cout << "clause in this comp:" << cl << endl;
        #endif

        //Create temporary space 'tmp' and copy to backup
        tmp.resize(cl.size());
        for (size_t i = 0; i < cl.size(); i++) {
            tmp[i] = updateLit(cl[i]);
        }

        //Add 'tmp' to the new solver
        if (cl.red()) {
            cl.stats.conflictNumIntroduced = 0;
            //newSolver->addRedClause(tmp, cl.stats);
        } else {
            saveClause(cl);
            newSolver->addClauseOuter(tmp);
        }

        //Remove from here
        solver->detachClause(cl);
        solver->clAllocator.clauseFree(&cl);
    }
    cs.resize(cs.size() - (i-j));
}

void CompHandler::moveClausesImplicit(
    Solver* newSolver
    , const uint32_t comp
    , const vector<Var>& vars
) {
    vector<Lit> lits;
    uint32_t numRemovedHalfIrred = 0;
    uint32_t numRemovedHalfRed = 0;
    uint32_t numRemovedThirdIrred = 0;
    uint32_t numRemovedThirdRed = 0;

    for(const Var var: vars) {
    for(unsigned sign = 0; sign < 2; sign++) {
        const Lit lit = Lit(var, sign);
        watch_subarray ws = solver->watches[lit.toInt()];

        //If empty, nothing to to, skip
        if (ws.empty()) {
            continue;
        }

        Watched *i = ws.begin();
        Watched *j = i;
        for (Watched *end2 = ws.end()
            ; i != end2
            ; i++
        ) {
            //At least one variable inside comp
            if (i->isBinary()
                && (compFinder->getVarComp(lit.var()) == comp
                    || compFinder->getVarComp(i->lit2().var()) == comp
                )
            ) {
                const Lit lit2 = i->lit2();

                //Unless redundant, cannot be in 2 comps at once
                assert((compFinder->getVarComp(lit.var()) == comp
                            && compFinder->getVarComp(lit2.var()) == comp
                       ) || i->red()
                );

                //If it's redundant and the lits are in different comps, remove it.
                if (compFinder->getVarComp(lit.var()) != comp
                    || compFinder->getVarComp(lit2.var()) != comp
                ) {
                    //Can only be redundant, otherwise it would be in the same
                    //component
                    assert(i->red());

                    //The way we go through this, it's definitely going to be
                    //lit2 that's in the other component
                    assert(compFinder->getVarComp(lit2.var()) != comp);

                    removeWBin(solver->watches, lit2, lit, true);

                    //Update stats
                    solver->binTri.redBins--;

                    //Not copy, that's the other Watched removed
                    continue;
                }

                //don't add the same clause twice
                if (lit < lit2) {

                    //Add clause
                    lits = {updateLit(lit), updateLit(lit2)};
                    assert(compFinder->getVarComp(lit.var()) == comp);
                    assert(compFinder->getVarComp(lit2.var()) == comp);

                    //Add new clause
                    if (i->red()) {
                        //newSolver->addRedClause(lits);
                        numRemovedHalfRed++;
                    } else {
                        //Save backup
                        saveClause(vector<Lit>{lit, lit2});

                        newSolver->addClauseOuter(lits);
                        numRemovedHalfIrred++;
                    }
                } else {

                    //Just remove, already added above
                    if (i->red()) {
                        numRemovedHalfRed++;
                    } else {
                        numRemovedHalfIrred++;
                    }
                }

                //Yes, remove
                continue;
            }

            if (i->isTri()
                && (compFinder->getVarComp(lit.var()) == comp
                    || compFinder->getVarComp(i->lit2().var()) == comp
                    || compFinder->getVarComp(i->lit3().var()) == comp
                )
            ) {
                const Lit lit2 = i->lit2();
                const Lit lit3 = i->lit3();

                //Unless redundant, cannot be in 2 comps at once
                assert((compFinder->getVarComp(lit.var()) == comp
                            && compFinder->getVarComp(lit2.var()) == comp
                            && compFinder->getVarComp(lit3.var()) == comp
                       ) || i->red()
                );

                //If it's redundant and the lits are in different comps, remove it.
                if (compFinder->getVarComp(lit.var()) != comp
                    || compFinder->getVarComp(lit2.var()) != comp
                    || compFinder->getVarComp(lit3.var()) != comp
                ) {
                    assert(i->red());

                    //The way we go through this, it's definitely going to be
                    //either lit2 or lit3, not lit, that's in the other comp
                    assert(compFinder->getVarComp(lit2.var()) != comp
                        || compFinder->getVarComp(lit3.var()) != comp
                    );

                    //Update stats
                    solver->binTri.redTris--;

                    //We need it sorted, because that's how we know what order
                    //it is in the Watched()
                    lits = {lit, lit2, lit3};
                    std::sort(lits.begin(), lits.end());

                    //Remove only 2, the remaining gets removed by not copying it over
                    if (lits[0] != lit) {
                        removeWTri(solver->watches, lits[0], lits[1], lits[2], true);
                    }
                    if (lits[1] != lit) {
                        removeWTri(solver->watches, lits[1], lits[0], lits[2], true);
                    }
                    if (lits[2] != lit) {
                        removeWTri(solver->watches, lits[2], lits[0], lits[1], true);
                    }

                    //Not copying, that's the 3rd one
                    continue;
                }

                //don't add the same clause twice
                if (lit < lit2
                    && lit2 < lit3
                ) {

                    //Add clause
                    lits = {updateLit(lit), updateLit(lit2), updateLit(lit3)};
                    assert(compFinder->getVarComp(lit.var()) == comp);
                    assert(compFinder->getVarComp(lit2.var()) == comp);
                    assert(compFinder->getVarComp(lit3.var()) == comp);

                    //Add new clause
                    if (i->red()) {
                        //newSolver->addRedClause(lits);
                        numRemovedThirdRed++;
                    } else {
                        //Save backup
                        saveClause(vector<Lit>{lit, lit2, lit3});

                        newSolver->addClauseOuter(lits);
                        numRemovedThirdIrred++;
                    }
                } else {

                    //Just remove, already added above
                    if (i->red()) {
                        numRemovedThirdRed++;
                    } else {
                        numRemovedThirdIrred++;
                    }
                }

                //Yes, remove
                continue;
            }

            *j++ = *i;
        }
        ws.shrink_(i-j);
    }}

    assert(numRemovedHalfIrred % 2 == 0);
    solver->binTri.irredBins -= numRemovedHalfIrred/2;

    assert(numRemovedThirdIrred % 3 == 0);
    solver->binTri.irredTris -= numRemovedThirdIrred/3;

    assert(numRemovedHalfRed % 2 == 0);
    solver->binTri.redBins -= numRemovedHalfRed/2;

    assert(numRemovedThirdRed % 3 == 0);
    solver->binTri.redTris -= numRemovedThirdRed/3;
}

void CompHandler::addSavedState(vector<lbool>& solution)
{
    //Enqueue them. They may need to be extended, so enqueue is needed
    //manipulating "model" may not be good enough
    assert(savedState.size() == solver->nVarsReal());
    assert(solution.size() == solver->nVarsReal());
    for (size_t var = 0; var < savedState.size(); var++) {
        if (savedState[var] != l_Undef) {
            const Var interVar = solver->map_outer_to_inter(var);
            assert(solver->varData[interVar].removed == Removed::decomposed);
            assert(solver->varData[interVar].is_decision == false);

            const lbool val = savedState[var];
            assert(solution[var] == l_Undef);
            solution[var] = val;
            //cout << "Solution to var " << var + 1 << " has been added: " << val << endl;

            solver->varData[interVar].polarity = (val == l_True);
        }
    }
}

template<class T>
void CompHandler::saveClause(const T& lits)
{
    //Update variable number to 'outer' number. This means we will not have
    //to update the variables every time the internal variable numbering changes
    for (const Lit lit : lits ) {
        removedClauses.lits.push_back(
            solver->map_inter_to_outer(lit)
        );
    }
    removedClauses.sizes.push_back(lits.size());
}

void CompHandler::readdRemovedClauses()
{
    assert(solver->okay());
    double myTime = cpuTime();

    //Avoid recursion, clear 'removed' status
    for(size_t i = 0; i < solver->nVarsReal(); i++) {
        VarData& dat = solver->varData[i];
        if (dat.removed == Removed::decomposed) {
            dat.removed = Removed::none;
            solver->setDecisionVar(i);
        }
    }

     //Clear saved state
    for(lbool& val: savedState) {
        val = l_Undef;
    }

    vector<Lit> tmp;
    size_t at = 0;
    for (uint32_t sz: removedClauses.sizes) {

        //addClause() needs *outer* literals, so just do that
        tmp.clear();
        for(size_t i = at; i < at + sz; i++) {
            tmp.push_back(removedClauses.lits[i]);
        }
        if (solver->conf.verbosity >= 6) {
            cout << "c [comp] Adding back component clause " << tmp << endl;
        }

        //Add the clause to the system
        solver->addClause(tmp);
        assert(solver->okay());

        //Move 'at' along
        at += sz;
    }

    //Explain what we just did
    if (solver->conf.verbosity >= 2) {
        cout
        << "c [comp] re-added components. Lits: "
        << removedClauses.lits.size()
        << " cls:" << removedClauses.sizes.size()
        << " T: " << std::fixed << std::setprecision(2) << cpuTime() - myTime
        << endl;
    }

    //Clear added data
    removedClauses.lits.clear();
    removedClauses.sizes.clear();
}
