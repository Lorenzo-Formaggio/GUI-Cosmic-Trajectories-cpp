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

double directResidualCost(const std::vector<Solver::SystemFunction> &functions,
                          const std::vector<double> &targetValues,
                          const std::vector<double> &guess) {
  double sumSq = 0.0;
  for (size_t i = 0; i < functions.size(); ++i) {
    double v;
    try {
      v = functions[i](guess);
    } catch (...) {
      return std::numeric_limits<double>::infinity();
    }
    if (!std::isfinite(v)) return std::numeric_limits<double>::infinity();
    double r = v - targetValues[i];
    sumSq += r * r;
  }
  return std::sqrt(sumSq);
}

std::vector<double> findFiniteStart(const std::vector<Solver::SystemFunction> &functions,
                                     const std::vector<double> &targetValues,
                                     const std::vector<double> &initialGuess,
                                     double sigma) {
  // Quick exit: initial guess already evaluates to a finite residual.
  if (std::isfinite(directResidualCost(functions, targetValues, initialGuess))) {
    return initialGuess;
  }

  std::mt19937_64 rng(std::random_device{}());
  std::vector<double> best = initialGuess;
  double bestCost = std::numeric_limits<double>::infinity();

  // Geometrically expand the search radius. Most NaN regions of the EoS are
  // crossed within a few × σ; the larger scales are a safety net.
  const double scales[]  = {1.0, 3.0, 10.0, 30.0, 100.0, 300.0};
  constexpr int samplesPerScale = 64;

  for (double scale : scales) {
    std::normal_distribution<double> gauss(0.0, sigma * scale);
    for (int s = 0; s < samplesPerScale; ++s) {
      std::vector<double> cand = initialGuess;
      for (auto &c : cand) c += gauss(rng);
      double cost = directResidualCost(functions, targetValues, cand);
      if (cost < bestCost) {
        bestCost = cost;
        best = cand;
      }
    }
    // Stop expanding as soon as we've found any real-valued region.
    if (std::isfinite(bestCost)) return best;
  }
  return best;
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

  // The chain is driven by the cheap direct residual norm (no nested
  // solver). Calling Solver::solveSystem inside every Metropolis step
  // would mean ~maxIter × N_eqs²  EoS evaluations per step — for 500
  // steps with the EntrCont contour EoS that's several minutes before
  // any log line is emitted, which feels like a hang. The public
  // computeCost() (full solver) is still available as a refinement.
  // Suppress unused-arg warnings for the forwarded solver settings.
  (void)tolerance; (void)maxIter;

  // If the initial guess sits in a NaN region of the EoS, prescan for a
  // point where the EoS is real before launching the random walk.
  std::vector<double> current = initialGuess;
  double currentCost = directResidualCost(functions, targetValues, current);
  if (!std::isfinite(currentCost)) {
    std::vector<double> seed = findFiniteStart(functions, targetValues, initialGuess, sigma);
    if (seed != initialGuess) {
      current = seed;
      currentCost = directResidualCost(functions, targetValues, current);
    }
  }

  std::vector<double> best = current;
  double bestCost = currentCost;

  for (int step = 0; step < steps; ++step) {
    // Propose new guess: perturb each component with a Gaussian draw
    std::vector<double> proposal = current;
    for (size_t i = 0; i < proposal.size(); ++i) {
      proposal[i] += gauss(rng);
    }

    double proposalCost = directResidualCost(functions, targetValues, proposal);

    // Metropolis acceptance.
    bool accept = false;
    if (!std::isfinite(currentCost) && std::isfinite(proposalCost)) {
      // Escape an unreachable starting point as soon as we sample a finite
      // one — without this the chain can sit on cost=∞ forever when both
      // the initial guess and proposals make the solver throw (e.g. NaN
      // regions of the EntrCont EoS).
      accept = true;
    } else if (proposalCost < currentCost) {
      accept = true;
    } else if (T_metro > 0.0 && std::isfinite(proposalCost) && std::isfinite(currentCost)) {
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
