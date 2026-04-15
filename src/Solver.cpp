#include "../include/Solver.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Solver {

// Helper: Gaussian elimination to solve Ax = b
std::vector<double> solveLinearSystem(std::vector<std::vector<double>> A,
                                      std::vector<double> b) {
  int n = A.size();

  // Forward elimination
  for (int i = 0; i < n; i++) {
    // Pivot
    int maxRow = i;
    for (int k = i + 1; k < n; k++) {
      if (std::abs(A[k][i]) > std::abs(A[maxRow][i])) {
        maxRow = k;
      }
    }
    std::swap(A[i], A[maxRow]);
    std::swap(b[i], b[maxRow]);

    if (std::abs(A[i][i]) < 1e-12) {
      throw std::runtime_error("Singular matrix in linear solver.");
    }

    // Eliminate
    for (int k = i + 1; k < n; k++) {
      double factor = A[k][i] / A[i][i];
      b[k] -= factor * b[i];
      for (int j = i; j < n; j++) {
        A[k][j] -= factor * A[i][j];
      }
    }
  }

  // Back substitution
  std::vector<double> x(n);
  for (int i = n - 1; i >= 0; i--) {
    double sum = 0.0;
    for (int j = i + 1; j < n; j++) {
      sum += A[i][j] * x[j];
    }
    x[i] = (b[i] - sum) / A[i][i];
  }
  return x;
}

std::vector<double> solveSystem(const std::vector<SystemFunction> &functions,
                                const std::vector<double> &targetValues,
                                const std::vector<double> &initialGuess,
                                double tolerance, int maxIter) {
  int n = functions.size();
  if (targetValues.size() != n || initialGuess.size() != n) {
    throw std::runtime_error("Dimension mismatch in solver arguments.");
  }

  std::vector<double> x = initialGuess;
  std::vector<double> delta(n);
  std::vector<double> F(n); // Residuals F(x) - y
  std::vector<std::vector<double>> J(n, std::vector<double>(n)); // Jacobian

  for (int iter = 0; iter < maxIter; iter++) {
    // 1. Calculate Residuals F = f(x) - target
    double maxResidual = 0.0;
    for (int i = 0; i < n; i++) {
      double val = functions[i](x);
      F[i] = val - targetValues[i];
      if (std::isnan(F[i])) {
        maxResidual = std::nan("");
        break;
      }
      if (std::abs(F[i]) > maxResidual)
        maxResidual = std::abs(F[i]);
    }

    // Check convergence
    if (std::isnan(maxResidual) || maxResidual < tolerance) {
      if (std::isnan(maxResidual)) {
        throw std::runtime_error("NaN encountered in residual calculation.");
      }
      return x;
    }

    // 2. Compute Jacobian Matrix J_ij = dF_i / dx_j using finite differences
    for (int j = 0; j < n; j++) {
      double originalX = x[j];
      // Adaptive epsilon: small enough for precision, large enough to avoid noise.
      // For muB ~ 1e-10, we need a step like 1e-12 or 1e-13.
      // For munue ~ 1e-3, we need a step like 1e-7.
      double epsilon = 1e-6 * (std::abs(originalX) + 1e-9); 
      
      x[j] += epsilon;
      for (int i = 0; i < n; i++) {
        double valPlus = functions[i](x);
        J[i][j] = (valPlus - (F[i] + targetValues[i])) / epsilon;
      }
      x[j] = originalX; // restore
    }

    // 3. Solve linear system J * delta = -F
    // We use column normalization (Jacobi preconditioning) to handle different scales.
    std::vector<double> colNorms(n, 0.0);
    for (int j = 0; j < n; j++) {
      double sumSq = 0.0;
      for (int i = 0; i < n; i++) sumSq += J[i][j] * J[i][j];
      colNorms[j] = std::sqrt(sumSq);
      if (colNorms[j] < 1e-18) colNorms[j] = 1.0; // avoid division by zero
      
      // Scale column j
      for (int i = 0; i < n; i++) J[i][j] /= colNorms[j];
    }

    std::vector<double> negF(n);
    for (int i = 0; i < n; i++) negF[i] = -F[i];

    try {
      std::vector<double> delta_scaled = solveLinearSystem(J, negF);
      // Un-scale the solution: delta_j = delta_scaled_j / colNorms_j
      for (int j = 0; j < n; j++) {
        delta[j] = delta_scaled[j] / colNorms[j];
      }
    } catch (const std::exception &e) {
      throw std::runtime_error(
          std::string("Linear solver failed at iteration ") +
          std::to_string(iter) + ": " + e.what());
    }

    // 4. Update solution
    for (int i = 0; i < n; i++) {
      x[i] += delta[i];
    }
  }

  throw std::runtime_error("Newton-Raphson failed to converge.");
}

} // namespace Solver
