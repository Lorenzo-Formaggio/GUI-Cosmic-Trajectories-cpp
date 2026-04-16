#include "../include/InterpolatedEoS.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace InterpolatedEoS {

// Internal storage for grid axes
static std::vector<double> T_grid;
static std::vector<double> muB_grid;
static std::vector<double> muQ_grid;

// Internal storage for data. Flattened 3D arrays.
// Indexing: [i_T * (N_muB * N_muQ) + j_muB * (N_muQ) + k_muQ]
static std::vector<double> data_nB;
static std::vector<double> data_nQ;
static std::vector<double> data_s;

static bool loaded = false;
static std::string current_filename = "";

// Helper to determine grid index. Returns index 'i' such that grid[i] <= val <
// grid[i+1]. Uses std::lower_bound.
static int get_index(const std::vector<double> &grid, double val) {
  if (grid.empty())
    return -1;
  if (val < grid.front())
    return 0; // Extrapolate using first interval
  if (val >= grid.back())
    return static_cast<int>(grid.size()) - 2; // Extrapolate using last interval

  // lower_bound returns first element >= val
  auto it = std::lower_bound(grid.begin(), grid.end(), val);

  // We want the index strictly less than or equal to val (actually the interval
  // start) If it points to begin, val is exactly the first (handled above) or
  // we shouldn't be here. If *it == val, we can use that as the start or use
  // previous. Generally for interpolation between i and i+1, we want i such
  // that grid[i] <= val <= grid[i+1].

  int idx = static_cast<int>(std::distance(grid.begin(), it));

  // If we found an exact match grid[idx] == val, we can technically use [idx,
  // idx+1] If val is between idx-1 and idx, lower_bound returns idx. So we want
  // idx-1.

  if (it == grid.end() || *it > val) {
    return idx - 1;
  }

  // If *it == val, we can return idx (but check if idx is last element)
  if (idx >= static_cast<int>(grid.size()) - 1) {
    return idx - 1;
  }
  return idx;
}

void loadTable(const std::string &filename) {
  if (loaded && current_filename == filename) {
    std::cout << "InterpolatedEoS: Table '" << filename << "' is already loaded in memory. Skipping reload." << std::endl;
    return;
  }

  if (loaded)
    cleanup();

  std::ifstream file(filename);
  if (!file.is_open()) {
    throw std::runtime_error(
        "ERROR: EoS Table file '" + filename +
        "' not found! Please ensure it exists in the directory.");
  }

  // Temporary sets to find unique axis values
  std::set<double> T_set;
  std::set<double> muB_set;
  std::set<double> muQ_set;

  // Struct to hold raw lines to avoid re-reading file
  struct Row {
    double T, muB, muQ, nB, nQ, s;
  };
  std::vector<Row> rows;

  std::string line;
  // Skip potential header if it starts with non-digit (naively)
  // Actually the generated file has "T muB..." header.
  // Proper check:
  while (std::getline(file, line)) {
    std::stringstream ss(line);
    double T, muB, muQ, nB, nQ, s_val;
    // Table format: T  muB  muQ  nB  nQ  s
    if (ss >> T >> muB >> muQ >> nB >> nQ >> s_val) {
      T_set.insert(T);
      muB_set.insert(muB);
      muQ_set.insert(muQ);
      rows.push_back({T, muB, muQ, nB, nQ, s_val});
    }
    // If fail, assume header or empty line and skip
  }

  if (rows.empty()) {
    throw std::runtime_error("ERROR: EoS Table file '" + filename +
                             "' is empty or contains no valid data!");
  }

  // Convert sets to sorted vectors
  T_grid.assign(T_set.begin(), T_set.end());
  muB_grid.assign(muB_set.begin(), muB_set.end());
  muQ_grid.assign(muQ_set.begin(), muQ_set.end());

  size_t NT = T_grid.size();
  size_t NB = muB_grid.size();
  size_t NQ = muQ_grid.size();

  // Verify grid completeness
  if (rows.size() != NT * NB * NQ) {
    std::cerr << "Warning: Data size (" << rows.size()
              << ") does not match grid dimensions (" << NT << "x" << NB << "x"
              << NQ << " = " << NT * NB * NQ << ")."
              << " Interpolation may be flawed if grid is sparse." << std::endl;
    // In a robust implementation, we would fail or handle sparse data.
    // For now, assume it's roughly correct but warn.
    // Actually, if it's not a tensor product grid, we cannot use simple
    // indexing. Let's assume user provides a full grid.
  }

  // Resize data vectors
  data_nB.resize(NT * NB * NQ, 0.0);
  data_nQ.resize(NT * NB * NQ, 0.0);
  data_s.resize(NT * NB * NQ, 0.0);

  // Populate data vectors
  // We need to map (T, muB, muQ) -> index
  // Using binary search (lower_bound) to find indices for each row
  // Also track which cells are filled
  std::vector<bool> filled(NT * NB * NQ, false);

  for (const auto &r : rows) {
    auto itT = std::lower_bound(T_grid.begin(), T_grid.end(), r.T);
    auto itB = std::lower_bound(muB_grid.begin(), muB_grid.end(), r.muB);
    auto itQ = std::lower_bound(muQ_grid.begin(), muQ_grid.end(), r.muQ);

    // Indices
    size_t i = std::distance(T_grid.begin(), itT);
    size_t j = std::distance(muB_grid.begin(), itB);
    size_t k = std::distance(muQ_grid.begin(), itQ);

    // Check bounds (should be exact match since we built grid from data)
    if (i < NT && j < NB && k < NQ) {
      size_t idx = i * (NB * NQ) + j * NQ + k;
      data_nB[idx] = r.nB;
      data_nQ[idx] = r.nQ;
      data_s[idx] = r.s;
      filled[idx] = true;
    }
  }

  // Fill empty cells with iterative Laplacian-like smoothing in the muB-muQ plane.
  // This ensures non-zero gradients instead of flat plateaus from nearest-neighbor.
  size_t filled_count = 0;
  std::vector<bool> newly_filled(NT * NB * NQ, false);
  
  // We repeat the smoothing until all cells are filled (simple diffusion)
  bool any_empty = true;
  while (any_empty) {
    any_empty = false;
    std::vector<double> next_nB = data_nB;
    std::vector<double> next_nQ = data_nQ;
    std::vector<double> next_s = data_s;
    std::vector<bool> next_filled = filled;

    for (size_t i = 0; i < NT; ++i) {
      for (size_t j = 0; j < NB; ++j) {
        for (size_t k = 0; k < NQ; ++k) {
          size_t idx = i * (NB * NQ) + j * NQ + k;
          if (!filled[idx]) {
            double sum_nB = 0, sum_nQ = 0, sum_s = 0;
            int count = 0;
            
            // Check 4 neighbors in muB-muQ plane
            auto check = [&](int dj, int dk) {
              int nj = (int)j + dj;
              int nk = (int)k + dk;
              if (nj >= 0 && nj < (int)NB && nk >= 0 && nk < (int)NQ) {
                size_t nidx = i * (NB * NQ) + nj * NQ + nk;
                if (filled[nidx]) {
                  sum_nB += data_nB[nidx];
                  sum_nQ += data_nQ[nidx];
                  sum_s += data_s[nidx];
                  count++;
                }
              }
            };

            check(-1, 0); check(1, 0); check(0, -1); check(0, 1);

            if (count > 0) {
              next_nB[idx] = sum_nB / count;
              next_nQ[idx] = sum_nQ / count;
              next_s[idx] = sum_s / count;
              next_filled[idx] = true;
              filled_count++;
            } else {
              any_empty = true;
            }
          }
        }
      }
    }
    data_nB = std::move(next_nB);
    data_nQ = std::move(next_nQ);
    data_s = std::move(next_s);
    filled = std::move(next_filled);
  }

  loaded = true;
  current_filename = filename;
  std::cout << "InterpolatedEoS: Loaded table. Grid: T[" << NT << "] x muB["
            << NB << "] x muQ[" << NQ << "]";
  if (filled_count > 0) {
    std::cout << " (" << filled_count << " empty cells filled by nearest-neighbor)";
  }
  std::cout << std::endl;
}

