#ifndef METROPOLIS_HPP
#define METROPOLIS_HPP

#include "Solver.hpp"
#include <vector>

namespace Metropolis {

/**
 * @brief Evaluates the cost C = sqrt(sum_i (F_i(guess) - target_i)^2)
 *        by attempting to solve and computing residuals.
 *        Returns std::numeric_limits<double>::infinity() if the solver throws.
 */
double computeCost(const std::vector<Solver::SystemFunction> &functions,
                   const std::vector<double> &targetValues,
                   const std::vector<double> &guess,
                   double tolerance = 1e-6, int maxIter = 100);

/**
 * @brief Metropolis Monte Carlo optimizer for the initial guess.
 *
 * Stochastically explores the guess space to minimise computeCost.
 *
 * @param functions    System functions.
 * @param targetValues Target values.
 * @param initialGuess Starting guess.
 * @param steps        Number of Metropolis steps.
 * @param sigma        Gaussian step size (MeV) for each component.
 * @param T_metro      Metropolis acceptance temperature (dimensionless cost scale).
 * @param tolerance    Forwarded to computeCost / solveSystem.
 * @param maxIter      Forwarded to computeCost / solveSystem.
 * @return std::vector<double> The best guess found.
 */
std::vector<double> optimize(const std::vector<Solver::SystemFunction> &functions,
                             const std::vector<double> &targetValues,
                             const std::vector<double> &initialGuess,
                             int steps, double sigma, double T_metro,
                             double tolerance = 1e-6, int maxIter = 100);

} // namespace Metropolis

#endif // METROPOLIS_HPP
