#include "../include/Metropolis.hpp"
#include <cmath>
#include <limits>
#include <random>

namespace Metropolis {

double computeCost(const std::vector<Solver::SystemFunction> &functions,
                   const std::vector<double> &targetValues,
                   const std::vector<double> &guess,
                   double tolerance, int maxIter) {
  try {
    std::vector<double> sol = Solver::solveSystem(functions, targetValues, guess, tolerance, maxIter);
    // Cost = sqrt( sum_i (F_i(sol) - target_i)^2 )
    double sumSq = 0.0;
    for (size_t i = 0; i < functions.size(); ++i) {
      double r = functions[i](sol) - targetValues[i];
      sumSq += r * r;
    }
    return std::sqrt(sumSq);
  } catch (...) {
    return std::numeric_limits<double>::infinity();
  }
}

std::vector<double> optimize(const std::vector<Solver::SystemFunction> &functions,
                             const std::vector<double> &targetValues,
                             const std::vector<double> &initialGuess,
                             int steps, double sigma, double T_metro,
                             double tolerance, int maxIter) {
  // Mersenne-Twister RNG seeded from hardware entropy
  std::mt19937_64 rng(std::random_device{}());
  std::normal_distribution<double> gauss(0.0, sigma);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  std::vector<double> current = initialGuess;
  double currentCost = computeCost(functions, targetValues, current, tolerance, maxIter);

  std::vector<double> best = current;
  double bestCost = currentCost;

  for (int step = 0; step < steps; ++step) {
    // Propose new guess: perturb each component with a Gaussian draw
    std::vector<double> proposal = current;
    for (size_t i = 0; i < proposal.size(); ++i) {
      proposal[i] += gauss(rng);
    }

    double proposalCost = computeCost(functions, targetValues, proposal, tolerance, maxIter);

    // Metropolis acceptance
    bool accept = false;
    if (proposalCost < currentCost) {
      accept = true;
    } else if (T_metro > 0.0 && std::isfinite(proposalCost)) {
      double dC = proposalCost - currentCost;
      accept = (uniform(rng) < std::exp(-dC / T_metro));
    }

    if (accept) {
      current = proposal;
      currentCost = proposalCost;
    }

    // Track global best
    if (proposalCost < bestCost) {
      best = proposal;
      bestCost = proposalCost;
    }
  }

  return best;
}

} // namespace Metropolis
