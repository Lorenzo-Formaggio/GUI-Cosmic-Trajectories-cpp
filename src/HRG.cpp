#include "HRG.hpp"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#ifdef HRG_WITH_THERMALFIST
#include "CosmicEos/EffectiveMassModel.h"
#include "HRGBase.h"
#include "HRGVDW.h"
#include "ThermalFISTConfig.h"
using namespace thermalfist;
#endif

namespace HRG {

namespace {

/* Unit conversion, identical to scontours/QvdWReference.cpp.
 * Thermal-FIST works in GeV and fm^-3. */
constexpr double kHbarC = 197.3269804; /* MeV fm */
constexpr double kGeV = 1000.0;        /* MeV */
const double kHbarC3 = kHbarC * kHbarC * kHbarC;   /* fm^-3 -> MeV^3 */
const double kGeVfm3ToMeV4 = kGeV * kHbarC3;       /* GeV/fm^3 -> MeV^4 */

Params g_params;
bool g_initialized = false;

/* --------------------------------------------------------------------------
 * Evaluation cache.
 *
 * A QvdW solve is orders of magnitude more expensive than the contour algebra
 * that consumes it, and the callers query the same seam point repeatedly (the
 * trajectory solver evaluates the residual and then each Jacobian column at the
 * same (muB, muQ); QCDTherm caches the contour but not across EoS switches).
 * Memoizing on the exact double bit pattern of (T, muB, muQ, muS) is safe --
 * a hit means literally the same point -- and turns those repeats into lookups.
 * -------------------------------------------------------------------------- */
struct Key {
  double T, muB, muQ, muS;
  bool operator<(const Key &o) const {
    if (T != o.T) return T < o.T;
    if (muB != o.muB) return muB < o.muB;
    if (muQ != o.muQ) return muQ < o.muQ;
    return muS < o.muS;
  }
};
struct Entry {
  Result res;
  double eps; /* energy density, MeV^4 */
};

constexpr std::size_t kCacheMax = 8192;
std::map<Key, Entry> g_cache;

void clearCache() { g_cache.clear(); }

/* Serializes access to the Thermal-FIST model and the cache.
 *
 * Thermal-FIST's ThermalModelVDWFull is a single stateful object: an evaluation
 * writes T and the chemical potentials into it, solves, and leaves the densities
 * in its member arrays. The GUI calls the equation of state from more than one
 * thread -- the simulation worker runs in its own QThread, and its Metropolis
 * fallback fans chains out over std::async while the main thread can be driving
 * the EoS explorer -- so concurrent evaluations would interleave writes into the
 * same model and silently return another thread's point.
 *
 * Serializing here is the honest fix: the solve is the expensive step, but it is
 * not reentrant, and a wrong number is worse than a slow one. Callers that want
 * parallelism should evaluate distinct points and let the memo absorb repeats. */
std::mutex g_mutex;

} /* namespace */

#ifdef HRG_WITH_THERMALFIST

namespace {

std::unique_ptr<ThermalParticleSystem> g_tps;
std::unique_ptr<ThermalModelVDWFull> g_model;

/* Replace the ideal-gas density model of the listed species by the ChPT-matched
 * effective mass model (Thermal-FIST's CosmicEos module). Identical to
 * scontours/QvdWReference.cpp::SetEffectiveMassModels. */
void setEffectiveMassModels(ThermalModelBase *model,
                            const std::vector<long long> &pdgs, double f) {
  for (long long pdg : pdgs) {
    if (model->TPS()->PdgToId(pdg) == -1)
      continue;
    const ThermalParticle &part = model->TPS()->ParticleByPDG(pdg);
    model->SetDensityModelForParticleSpeciesByPdg(
        pdg,
        new EffectiveMassModel(part, new EMMFieldPressureChPT(part.Mass(), f)));
  }
}

/* (Re)fill the virial (b) and attraction (a) matrices. Non-zero only between
 * same-sign baryons, i.e. baryon-baryon and antibaryon-antibaryon; mesons and
 * baryon-antibaryon pairs stay non-interacting. */
void fillInteractions() {
  ThermalModelVDWFull *m = g_model.get();
  const double a = g_params.a / kGeV; /* MeV fm^3 -> GeV fm^3 */
  const double b = g_params.b;        /* fm^3 */
  const auto &parts = m->TPS()->Particles();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    const int B1 = parts[i].BaryonCharge();
    for (std::size_t j = 0; j < parts.size(); ++j) {
      const int B2 = parts[j].BaryonCharge();
      const bool sameSignBaryons = (B1 * B2 > 0);
      m->SetVirial(static_cast<int>(i), static_cast<int>(j),
                   sameSignBaryons ? b : 0.0);
      m->SetAttraction(static_cast<int>(i), static_cast<int>(j),
                       sameSignBaryons ? a : 0.0);
    }
  }
}

/* Solve the QvdW equations at (T, mu) -- the single expensive step. Everything
 * else (pressure, densities, entropy, energy) is read off this one solution.
 * Same call sequence as scontours::QvdWHRG. */
void solveAt(double T, double muB, double muQ, double muS) {
  ThermalModelVDWFull *m = g_model.get();
  m->SetTemperature(T / kGeV);
  m->SetBaryonChemicalPotential(muB / kGeV);
  m->SetElectricChemicalPotential(muQ / kGeV);
  m->SetStrangenessChemicalPotential(muS / kGeV);
  m->FixParameters();      /* solve the vdW shifted chemical potentials */
  m->CalculateDensities(); /* primordial densities, needed for the pressure */
}

/* Solve once and pack every thermodynamic quantity we expose. */
Entry evaluate(double T, double muB, double muQ, double muS) {
  ThermalModelVDWFull *m = g_model.get();
  solveAt(T, muB, muQ, muS);

  const double T3 = T * T * T;
  const double T4 = T3 * T;

  Entry e{};
  e.res.P_T4 = m->CalculatePressure() * kGeVfm3ToMeV4 / T4;
  e.res.nB_T3 = m->BaryonDensity() * kHbarC3 / T3;
  e.res.nQ_T3 = m->ElectricChargeDensity() * kHbarC3 / T3;
  e.res.nS_T3 = m->StrangenessDensity() * kHbarC3 / T3;
  e.res.s_T3 = m->CalculateEntropyDensity() * kHbarC3 / T3;
  e.eps = m->CalculateEnergyDensity() * kGeVfm3ToMeV4;
  return e;
}

/* Returns by value, not by reference into the cache: another thread may clear
 * the cache between the lookup and the caller's use of the result. */
Entry cachedEvaluate(double T, double muB, double muQ, double muS) {
  if (!(T > 0.0))
    throw std::runtime_error("HRG: T must be positive");

  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized)
    throw std::runtime_error("HRG: not initialized");

