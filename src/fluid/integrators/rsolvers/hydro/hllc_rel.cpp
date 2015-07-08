// HLLC Riemann solver for relativistic hydrodynamics

// Primary header
#include "../../fluid_integrator.hpp"

// C++ headers
#include <algorithm>  // max(), min()
#include <cmath>      // sqrt()

// Athena headers
#include "../../../fluid.hpp"                       // Fluid
#include "../../../eos/eos.hpp"                     // FluidEqnOfState
#include "../../../../athena.hpp"                   // enums, macros, Real
#include "../../../../athena_arrays.hpp"            // AthenaArray
#include "../../../../mesh.hpp"                     // MeshBlock
#include "../../../../coordinates/coordinates.hpp"  // Coordinates

// Riemann solver
// Inputs:
//   k,j: x3- and x2-indices
//   il,iu: lower and upper x1-indices
//   ivx: type of interface (IVX for x1, IVY for x2, IVZ for x3)
//   bb: 3D array of normal magnetic fields (not used)
//   prim_l, prim_r: left and right primitive states
// Outputs:
//   flux: fluxes across interface
// Notes:
//   prim_l, prim_r overwritten
//   implements HLLC algorithm from Mignone & Bodo 2005, MNRAS 364 126 (MB)
void FluidIntegrator::RiemannSolver(const int k, const int j, const int il,
    const int iu, const int ivx, const AthenaArray<Real> &bb, AthenaArray<Real> &prim_l,
    AthenaArray<Real> &prim_r, AthenaArray<Real> &flux)
{
  // Transform primitives to locally flat coordinates if in GR
  if (GENERAL_RELATIVITY)
    switch (ivx)
    {
      case IVX:
        pmy_fluid->pmy_block->pcoord->PrimToLocal1(k, j, il, iu, bb, prim_l, prim_r,
            bb_normal_);
        break;
      case IVY:
        pmy_fluid->pmy_block->pcoord->PrimToLocal2(k, j, il, iu, bb, prim_l, prim_r,
            bb_normal_);
        break;
      case IVZ:
        pmy_fluid->pmy_block->pcoord->PrimToLocal3(k, j, il, iu, bb, prim_l, prim_r,
            bb_normal_);
        break;
    }

  // Calculate cyclic permutations of indices
  int ivy = IVX + ((ivx-IVX)+1)%3;
  int ivz = IVX + ((ivx-IVX)+2)%3;

  // Extract ratio of specific heats
  const Real gamma_adi = pmy_fluid->pf_eos->GetGamma();

  // Go through each interface
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    // Extract left primitives
    const Real &rho_l = prim_l(IDN,i);
    const Real &pgas_l = prim_l(IEN,i);
    Real u_l[4];
    if (GENERAL_RELATIVITY)
    {
      u_l[1] = prim_l(ivx,i);
      u_l[2] = prim_l(ivy,i);
      u_l[3] = prim_l(ivz,i);
      u_l[0] = std::sqrt(1.0 + SQR(u_l[1]) + SQR(u_l[2]) + SQR(u_l[3]));
    }
    else  // SR
    {
      const Real &vx_l = prim_l(ivx,i);
      const Real &vy_l = prim_l(ivy,i);
      const Real &vz_l = prim_l(ivz,i);
      u_l[0] = std::sqrt(1.0 / (1.0 - SQR(vx_l) - SQR(vy_l) - SQR(vz_l)));
      u_l[1] = u_l[0] * vx_l;
      u_l[2] = u_l[0] * vy_l;
      u_l[3] = u_l[0] * vz_l;
    }

    // Extract right primitives
    const Real &rho_r = prim_r(IDN,i);
    const Real &pgas_r = prim_r(IEN,i);
    Real u_r[4];
    if (GENERAL_RELATIVITY)
    {
      u_r[1] = prim_r(ivx,i);
      u_r[2] = prim_r(ivy,i);
      u_r[3] = prim_r(ivz,i);
      u_r[0] = std::sqrt(1.0 + SQR(u_r[1]) + SQR(u_r[2]) + SQR(u_r[3]));
    }
    else  // special relativity
    {
      const Real &vx_r = prim_r(ivx,i);
      const Real &vy_r = prim_r(ivy,i);
      const Real &vz_r = prim_r(ivz,i);
      u_r[0] = std::sqrt(1.0 / (1.0 - SQR(vx_r) - SQR(vy_r) - SQR(vz_r)));
      u_r[1] = u_r[0] * vx_r;
      u_r[2] = u_r[0] * vy_r;
      u_r[3] = u_r[0] * vz_r;
    }

    // Calculate wavespeeds in left state (MB 23)
    Real lambda_p_l, lambda_m_l;
    Real wgas_l = rho_l + gamma_adi/(gamma_adi-1.0) * pgas_l;
    pmy_fluid->pf_eos->SoundSpeedsSR(wgas_l, pgas_l, u_l[1]/u_l[0], SQR(u_l[0]),
        &lambda_p_l, &lambda_m_l);

    // Calculate wavespeeds in right state (MB 23)
    Real lambda_p_r, lambda_m_r;
    Real wgas_r = rho_r + gamma_adi/(gamma_adi-1.0) * pgas_r;
    pmy_fluid->pf_eos->SoundSpeedsSR(wgas_r, pgas_r, u_r[1]/u_r[0], SQR(u_r[0]),
        &lambda_p_r, &lambda_m_r);

    // Calculate extremal wavespeeds
    Real lambda_l = std::min(lambda_m_l, lambda_m_r);
    Real lambda_r = std::max(lambda_p_l, lambda_p_r);

    // Calculate conserved quantities in L region (MB 3)
    Real cons_l[NWAVE];
    cons_l[IDN] = rho_l * u_l[0];
    cons_l[IEN] = wgas_l * u_l[0] * u_l[0] - pgas_l;
    cons_l[ivx] = wgas_l * u_l[1] * u_l[0];
    cons_l[ivy] = wgas_l * u_l[2] * u_l[0];
    cons_l[ivz] = wgas_l * u_l[3] * u_l[0];

    // Calculate fluxes in L region (MB 2,3)
    Real flux_l[NWAVE];
    flux_l[IDN] = rho_l * u_l[1];
    flux_l[IEN] = wgas_l * u_l[0] * u_l[1];
    flux_l[ivx] = wgas_l * u_l[1] * u_l[1] + pgas_l;
    flux_l[ivy] = wgas_l * u_l[2] * u_l[1];
    flux_l[ivz] = wgas_l * u_l[3] * u_l[1];

    // Calculate conserved quantities in R region (MB 3)
    Real cons_r[NWAVE];
    cons_r[IDN] = rho_r * u_r[0];
    cons_r[IEN] = wgas_r * u_r[0] * u_r[0] - pgas_r;
    cons_r[ivx] = wgas_r * u_r[1] * u_r[0];
    cons_r[ivy] = wgas_r * u_r[2] * u_r[0];
    cons_r[ivz] = wgas_r * u_r[3] * u_r[0];

    // Calculate fluxes in R region (MB 2,3)
    Real flux_r[NWAVE];
    flux_r[IDN] = rho_r * u_r[1];
    flux_r[IEN] = wgas_r * u_r[0] * u_r[1];
    flux_r[ivx] = wgas_r * u_r[1] * u_r[1] + pgas_r;
    flux_r[ivy] = wgas_r * u_r[2] * u_r[1];
    flux_r[ivz] = wgas_r * u_r[3] * u_r[1];

    // Calculate conserved quantities in HLL region (MB 9)
    Real e_hll = (lambda_r*cons_r[IEN] - lambda_l*cons_l[IEN]
        + flux_l[IEN] - flux_r[IEN]) / (lambda_r-lambda_l);
    Real mx_hll = (lambda_r*cons_r[ivx] - lambda_l*cons_l[ivx]
        + flux_l[ivx] - flux_r[ivx]) / (lambda_r-lambda_l);

    // Calculate fluxes in HLL region (MB 11)
    Real flux_e_hll = (lambda_r*flux_l[IEN] - lambda_l*flux_r[IEN]
        + lambda_r*lambda_l * (cons_r[IEN]-cons_l[IEN]))
        / (lambda_r-lambda_l);
    Real flux_mx_hll = (lambda_r*flux_l[ivx] - lambda_l*flux_r[ivx]
        + lambda_r*lambda_l * (cons_r[ivx]-cons_l[ivx]))
        / (lambda_r-lambda_l);

    // Calculate contact wavespeed (MB 18)
    Real lambda_star;
    if (flux_e_hll > TINY_NUMBER || flux_e_hll < -TINY_NUMBER)  // use quadratic formula
    {
      // Follows algorithm in Numerical Recipes (section 5.6) for avoiding cancellations
      Real a = flux_e_hll;
      Real b = -(e_hll + flux_mx_hll);
      Real c = mx_hll;
      Real q = -0.5 * (b - std::sqrt(SQR(b) - 4.0*a*c));
      lambda_star = c / q;
    }
    else  // no quadratic term
      lambda_star = mx_hll / (e_hll + flux_mx_hll);

    // Calculate contact pressure (MB 17)
    Real vx_l = u_l[1] / u_l[0];
    Real a_l = lambda_l * cons_l[IEN] - cons_l[ivx];
    Real b_l = cons_l[ivx] * (lambda_l - vx_l) - pgas_l;
    Real pgas_lstar = (a_l * lambda_star - b_l) / (1.0 - lambda_l * lambda_star);
    Real vx_r = u_r[1] / u_r[0];
    Real a_r = lambda_r * cons_r[IEN] - cons_r[ivx];
    Real b_r = cons_r[ivx] * (lambda_r - vx_r) - pgas_r;
    Real pgas_rstar = (a_r * lambda_star - b_r) / (1.0 - lambda_r * lambda_star);
    Real pgas_star = 0.5 * (pgas_lstar + pgas_rstar);

    // Calculate conserved quantities in L* region (MB 16)
    for (int n = 0; n < NWAVE; ++n)
      cons_lstar[n] = cons_l[n] * (lambda_l-vx_l);
    cons_lstar[IEN] += pgas_star*lambda_star - pgas_l*vx_l;
    cons_lstar[ivx] += pgas_star - pgas_l;
    for (int n = 0; n < NWAVE; ++n)
      cons_lstar[n] /= lambda_l - lambda_star;

    // Calculate fluxes in L* region (MB 14)
    Real flux_lstar[NWAVE];
    for (int n = 0; n < NWAVE; ++n)
      flux_lstar[n] = flux_l[n] + lambda_l * (cons_lstar[n] - cons_l[n]);

    // Calculate conserved quantities in R* region (MB 16)
    Real cons_rstar[NWAVE];
    for (int n = 0; n < NWAVE; ++n)
      cons_rstar[n] = cons_r[n] * (lambda_r-vx_r);
    cons_rstar[IEN] += pgas_star*lambda_star - pgas_r*vx_r;
    cons_rstar[ivx] += pgas_star - pgas_r;
    for (int n = 0; n < NWAVE; ++n)
      cons_rstar[n] /= lambda_r - lambda_star;

    // Calculate fluxes in R* region (MB 14)
    Real flux_rstar[NWAVE];
    for (int n = 0; n < NWAVE; ++n)
      flux_rstar[n] = flux_r[n] + lambda_r * (cons_rstar[n] - cons_r[n]);

    // Set fluxes
    for (int n = 0; n < NWAVE; ++n)
    {
      if (lambda_l >= 0.0)  // L region
        flux(n,i) = flux_l[n];
      else if (lambda_r <= 0.0)  // R region
        flux(n,i) = flux_r[n];
      else if (lambda_star >= 0.0)  // L* region
        flux(n,i) = flux_lstar[n];
      else  // R* region
        flux(n,i) = flux_rstar[n];
    }

    // Set conserved quantities in GR
    if (GENERAL_RELATIVITY)
      for (int n = 0; n < NWAVE; ++n)
      {
        if (lambda_l >= 0.0)  // L region
          cons_(n,i) = cons_l[n];
        else if (lambda_r <= 0.0)  // R region
          cons_(n,i) = cons_r[n];
        else if (lambda_star >= 0.0)  // L* region
          cons_(n,i) = cons_lstar[n];
        else  // R* region
          cons_(n,i) = cons_rstar[n];
      }
  }

  // Transform fluxes to global coordinates if in GR
  if (GENERAL_RELATIVITY)
    switch (ivx)
    {
      case IVX:
        pmy_fluid->pmy_block->pcoord->FluxToGlobal1(k, j, il, iu, cons_, bb_normal_,
            flux);
        break;
      case IVY:
        pmy_fluid->pmy_block->pcoord->FluxToGlobal2(k, j, il, iu, cons_, bb_normal_,
            flux);
        break;
      case IVZ:
        pmy_fluid->pmy_block->pcoord->FluxToGlobal3(k, j, il, iu, cons_, bb_normal_,
            flux);
        break;
    }
  return;
}
