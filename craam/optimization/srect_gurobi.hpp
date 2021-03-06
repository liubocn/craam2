// This file is part of CRAAM, a C++ library for solving plain
// and robust Markov decision processes.
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "craam/definitions.hpp"

#ifdef GUROBI_USE

#include <gurobi/gurobi_c++.h>
#include <memory>

namespace craam {

using namespace std;

/**
 * Solve the s-rectangular solution using gurobi linear solver
 *
 * max_{d in R^S} min_{p in R^{A*S}} sum_{a in A} d_a * z_a^T p_a
 * s.t. 1^T d = 1
 *      sum_{a in A} \| p_a - \bar{p}_a \|_{1,w_a} <= \kappa
 *      1^T p_a = 1
 *      p_a >= 0
 *      d >= 0
 *
 * The inner minimization problem can be reformulated as the following *linear*
 * program. The dual variables corresponding each constraint is noted in
 * parentheses.
 *
 * min_{p, \theta in \theta in R^{|A| x |S|}} sum_{a in A} (d_a * z_a^T p_a)
 * s.t. 1^T p_a = 1                                     (dual: x_a)
 *      p_a - \bar{p}_a >= - \theta_a                   (dual: y^n_a)
 *      \bar{p}_a - p_a >= - \theta_a                   (dual: y^p_a)
 *      - sum_{a in A} w_a\tr \theta_a >= - \kappa      (dual: \lambda)
 *      p >= 0
 *
 * Dualizing the inner optimization problem, we get the full linear program for
 * computing s-rectangular Bellman updates:
 * max_{d,x in R^{|A|},\lambda in R, y^p,y^n in R^{|S| x |A|}}
 *        sum_{a in A} ( x_a - \bar{p}_a\tr (y^n_a - y^p_a) ) - \kappa \lambda
 * s.t.    1^T d = 1       d >= 0
 *          - y^p_a + y^n_a + x * 1         <= d_a z_a       a in A
 *          y^p_a + y^n_a - \lambda * w_a   <= 0             a in A
 *          y^p >= 0      y^n >= 0
 *          lambda >= 0
 *
 * @param z Expected returns for each state and action (a state-length vector
 * for each action)
 * @param pbar Nominal transition probability (a state-length vector for each
 * action)
 * @param w Weights assigned to the L1 errors (optional). A uniform vector of
 * all ones if omitted
 * @param policy_eval The policy of the decision maker (d in the optimization problem
 *          described above). This policy is used in the evaluation
 *          step for a randomized policy. The parameter is optional
 *
 * @returns A tuple with: objective value, policy, and the individual budgets xi_a
 */
std::tuple<double, numvec, numvec>
srect_l1_solve_gurobi(const GRBEnv& env, const numvecvec& z, const numvecvec& pbar,
                      const prec_t kappa, const numvecvec& w = numvecvec(0),
                      const numvec& policy_eval = numvec(0)) {

    // general constants values
    const double inf = std::numeric_limits<prec_t>::infinity();

    assert(pbar.size() == z.size());
    assert(w.empty() || w.size() == z.size());

    // helpful numbers of actions
    const size_t nactions = pbar.size();
    // number of transition states for each action
    std::vector<size_t> statecounts(nactions);
    transform(pbar.cbegin(), pbar.cend(), statecounts.begin(),
              [](const numvec& v) { return v.size(); });
    // the number of states per action does not need to be the same
    // (when transitions are sparse)
    const size_t nstateactions =
        accumulate(statecounts.cbegin(), statecounts.cend(), size_t(0));

    // construct the LP model
    GRBModel model = GRBModel(env);

    // Create varables: duals of the nature problem
    auto x = std::unique_ptr<GRBVar[]>(model.addVars(
        numvec(nactions, -inf).data(), nullptr, nullptr,
        std::vector<char>(nactions, GRB_CONTINUOUS).data(), nullptr, int(nactions)));
    //  outer loop: actions, inner loop: next state
    auto yp = std::unique_ptr<GRBVar[]>(
        model.addVars(nullptr, nullptr, nullptr,
                      std::vector<char>(nstateactions, GRB_CONTINUOUS).data(), nullptr,
                      int(nstateactions)));
    auto yn = std::unique_ptr<GRBVar[]>(
        model.addVars(nullptr, nullptr, nullptr,
                      std::vector<char>(nstateactions, GRB_CONTINUOUS).data(), nullptr,
                      int(nstateactions)));

    auto lambda = model.addVar(0, inf, -kappa, GRB_CONTINUOUS, "lambda");

    // primal variables for the nature
    auto d = std::unique_ptr<GRBVar[]>(model.addVars(
        numvec(nactions, 0).data(), nullptr, numvec(nactions, 0).data(),
        std::vector<char>(nactions, GRB_CONTINUOUS).data(), nullptr, int(nactions)));

    // constraints on the primal policy decision
    // if a policy_eval is provided, then add constraints
    assert(policy_eval.empty() || policy_eval.size() == z.size());
    if (!policy_eval.empty()) {
        // make sure that the provided policy is a probability distribution
        assert(is_probability_dist(policy_eval.begin(), policy_eval.end()));
        for (size_t i = 0; i < policy_eval.size(); ++i) {
            model.addConstr(d[i] == policy_eval[i]);
        }
    }
    // if the policy is not provided, we want to optimize over the value
    else {
        // constraint on the policy pi
        GRBLinExpr ones;
        ones.addTerms(numvec(nactions, 1.0).data(), d.get(), int(nactions));
        model.addConstr(ones, GRB_EQUAL, 1, "pi");
    }

    // objective
    GRBLinExpr objective;

    size_t i = 0;
    // constraints dual to variables of the inner problem
    for (size_t actionid = 0; actionid < nactions; actionid++) {
        objective += x[actionid];
        for (size_t stateid = 0; stateid < statecounts[actionid]; stateid++) {
            // objective
            objective += -pbar[actionid][stateid] * yp[i];
            objective += pbar[actionid][stateid] * yn[i];
            // dual for p
            model.addConstr(
                x[actionid] - yp[i] + yn[i] <= d[actionid] * z[actionid][stateid], "P");
            // dual for z
            double weight = w.size() > 0 ? w[actionid][stateid] : 1.0;
            model.addConstr(-lambda * weight + yp[i] + yn[i] <= 0, "psi");
            // update the counter (an absolute index for each variable)
            ++i;
        }
    }

    // add the dual variable to the objective
    objective += -lambda * kappa;

    // set objective
    model.setObjective(objective, GRB_MAXIMIZE);

    // run optimization
    model.optimize();

    int status = model.get(GRB_IntAttr_Status);
    //if ((status == GRB_INF_OR_UNBD) || (status == GRB_INFEASIBLE) ||
    //    (status == GRB_UNBOUNDED)) {
    if (status != GRB_OPTIMAL) { throw invalid_argument("Failed to solve the LP."); }

    // retrieve policy values
    numvec policy(nactions);
    for (size_t i = 0; i < nactions; i++) {
        policy[i] = d[i].get(GRB_DoubleAttr_X);
    }

    // retrieve the worst-case response values
    //Obtain Dual variable values for sa_budget, dual of psi
    int constrNum = model.get(GRB_IntAttr_NumConstrs);

    numvec budgets;
    for (int j = 0; j < constrNum; j++) {
        if (model.getConstr(j).get(GRB_StringAttr_ConstrName) == "psi")
            budgets.push_back(model.getConstr(j).get(GRB_DoubleAttr_Pi));
    }

    return {model.get(GRB_DoubleAttr_ObjVal), move(policy), budgets};
}

/**
* Solve the s-rectangular L-infinity problem using gurobi linear solver
*
* max_{d in R^S} min_{p in R^{A*S}} sum_{a in A} d_a * z_a^T p_a
* s.t. 1^T d = 1
*      sum_{a in A} \| p_a - \bar{p}_a \|_{inf,w_a} <= \kappa
*      1^T p_a = 1
*      p_a >= 0
*      d >= 0
*
* The inner minimization problem can be reformulated as the following *linear* program.
* The dual variables corresponding each constraint is noted in parentheses.
*
* min_{p, \theta in \theta in R^{|A| x |S|}} sum_{a in A} (d_a * z_a^T p_a)
* s.t. max{ |p_a1^s1 - \bar{p}_a1^s1|, |p_a1^s2 - \bar{p}_a1^s2|, ... } <= \theta_1
*      max{ |p_a2^s1 - \bar{p}_a2^s1|, |p_a2^s2 - \bar{p}_a2^s2|, ... } <= \theta_2
*      ....
*      max{ |p_aA^s1 - \bar{p}_aA^s1|, |p_aA^s2 - \bar{p}_aA^s2|, ... } <= \theta_A
*      \theta_1 + \theta_2 + ... +theta_A <= \kappa
*      1^T p_a = 1
*      p >= 0
*
*
* min_{p, \theta in \theta in R^{|A| x |S|}} sum_{a in A} (d_a * z_a^T p_a)
* s.t. |p_a1^s1 - \bar{p}_a1^s1| <= \theta_1,
*      |p_a1^s2 - \bar{p}_a1^s2| <= \theta_1,
 *      ...                                         (all the next states for action 1)
*      |p_a2^s1 - \bar{p}_a2^s1| <= \theta_2,
*      |p_a2^s2 - \bar{p}_a2^s2| <= \theta_2,
*      ...                                          (all the next states for action 2)
*      ...                                          (all the actions upto A)
*      |p_aA^s1 - \bar{p}_aA^s1| <= \theta_A,
*      |p_aA^s2 - \bar{p}_aA^s2| <= \theta_A,
*      ...                                          (all the next states for action A)
*      \theta_1 + \theta_2 + ... +theta_A <= \kappa
*      1^T p_a = 1
*      p >= 0
*
*
* min_{p, \theta in \theta in R^{|A| x |S|}} sum_{a in A} (d_a * z_a^T p_a)
* s.t. p_a1^s1 - \bar{p}_a1^s1 <= \theta_1,        (Dual: y_11^p) (y_action=1,state=1^positive)
*      \bar{p}_a1^s1 - p_a1^s1 <= \theta_1,        (Dual: y_11^n) (y_action=1,state=1^negative)
*      p_a1^s2 - \bar{p}_a1^s2 <= \theta_1,        (Dual: y_12^p) (y_action=1,state=2^positive)
*      \bar{p}_a1^s2 - p_a1^s2 <= \theta_1,        (Dual: y_12^n) (y_action=1,state=2^negative)
*      ...                                         (all the next states for action 1)
*      p_a2^s1 - \bar{p}_a2^s1 <= \theta_2,        (Dual: y_21^p) (y_action=2,state=1^positive)
*      \bar{p}_a2^s1 - p_a2^s1 <= \theta_2,        (Dual: y_21^n) (y_action=2,state=1^negative)
*      ...                                          (all the next states for action 2)
*      ...                                          (all the actions upto A)
*      \theta_1 + \theta_2 + ... +theta_A <= \kappa (Dual: \lambda)
*      1^T p_a = 1                                  (Dual: X_a)
*      p >= 0
*
* Take Lagrange Multipler with dual variables to make it unconstrained optimization problem.
* Rearrange the expressions to seperate terms with primal variables (p & \theta).
* Take dual by adding constraint for each expression of primal variables.
*
* max_{d,x in R^{|A|},\lambda in R, y^p,y^n in R^{|S| x |A|}}
*          sum_{a in A} ( x_a - \bar{p}_a\tr (y^n_a - y^p_a) ) - \kappa \lambda
*  s.t.    1^T d = 1       d >= 0
*          - y^p_a + y^n_a + x * 1         <= d_a z_a       a in A
*          y^p_a^s1 + y^n_a^s1 + y^p_a^s2 + y^n_a^s2 + ... - \lambda * w_a   <= 0             a in A
*          y^p >= 0      y^n >= 0
*          \lambda >= 0
*
* @param z Expected returns for each state and action (a state-length vector for each action)
* @param pbar Nominal transition probability (a state-length vector for each action)
* @param kappa Total allocated budget value (will get distributed over all the actions)
* @param w Weights assigned to the L1 errors (optional). A uniform vector of all ones if omitted
* @returns A tuple with: objective value, policy, sa_budgets
*/
std::tuple<double, numvec, numvec>
srect_linf_solve_gurobi(const GRBEnv& env, const numvecvec& z, const numvecvec& pbar,
                        const prec_t kappa, const numvecvec& w = numvecvec(0)) {
    // general constants values
    const double inf = std::numeric_limits<prec_t>::infinity();

    assert(pbar.size() == z.size());
    assert(w.empty() || w.size() == z.size());

    // helpful numbers of actions
    const size_t nactions = pbar.size();

    // number of transition states for each action
    std::vector<size_t> statecounts(nactions);

    transform(pbar.cbegin(), pbar.cend(), statecounts.begin(),
              [](const numvec& v) { return v.size(); });

    // the number of states per action does not need to be the same
    // (when transitions are sparse)
    const size_t nstateactions =
        accumulate(statecounts.cbegin(), statecounts.cend(), size_t(0));

    // construct the LP model
    GRBModel model = GRBModel(env);

    // Create varables: duals of the nature problem
    auto x = std::unique_ptr<GRBVar[]>(model.addVars(
        numvec(nactions, -inf).data(), nullptr, nullptr,
        std::vector<char>(nactions, GRB_CONTINUOUS).data(), nullptr, int(nactions)));
    //  outer loop: actions, inner loop: next state
    auto yp = std::unique_ptr<GRBVar[]>(model.addVars(
        0, nullptr, nullptr, std::vector<char>(nstateactions, GRB_CONTINUOUS).data(),
        nullptr, int(nstateactions)));
    auto yn = std::unique_ptr<GRBVar[]>(model.addVars(
        0, nullptr, nullptr, std::vector<char>(nstateactions, GRB_CONTINUOUS).data(),
        nullptr, int(nstateactions)));

    auto lambda = model.addVar(0, inf, -kappa, GRB_CONTINUOUS, "lambda");

    // primal variables for the nature
    auto d = std::unique_ptr<GRBVar[]>(model.addVars(
        numvec(nactions, 0).data(), nullptr, numvec(nactions, 0).data(),
        std::vector<char>(nactions, GRB_CONTINUOUS).data(), nullptr, int(nactions)));
    // objective
    GRBLinExpr objective;

    size_t i = 0;
    // constraints dual to variables of the inner problem
    for (size_t actionid = 0; actionid < nactions; actionid++) {
        objective += x[actionid];
        GRBLinExpr z_dual;
        for (size_t stateid = 0; stateid < statecounts[actionid]; stateid++) {
            // objective
            objective += -pbar[actionid][stateid] * yp[i];
            objective += pbar[actionid][stateid] * yn[i];
            // dual for p
            model.addConstr(
                x[actionid] - yp[i] + yn[i] <= d[actionid] * z[actionid][stateid], "P");
            // dual for z
            z_dual += yp[i] + yn[i];
            // update the counter (an absolute index for each variable)
            i++;
        }

        //TODO: figure out how the weight should work in s-rectangular case.
        // Should the weight vector contain weights for each action and then for next state?
        double weight = 1.0; //w.size() > 0 ? w[actionid][stateid] : 1.0;
        model.addConstr(-lambda * weight + z_dual <= 0, "theta");
    }
    objective += -lambda * kappa;

    // constraint on the policy pi
    GRBLinExpr ones;
    ones.addTerms(numvec(nactions, 1.0).data(), d.get(), int(nactions));
    model.addConstr(ones, GRB_EQUAL, 1, "policy");

    // set objective
    model.setObjective(objective, GRB_MAXIMIZE);

    // run optimization
    model.optimize();

    // retrieve policy values
    numvec policy(nactions, 0);
    for (size_t i = 0; i < nactions; i++) {
        policy[i] = d[i].get(GRB_DoubleAttr_X);
    }

    //Obtain Dual variable values for sa_budget, (dual of theta)
    int constrNum = model.get(GRB_IntAttr_NumConstrs);

    numvec budgets(nactions);
    int index = 0;
    for (int j = 0; j < constrNum; j++) {
        if (model.getConstr(j).get(GRB_StringAttr_ConstrName) == "theta")
            budgets[index++] = model.getConstr(j).get(GRB_DoubleAttr_Pi);
    }

    return make_tuple(model.get(GRB_DoubleAttr_ObjVal), move(policy), budgets);
}

} // namespace craam
#endif
