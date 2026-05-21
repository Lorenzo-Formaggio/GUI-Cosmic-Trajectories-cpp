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

// Mirror a proposal so each component stays on the same side of zero as the
// initial guess. Cosmic-trajectory unknowns have physical sign tied to the
// inputs — e.g. negative lepton fractions ⇒ μQ < 0 — and letting Metropolis
// cross zero typically lands in NaN regions of the EoS. Components whose
// initial guess is essentially zero are left free to take either sign.
static void preserveSigns(std::vector<double> &proposal,
                          const std::vector<double> &initialGuess) {
  constexpr double kSignThreshold = 1e-9;
  for (size_t i = 0; i < proposal.size(); ++i) {
    if (std::abs(initialGuess[i]) <= kSignThreshold) continue;
    if (proposal[i] * initialGuess[i] < 0.0) {
      proposal[i] = -proposal[i];
    }
  }
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

  // The cosmic-trajectory unknowns span very different magnitudes
  // (μB/μQ ~ 10²–10³ MeV; lepton chemical potentials ~ 10⁻⁵ MeV). Neither
  // a uniform σ step nor a pure relative step works for every situation,
  // so we try BOTH and keep the best finite-cost sample:
  //
  //   • Absolute step σ·scale per component — covers cold-start guesses
  //     where some components start tiny (e.g. μB = 0.01 MeV) and the
  //     true solution lives O(σ·scale) away in absolute terms.
  //
  //   • Relative step max(5%·|guess[i]|, 1e-12)·scale per component —
  //     covers warm-start guesses near the solution, where smearing the
  //     leptons by σ MeV would destroy the lepton balance.
  const size_t N = initialGuess.size();
  std::vector<double> relStep(N);
  for (size_t i = 0; i < N; ++i) {
    relStep[i] = std::max(0.05 * std::abs(initialGuess[i]), 1e-12);
  }

  // Geometrically expand the search radius. Multiple outer "attempts" run
  // independent random walks with fresh seeds — bad luck on one set of draws
  // shouldn't make us give up if a finite region exists nearby.
  const double scales[]  = {1.0, 3.0, 10.0, 30.0, 100.0, 300.0};
  constexpr int samplesPerScale = 64;     // half absolute, half relative
  constexpr int kAttempts = 5;            // outer retry budget

  std::normal_distribution<double> unit(0.0, 1.0);
  for (int attempt = 0; attempt < kAttempts; ++attempt) {
    for (double scale : scales) {
      for (int sIdx = 0; sIdx < samplesPerScale; ++sIdx) {
        std::vector<double> cand = initialGuess;
        const bool useRelative = (sIdx >= samplesPerScale / 2);
        for (size_t i = 0; i < N; ++i) {
          const double step = useRelative ? relStep[i] * scale : sigma * scale;
          cand[i] += unit(rng) * step;
        }
        preserveSigns(cand, initialGuess);
        double cost = directResidualCost(functions, targetValues, cand);
        if (cost < bestCost) {
          bestCost = cost;
          best = cand;
        }
      }
      // Stop expanding as soon as we've found any real-valued region.
      if (std::isfinite(bestCost)) return best;
    }
    // Still ∞: try again with a fresh draw before giving up.
  }
  return best;
}

std::vector<double> optimize(const std::vector<Solver::SystemFunction> &functions,
                             const std::vector<double> &targetValues,
                             const std::vector<double> &initialGuess,
                             int steps, double sigma, double T_metro,
                             double tolerance, int maxIter) {
  // This is the original Metropolis flavour that the user verified working
  // on cosmic-trajectory cases: cost = ‖F(solveSystem(guess))‖, i.e. it runs
  // the full Newton solver inside every proposal and uses the resulting
  // residual as the score. Effectively this gives a *binary* cost surface
  // — 0 inside a Newton basin of attraction, ∞ outside — so the random
  // walk naturally lands in a basin and Newton finishes the job. The
  // continuous "directResidualCost" version had flat plateaus where the
  // chain would stall, which is what produced the "best residual: 2.105e+04"
  // repeated identically across attempts. Keep it simple.
  std::mt19937_64 rng(std::random_device{}());
  std::normal_distribution<double> gauss(0.0, sigma);
  std::uniform_real_distribution<double> uniform(0.0, 1.0);

  std::vector<double> current = initialGuess;
  double currentCost = computeCost(functions, targetValues, current, tolerance, maxIter);

  std::vector<double> best = current;
  double bestCost = currentCost;

  for (int step = 0; step < steps; ++step) {
    // Propose new guess: perturb each component with a Gaussian draw.
    std::vector<double> proposal = current;
    for (size_t i = 0; i < proposal.size(); ++i) {
      proposal[i] += gauss(rng);
    }

    double proposalCost = computeCost(functions, targetValues, proposal, tolerance, maxIter);

    // Metropolis acceptance. Always accept improvement; if worse, accept
    // with probability exp(-ΔC / T_metro). ∞ proposals (Newton fails from
    // there) are rejected — unless we're already at ∞, in which case any
    // finite proposal escapes.
    bool accept = false;
    if (!std::isfinite(currentCost) && std::isfinite(proposalCost)) {
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

    if (proposalCost < bestCost) {
      best = proposal;
      bestCost = proposalCost;
    }
  }

  return best;
}

} // namespace Metropolis