EoSValues evaluate(double T, double muB, double muQ) {
  if (!loaded) {
    throw std::runtime_error("InterpolatedEoS: Table not loaded.");
  }

  // Find indices
  int i = get_index(T_grid, T);
  int j = get_index(muB_grid, muB);
  int k = get_index(muQ_grid, muQ);

  // Indices for the 8 corners of the cube
  // (i, j, k) to (i+1, j+1, k+1)

  // Helper to access data safely
  auto get_val = [&](const std::vector<double> &data, int ix, int iy, int iz) {
    return data[ix * (muB_grid.size() * muQ_grid.size()) +
                iy * muQ_grid.size() + iz];
  };

  // Normalized coordinates (0 to 1) within the voxel
  double T0 = T_grid[i];
  double T1 = T_grid[i + 1];
  double muB0 = muB_grid[j];
  double muB1 = muB_grid[j + 1];
  double muQ0 = muQ_grid[k];
  double muQ1 = muQ_grid[k + 1];

  double xd = (T - T0) / (T1 - T0);
  double yd = (muB - muB0) / (muB1 - muB0);
  double zd = (muQ - muQ0) / (muQ1 - muQ0);

  // Trilinear interpolation formula
  // c000 = data[i][j][k]
  // c100 = data[i+1][j][k]
  // ...

  auto interpolate_field = [&](const std::vector<double> &data) {
    double c000 = get_val(data, i, j, k);
    double c100 = get_val(data, i + 1, j, k);
    double c010 = get_val(data, i, j + 1, k);
    double c001 = get_val(data, i, j, k + 1);
    double c110 = get_val(data, i + 1, j + 1, k);
    double c101 = get_val(data, i + 1, j, k + 1);
    double c011 = get_val(data, i, j + 1, k + 1);
    double c111 = get_val(data, i + 1, j + 1, k + 1);

    double c00 = c000 * (1 - xd) + c100 * xd;
    double c01 = c001 * (1 - xd) + c101 * xd;
    double c10 = c010 * (1 - xd) + c110 * xd;
    double c11 = c011 * (1 - xd) + c111 * xd;

    double c0 = c00 * (1 - yd) + c10 * yd;
    double c1 = c01 * (1 - yd) + c11 * yd;

    return c0 * (1 - zd) + c1 * zd;
  };

  EoSValues res;
  res.nB = interpolate_field(data_nB);
  res.nQ = interpolate_field(data_nQ);
  res.s = interpolate_field(data_s);

  return res;
}

bool isLoaded() { return loaded; }

double getTmin() {
  if (!loaded || T_grid.empty()) return 0.0;
  return T_grid.front();
}

double getTmax() {
  if (!loaded || T_grid.empty()) return 0.0;
  return T_grid.back();
}

std::string getLoadedFilename() {
  return current_filename;
}

void cleanup() {
  T_grid.clear();
  muB_grid.clear();
  muQ_grid.clear();
  data_nB.clear();
  data_nQ.clear();
  data_s.clear();
  loaded = false;
  current_filename = "";
}

} // namespace InterpolatedEoS
