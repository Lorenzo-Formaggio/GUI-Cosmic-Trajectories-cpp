import matplotlib.pyplot as plt
import numpy as np
import re

temps = []
muB = []
muQ = []
nS = []
nLambda = []
nNucleon = []

with open('build/output.txt', 'r') as f:
    for line in f:
        m = re.match(r'\s*([0-9\.]+)\s+([0-9\.eE\+\-]+)\s+([0-9\.eE\+\-]+)\s+([0-9\.eE\+\-]+)\s+([0-9\.eE\+\-]+)\s+([0-9\.eE\+\-]+)\s*', line)
        if m:
            t = float(m.group(1))
            if t <= 35: continue # Ignore non-converged points below 40 MeV
            temps.append(t)
            muB.append(float(m.group(2)))
            muQ.append(float(m.group(3)))
            nS.append(float(m.group(4)))
            nLambda.append(float(m.group(5)))
            nNucleon.append(float(m.group(6)))

# Plot 1: Normalized Densities vs Temperature
fig, ax1 = plt.subplots(figsize=(8, 5))
ax1.plot(temps, nS, label='Sexaquark ($n_S/n_B$)', color='blue', linewidth=2)
ax1.plot(temps, nLambda, label=r'$\Lambda^0$ ($n_\Lambda/n_B$)', color='orange', linewidth=2)
ax1.plot(temps, nNucleon, label='Nucleons ($n_N/n_B$)', color='green', linewidth=2)

ax1.set_yscale('log')
ax1.set_xlabel('Temperature $T$ (MeV)', fontsize=13)
ax1.set_ylabel('Normalized Density $n_i / n_B$', fontsize=13)
ax1.set_title('Normalized Hadron Densities vs Temperature', fontsize=14)
ax1.grid(True, which="both", ls="--", alpha=0.5)
ax1.legend(fontsize=11)
ax1.invert_xaxis()
plt.tight_layout()
plt.savefig('/Users/lorenzoformaggio/.gemini/antigravity-ide/brain/ba62263a-204f-4e49-a1e9-2845fc1c19e3/densities_plot.png', dpi=300)
plt.close()

# Plot 2: Trajectory in (muB, T) space
fig, ax2 = plt.subplots(figsize=(8, 5))
ax2.plot(muB, temps, color='crimson', linewidth=2.5, marker='o', markersize=4, label=r'Trajectory ($l_e=l_\mu=l_\tau=-10^{-10}$)')
ax2.set_xlabel(r'Baryon Chemical Potential $\mu_B$ (MeV)', fontsize=13)
ax2.set_ylabel(r'Temperature $T$ (MeV)', fontsize=13)
ax2.set_title(r'Cosmic Trajectory in $(T, \mu_B)$ Phase Space', fontsize=14)
ax2.set_xscale('log')
ax2.grid(True, which="both", ls="--", alpha=0.5)
ax2.legend(fontsize=11)
plt.tight_layout()
plt.savefig('/Users/lorenzoformaggio/.gemini/antigravity-ide/brain/ba62263a-204f-4e49-a1e9-2845fc1c19e3/trajectory_plot.png', dpi=300)
plt.close()
