// This file is part of CRAAM, a C++ library for solving plain
// and robust Markov decision processes.
//
// MIT License
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#pragma once

#include "craam/Samples.hpp"
#include "craam/definitions.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace craam { namespace msen {

using namespace std;
using namespace util::lang;

/**
 * A simulator of an exponential population model with a control action.
 * The effectiveness of the control action is piecewiese quadratic
 * as a function of the population.
 */
class PopulationSim {

public:
    /// Type of state: current population
    using State = long;
    /// Type of actions: whether to apply control/treatment or not
    using Action = long;

    /// The type of the growth model
    enum class Growth { Exponential, Logistic };

    /**
     * Initializes to an exponential growth model by default.
     *
     * @param initial_population Starting population to start the simulation
     * @param carrying_capacity Maximum possible amount of population
     * @param mean_growth_rate Mean of the population growth rate
     * @param std_growth_rate Standard deviation of the growth rate
     * @param std_observation Standard deviation for the observation from the actual underlying population
     * @param beta_1 Coefficient of effectiveness
     * @param beta_2 Coefficient of effectiveness for the quadratic term
     * @param n_hat Threshold when the quadratic control effects kick in
     * @param seed Seed for random number generation
     */
    PopulationSim(long initial_population, long carrying_capacity,
                  prec_t mean_growth_rate, prec_t std_growth_rate, prec_t std_observation,
                  prec_t beta_1, prec_t beta_2, long n_hat,
                  random_device::result_type seed = random_device{}())
        : initial_population(initial_population), carrying_capacity(carrying_capacity),
          mean_growth_rate(mean_growth_rate), std_growth_rate(std_growth_rate),
          std_observation(std_observation), beta_1(beta_1), beta_2(beta_2), n_hat(n_hat),
          growth_model(Growth::Exponential), gen(seed) {}

    /// Rteurns the initial state
    long init_state() const { return initial_population; }

    /// The simulation does not have a defined end
    bool end_condition(State population) const { return false; }

    /**
        Returns a sample of the reward and a population level following an action & current population level

        When the treatment action is not chosen (action=0),
        then growth_rate = max(0,Normal(mean_growth_rate, std_growth_rate)), In this case, the growth rate is independent of the current population level.

        When the treatment is applied (action=1),
        then the growth rate depends on the current population level & the beta_x parameters come into play.

        The next population is obtained using the logistic or exponential population growth model:
            min(carrying_capacity, growth_rate * current_population * (carrying_capacity-current_population)/carrying_capacity)

        The observed population deviates from the actual population & is stored in observed_population.

        The treatment action carries a fixed cost control_reward (or a negative reward of -4000) of applying the treatment. There is a variable population dependent
        cost invasive_reward (-1) that represents the economic (or ecological) damage of the invasive species.

        @param current_population Current population level
        @param action Whether the control measure should be taken or not

        \returns a pair of reward & next population level
    */
    pair<prec_t, State> transition(State current_population, Action action) {
        assert(current_population >= 0);

        //
        const prec_t action_multiplier = action ? 1.0 : 0.0;

        const prec_t growth_rate_adj =
            action_multiplier * current_population * beta_1 +
            action_multiplier * pow(max(current_population - n_hat, 0l), 2) * beta_2;
        const prec_t exp_growth_rate = max(0.0, mean_growth_rate - growth_rate_adj);
        normal_distribution<prec_t> growth_rate_distribution(exp_growth_rate,
                                                             std_growth_rate);

        prec_t growth_rate = growth_rate_distribution(gen);
        long next_population = 0;
        if (growth_model == Growth::Exponential) {
            next_population =
                clamp(long(growth_rate * current_population), 0l, carrying_capacity);
        } else if (growth_model == Growth::Logistic) {
            next_population = clamp(long(growth_rate * current_population *
                                         (carrying_capacity - current_population) /
                                         prec_t(carrying_capacity)),
                                    0l, carrying_capacity);
        } else {
            throw invalid_argument("Unsupported population model.");
        }

        normal_distribution<prec_t> observation_distribution(next_population,
                                                             std_observation);
        long observed_population = max(0l, (long)observation_distribution(gen));
        prec_t reward = next_population * (-1) + action * (-4000);
        return make_pair(reward, observed_population);
    }

    Growth get_growth() const { return growth_model; }

    void set_growth(Growth model) { growth_model = model; }

protected:
    /// Random number engine
    long initial_population, carrying_capacity;
    prec_t mean_growth_rate, std_growth_rate, std_observation, beta_1, beta_2;
    long n_hat;
    /// Growth model
    Growth growth_model;
    /// Used for simulation purposes
    default_random_engine gen;
};

/**
A policy for population management that depends on the population
threshold & control probability.

\tparam Sim Simulator class for which the policy is to be constructed.
            Must implement an instance method actions(State).
 */
class PopulationPol {

public:
    using State = typename PopulationSim::State;
    using Action = typename PopulationSim::Action;

    PopulationPol(const PopulationSim& sim, long threshold_control = 0,
                  prec_t prob_control = 0.5,
                  random_device::result_type seed = random_device{}())
        : sim(sim), threshold_control(threshold_control), prob_control(prob_control),
          gen(seed) {
        control_distribution = binomial_distribution<int>(1, prob_control);
    }

    /**
     * Provides a control action depending on the current population level. If the population level is below a certain threshold,
     * the policy is not to take the control measure. Otherwise, it takes a control measure with a specific probability (which introduces
     * randomness in the policy).
    */
    long operator()(long current_state) {
        if (current_state >= threshold_control) return control_distribution(gen);
        return 0;
    }

protected:
    /// Internal reference to the originating simulator
    const PopulationSim& sim;
    long threshold_control;
    prec_t prob_control;
    /// Random number engine
    default_random_engine gen;
    binomial_distribution<int> control_distribution;
};
}} // namespace craam::msen
