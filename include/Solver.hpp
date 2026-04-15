#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <functional>
#include <vector>

namespace Solver {

// Type alias for a function that takes a vector of variables and returns a
// double
using SystemFunction = std::function<double(const std::vector<double> &)>;

/**
 * @brief Solves a system of non-linear equations F(x) = y.
 *
 * @param functions Vector of functions defining the system (F_i(x)).
 * @param targetValues Vector of target values (y_i) for each function.
 * @param initialGuess Vector of initial guesses for the variables (x).
 * @param tolerance Convergence tolerance (default 1e-6).
 * @param maxIter Maximum number of iterations (default 100).
 * @return std::vector<double> The solution vector x.
 * @throws std::runtime_error if the system dimensions mismatch or convergence
 * fails.
 */
std::vector<double> solveSystem(const std::vector<SystemFunction> &functions,
                                const std::vector<double> &targetValues,
                                const std::vector<double> &initialGuess,
                                double tolerance = 1e-6, int maxIter = 100);

} // namespace Solver

#endif // SOLVER_HPP
