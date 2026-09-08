#ifndef GIBBSMIXEDPHASE_HPP
#define GIBBSMIXEDPHASE_HPP

#include "ContourEoSCore.hpp"

#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

/**
 * @file GibbsMixedPhase.hpp
 * @brief Gibbs (mixed-phase) treatment of the first-order region of the
 *        Entropy Contour EoS: eos = 5 (tabulated input) and 6 (parametrized).
 *
 * Past the critical point the contour EoS has two mechanically stable phases
 * at the same (T, mu_B, mu_Q): a dilute and a dense one. The homogeneous
 * models (eos = 3, 4) always return the phase of highest pressure, so along a
 * trajectory the densities jump when mu_B crosses the coexistence value
 * mu_B*(T, mu_Q) -- a Maxwell construction: the system is entirely in one
 * phase or entirely in the other.
 *
 * With conserved charges (n_B/s = b, electric neutrality with the leptons) the
 * system can instead sit ON the coexistence surface as a mixture: a volume
 * fraction lambda in the dilute phase and 1 - lambda in the dense one, both at
 * the same T, mu_B*, mu_Q and pressure (the Gibbs conditions), the conserved
 * densities being the lever-rule averages
 *
 *     n_X = lambda n_X^dil + (1 - lambda) n_X^den,   s likewise,   P = P*.
 *
 * A mixture is not a function of (T, mu) alone, so the mixed phase is exposed
 * through a "stretched" baryon coordinate x that the trajectory solver uses in
 * place of mu_B:
 *
 *     x <= mu_B*             homogeneous dilute phase at mu_B = x
 *     mu_B* < x < mu_B* + W  mixed phase at mu_B = mu_B*, lambda = 1 - (x - mu_B*)/W
 *     x >= mu_B* + W         homogeneous dense phase at mu_B = x - W
 *
 * Every thermodynamic quantity is continuous in x, so the existing Newton
 * solver walks through the transition; W is an arbitrary width (100 MeV by
 * default) that never enters a physical result -- physicalMuB() and
 * phaseFraction() map x back. Where there is no coexistence at (T, mu_Q),
 * i.e. above the critical temperature of that direction, x is simply mu_B.
 *
 * mu_B*(T, mu_Q) is located exactly by bisection of the model's own branch
 * switch (as gui/first_order_surface.cpp does) on the bare contour: the QvdW
 * seam cancels in the equal-pressure condition, so the search needs no
 * Thermal-FIST solve; the two coexisting phases are then evaluated with the
 * seam on. Strangeness: mu_S = 0 throughout (cosmic-trajectory convention).
 *
 * Units: T, mu in MeV; densities MeV^3; pressure MeV^4.
 */
namespace GibbsPhase {

/** Thermodynamics of one phase, or of the mixture. */
struct State {
  double nB = std::numeric_limits<double>::quiet_NaN();
  double nQ = std::numeric_limits<double>::quiet_NaN();
  double nS = std::numeric_limits<double>::quiet_NaN();
  double s = std::numeric_limits<double>::quiet_NaN();
  double P = std::numeric_limits<double>::quiet_NaN();
  bool valid = false;
};

/** The homogeneous contour model the mixed phase is built on (eos 3 or 4). */
struct Model {
  /** Contour at (mu_B, mu_Q, mu_S), with or without the QvdW-HRG seam. */
  std::function<ContourEoS::Contour(double muB, double muQ, double muS,
                                    bool useHRG)>
      build;
  /** Thermodynamics at T on a contour, with the model's own branch selection
   *  (the stable branch of highest pressure). False outside the domain. */
  std::function<bool(double T, const ContourEoS::Contour &c, State &out)>
      evaluate;
  double Tlow = 80.0; /**< MeV, the seam temperature (domain edge) */
};

/** Coexistence at (T, mu_Q): the two phases at mu_B*, seam included. */
struct Coexistence {
  bool found = false;
  double muB = std::numeric_limits<double>::quiet_NaN();
  State dilute, dense;
};

/** An evaluation in the stretched coordinate x. */
struct Result {
  double muB = std::numeric_limits<double>::quiet_NaN(); /**< physical mu_B */
  double lambda = std::numeric_limits<double>::quiet_NaN(); /**< dilute fraction; NaN if homogeneous */
  bool mixed = false;
  State state; /**< the mixture (lever rule) or the homogeneous phase */
};

class MixedPhaseEoS {
public:
  struct Options {
    double width = 100.0;   /**< MeV, extent of the mixed phase in x */
    double muBmin = 400.0;  /**< MeV, full-scan range of the coexistence search */
    double muBmax = 1100.0;
    double coarse = 4.0;    /**< MeV, coarse step of the search */
    double tol = 1e-6;      /**< MeV, bisection tolerance on mu_B* */
    double delta = 0.02;    /**< ln s jump within one coarse step that triggers a bisection */
    double minJump = 0.005; /**< ln s jump that must survive the bisection */
    double minDnB = 1e-3;   /**< fm^-3, density jump that must survive it */
    double window = 25.0;   /**< MeV, half-width of the warm-started search */
    double eps = 1e-4;      /**< MeV, offset at which the two phases are read off */
  };

  /* Two overloads rather than a defaulted argument: `Options()` cannot be used
   * as a default argument for a nested type whose enclosing class is still
   * incomplete at that point. */
  explicit MixedPhaseEoS(Model model);
  MixedPhaseEoS(Model model, const Options &opts);

  /** Coexistence at (T, mu_Q), memoized on the exact point. */
  Coexistence coexistence(double T, double muQ) const;

  /** Thermodynamics at (T, x, mu_Q) in the stretched coordinate. */
  Result evaluate(double T, double x, double muQ) const;

  /** x -> physical mu_B, and the dilute fraction (NaN if homogeneous). */
  double physicalMuB(double T, double x, double muQ) const;
  double phaseFraction(double T, double x, double muQ) const;
  /** physical mu_B of a homogeneous state -> x (the dense side is shifted by W). */
  double coordinate(double T, double muB, double muQ) const;

  const Options &options() const { return opts_; }

  /** Highest temperature at which a coexistence exists for any mu_Q (the top
   *  of the first-order region); determined lazily by a scan. Above it no
   *  search is attempted. */
  double criticalTemperatureBound() const;

private:
  State homogeneous(double T, double muB, double muQ) const;
  bool bareState(double T, double muB, double muQ, State &st) const;
  bool locate(double T, double muQ, double from, double to, double &muBcoex) const;
  double tcMaxLocked() const;

  Model model_;
  Options opts_;
  mutable std::mutex mutex_;
  mutable std::map<std::pair<double, double>, Coexistence> memo_;
  mutable bool haveLast_ = false;
  mutable double lastT_ = 0.0, lastMuQ_ = 0.0, lastMuB_ = 0.0;
  mutable bool haveTcMax_ = false;
  mutable double tcMax_ = 0.0;
};

} // namespace GibbsPhase

#endif // GIBBSMIXEDPHASE_HPP