  const Key key{T, muB, muQ, muS};
  auto it = g_cache.find(key);
  if (it != g_cache.end())
    return it->second;
  if (g_cache.size() >= kCacheMax)
    g_cache.clear(); /* cheap bounded-memory policy; the seam grid is small */
  const Entry e = evaluate(T, muB, muQ, muS);
  g_cache.emplace(key, e);
  return e;
}

bool fileReadable(const std::string &path) {
  if (path.empty())
    return false;
  std::ifstream f(path);
  return f.good();
}

} /* namespace */

void initialize(const Params &params) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized)
    return;
  g_params = params;

  std::string list = g_params.particleList;
  if (!fileReadable(list))
    list = std::string(ThermalFIST_INPUT_FOLDER) + "/list/PDG2020/list.dat";
  if (!fileReadable(list))
    throw std::runtime_error("HRG: cannot open particle list " + list);
  g_params.particleList = list;

  g_tps.reset(new ThermalParticleSystem(list));
  g_model.reset(new ThermalModelVDWFull(g_tps.get()));
  ThermalModelVDWFull *m = g_model.get();

  fillInteractions();

  m->SetStatistics(g_params.quantumStatistics);
  m->SetUseWidth(g_params.resonanceWidth);
  m->ClearDensityModels();
  if (g_params.useEMMPions)
    setEffectiveMassModels(m, {211, 111, -211}, g_params.emmPionFPi);
  if (g_params.useEMMKaons)
    setEffectiveMassModels(m, {321, -321, 311, -311}, g_params.emmKaonFKa);
  /* chemical potentials are set explicitly, no neutrality constraints */
  m->ConstrainMuS(false);
  m->ConstrainMuQ(false);
  m->ConstrainMuC(false);

  clearCache();
  g_initialized = true;
}

void cleanup() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  clearCache();
  g_model.reset();
  g_tps.reset();
  g_initialized = false;
}

int particleCount() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_initialized ? static_cast<int>(g_tps->Particles().size()) : 0;
}

void setVdWParameters(double a_MeV_fm3, double b_fm3) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (a_MeV_fm3 == g_params.a && b_fm3 == g_params.b)
    return;
  g_params.a = a_MeV_fm3;
  g_params.b = b_fm3;
  if (g_initialized) {
    fillInteractions();
    clearCache();
  }
}

Result eval(double T, double muB, double muQ, double muS) {
  return cachedEvaluate(T, muB, muQ, muS).res;
}

double energyDensity(double T, double muB, double muQ, double muS) {
  return cachedEvaluate(T, muB, muQ, muS).eps;
}

std::array<std::array<double, 3>, 3>
susceptibilitiesD2P(double T, double muB, double muQ, double muS) {
  const std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_initialized)
    throw std::runtime_error("HRG: not initialized");
  ThermalModelVDWFull *m = g_model.get();
  solveAt(T, muB, muQ, muS);
  m->CalculateFluctuations();
  /* SusceptibilityDimensionfull is d^2P/dmu_i dmu_j in GeV^2 (the doxygen unit
   * "GeV^-2" is a typo: dimensionally it is GeV^2, verified against dn_i/dmu_j);
   * GeV^2 -> MeV^2 is *kGeV^2. The ConservedCharge enum (B=0,Q=1,S=2) matches
   * our index convention. */
  std::array<std::array<double, 3>, 3> d2P{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      d2P[i][j] = m->SusceptibilityDimensionfull(
                      static_cast<ConservedCharge::Name>(i),
                      static_cast<ConservedCharge::Name>(j)) *
                  kGeV * kGeV;
  return d2P;
}

#else /* built without Thermal-FIST */

namespace {
[[noreturn]] void missing() {
  throw std::runtime_error(
      "HRG: the QvdW-HRG seam model requires Thermal-FIST. Check out the "
      "submodule (git submodule add "
      "https://github.com/vlvovch/Thermal-FIST.git external/Thermal-FIST) and "
      "reconfigure; gui/CMakeLists.txt picks it up automatically.");
}
} /* namespace */

void initialize(const Params &params) {
  g_params = params;
  missing();
}
void cleanup() { clearCache(); g_initialized = false; }
int particleCount() { return 0; }
void setVdWParameters(double a_MeV_fm3, double b_fm3) {
  g_params.a = a_MeV_fm3;
  g_params.b = b_fm3;
}
Result eval(double, double, double, double) { missing(); }
double energyDensity(double, double, double, double) { missing(); }
std::array<std::array<double, 3>, 3> susceptibilitiesD2P(double, double, double,
                                                         double) {
  missing();
}

#endif /* HRG_WITH_THERMALFIST */

/* ---- shared by both build configurations ---- */

void initialize(const std::string &listPath) {
  Params p;
  p.particleList = listPath;
  initialize(p);
}

bool isInitialized() {
  const std::lock_guard<std::mutex> lock(g_mutex);
  return g_initialized;
}

const Params &parameters() { return g_params; }

/* Compatibility shim: the previous excluded-volume HRG took a meson and a
 * baryon eigenvolume. The QvdW-HRG has non-interacting mesons, so the meson
 * value has no counterpart and is dropped; the baryon value is the QvdW b.
 * Existing callers pass (1.0, 3.42) and 3.42 fm^3 is the reference b. */
void setExcludedVolumes(double /*b_meson_fm3*/, double b_baryon_fm3) {
  setVdWParameters(g_params.a, b_baryon_fm3);
}

double getMesonExcludedVolume() { return 0.0; }
double getBaryonExcludedVolume() { return g_params.b; }

} // namespace HRG
