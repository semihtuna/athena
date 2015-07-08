// HLLE Riemann solver for relativistic magnetohydrodynamics in pure GR

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
//   bb: 3D array of normal magnetic fields
//   prim_l, prim_r: left and right primitive states
// Outputs:
//   flux: fluxes across interface
// Notes:
//   prim_l, prim_r overwritten
//   implements HLLE algorithm similar to that of fluxcalc() in step_ch.c in Harm
void FluidIntegrator::RiemannSolver(const int k, const int j, const int il,
    const int iu, const int ivx, const AthenaArray<Real> &bb, AthenaArray<Real> &prim_l,
    AthenaArray<Real> &prim_r, AthenaArray<Real> &flux)
{
  // Calculate cyclic permutations of indices
  int ivy = IVX + ((ivx-IVX)+1)%3;
  int ivz = IVX + ((ivx-IVX)+2)%3;

  // Extract ratio of specific heats
  const Real gamma_adi = pmy_fluid->pf_eos->GetGamma();

  // Get metric components
  switch (ivx)
  {
    case IVX:
      pmy_fluid->pmy_block->pcoord->Face1Metric(k, j, il, iu, g_, gi_);
      break;
    case IVY:
      pmy_fluid->pmy_block->pcoord->Face2Metric(k, j, il, iu, g_, gi_);
      break;
    case IVZ:
      pmy_fluid->pmy_block->pcoord->Face3Metric(k, j, il, iu, g_, gi_);
      break;
  }

  // Go through each interface
  #pragma simd
  for (int i = il; i <= iu; ++i)
  {
    // Extract metric
    const Real
        &g_00 = g_(I00,i), &g_01 = g_(I01,i), &g_02 = g_(I02,i), &g_03 = g_(I03,i),
        &g_10 = g_(I01,i), &g_11 = g_(I11,i), &g_12 = g_(I12,i), &g_13 = g_(I13,i),
        &g_20 = g_(I02,i), &g_21 = g_(I12,i), &g_22 = g_(I22,i), &g_23 = g_(I23,i),
        &g_30 = g_(I03,i), &g_31 = g_(I13,i), &g_32 = g_(I23,i), &g_33 = g_(I33,i);
    const Real
        &g00 = gi_(I00,i), &g01 = gi_(I01,i), &g02 = gi_(I02,i), &g03 = gi_(I03,i),
        &g10 = gi_(I01,i), &g11 = gi_(I11,i), &g12 = gi_(I12,i), &g13 = gi_(I13,i),
        &g20 = gi_(I02,i), &g21 = gi_(I12,i), &g22 = gi_(I22,i), &g23 = gi_(I23,i),
        &g30 = gi_(I03,i), &g31 = gi_(I13,i), &g32 = gi_(I23,i), &g33 = gi_(I33,i);
    Real alpha = std::sqrt(-1.0/g00);
    Real gii, g0i;
    switch (ivx)
    {
      case IVX:
        gii = g11;
        g0i = g01;
        break;
      case IVY:
        gii = g22;
        g0i = g02;
        break;
      case IVZ:
        gii = g33;
        g0i = g03;
        break;
    }

    // Extract left primitives
    const Real &rho_l = prim_l(IDN,i);
    const Real &pgas_l = prim_l(IEN,i);
    const Real &w1_l = prim_l(IVX,i);
    const Real &w2_l = prim_l(IVY,i);
    const Real &w3_l = prim_l(IVZ,i);
    Real bb1_l, bb2_l, bb3_l;
    switch (ivx)
    {
      case IVX:
        bb1_l = bb(k,j,i);
        bb2_l = prim_l(IBY,i);
        bb3_l = prim_l(IBZ,i);
        break;
      case IVY:
        bb2_l = bb(k,j,i);
        bb3_l = prim_l(IBY,i);
        bb1_l = prim_l(IBZ,i);
        break;
      case IVZ:
        bb3_l = bb(k,j,i);
        bb1_l = prim_l(IBY,i);
        bb2_l = prim_l(IBZ,i);
        break;
    }

    // Extract right primitives
    const Real &rho_r = prim_r(IDN,i);
    const Real &pgas_r = prim_r(IEN,i);
    const Real &w1_r = prim_r(IVX,i);
    const Real &w2_r = prim_r(IVY,i);
    const Real &w3_r = prim_r(IVZ,i);
    Real bb1_r, bb2_r, bb3_r;
    switch (ivx)
    {
      case IVX:
        bb1_r = bb(k,j,i);
        bb2_r = prim_r(IBY,i);
        bb3_r = prim_r(IBZ,i);
        break;
      case IVY:
        bb2_r = bb(k,j,i);
        bb3_r = prim_r(IBY,i);
        bb1_r = prim_r(IBZ,i);
        break;
      case IVZ:
        bb3_r = bb(k,j,i);
        bb1_r = prim_r(IBY,i);
        bb2_r = prim_r(IBZ,i);
        break;
    }

    // Calculate 4-velocity in left state
    Real ucon_l[4], ucov_l[4];
    Real tmp = g_11*SQR(w1_l) + 2.0*g_12*w1_l*w2_l + 2.0*g_13*w1_l*w3_l
             + g_22*SQR(w2_l) + 2.0*g_23*w2_l*w3_l
             + g_33*SQR(w3_l);
    Real gamma_l = std::sqrt(1.0 + tmp);
    ucon_l[0] = gamma_l / alpha;
    ucon_l[1] = w1_l - alpha * gamma_l * g01;
    ucon_l[2] = w2_l - alpha * gamma_l * g02;
    ucon_l[3] = w3_l - alpha * gamma_l * g03;
    ucov_l[0] = g_00*ucon_l[0] + g_01*ucon_l[1] + g_02*ucon_l[2] + g_03*ucon_l[3];
    ucov_l[1] = g_10*ucon_l[0] + g_11*ucon_l[1] + g_12*ucon_l[2] + g_13*ucon_l[3];
    ucov_l[2] = g_20*ucon_l[0] + g_21*ucon_l[1] + g_22*ucon_l[2] + g_23*ucon_l[3];
    ucov_l[3] = g_30*ucon_l[0] + g_31*ucon_l[1] + g_32*ucon_l[2] + g_33*ucon_l[3];

    // Calculate 4-velocity in right state
    Real ucon_r[4], ucov_r[4];
    tmp = g_11*SQR(w1_r) + 2.0*g_12*w1_r*w2_r + 2.0*g_13*w1_r*w3_r
        + g_22*SQR(w2_r) + 2.0*g_23*w2_r*w3_r
        + g_33*SQR(w3_r);
    Real gamma_r = std::sqrt(1.0 + tmp);
    ucon_r[0] = gamma_r / alpha;
    ucon_r[1] = w1_r - alpha * gamma_r * g01;
    ucon_r[2] = w2_r - alpha * gamma_r * g02;
    ucon_r[3] = w3_r - alpha * gamma_r * g03;
    ucov_r[0] = g_00*ucon_r[0] + g_01*ucon_r[1] + g_02*ucon_r[2] + g_03*ucon_r[3];
    ucov_r[1] = g_10*ucon_r[0] + g_11*ucon_r[1] + g_12*ucon_r[2] + g_13*ucon_r[3];
    ucov_r[2] = g_20*ucon_r[0] + g_21*ucon_r[1] + g_22*ucon_r[2] + g_23*ucon_r[3];
    ucov_r[3] = g_30*ucon_r[0] + g_31*ucon_r[1] + g_32*ucon_r[2] + g_33*ucon_r[3];

    // Calculate 4-magnetic field in left state
    Real bcon_l[4], bcov_l[4];
    bcon_l[0] = ucon_l[0] * (g_01*bb1_l + g_02*bb2_l + g_03*bb3_l)
              + ucon_l[1] * (g_11*bb1_l + g_12*bb2_l + g_13*bb3_l)
              + ucon_l[2] * (g_21*bb1_l + g_22*bb2_l + g_23*bb3_l)
              + ucon_l[3] * (g_31*bb1_l + g_32*bb2_l + g_33*bb3_l);
    bcon_l[1] = (bb1_l + bcon_l[0] * ucon_l[1]) / ucon_l[0];
    bcon_l[2] = (bb2_l + bcon_l[0] * ucon_l[2]) / ucon_l[0];
    bcon_l[3] = (bb3_l + bcon_l[0] * ucon_l[3]) / ucon_l[0];
    bcov_l[0] = g_00*bcon_l[0] + g_01*bcon_l[1] + g_02*bcon_l[2] + g_03*bcon_l[3];
    bcov_l[1] = g_10*bcon_l[0] + g_11*bcon_l[1] + g_12*bcon_l[2] + g_13*bcon_l[3];
    bcov_l[2] = g_20*bcon_l[0] + g_21*bcon_l[1] + g_22*bcon_l[2] + g_23*bcon_l[3];
    bcov_l[3] = g_30*bcon_l[0] + g_31*bcon_l[1] + g_32*bcon_l[2] + g_33*bcon_l[3];
    Real b_sq_l = bcon_l[0]*bcov_l[0] + bcon_l[1]*bcov_l[1] + bcon_l[2]*bcov_l[2]
        + bcon_l[3]*bcov_l[3];

    // Calculate 4-magnetic field in right state
    Real bcon_r[4], bcov_r[4];
    bcon_r[0] = ucon_r[0] * (g_01*bb1_r + g_02*bb2_r + g_03*bb3_r)
              + ucon_r[1] * (g_11*bb1_r + g_12*bb2_r + g_13*bb3_r)
              + ucon_r[2] * (g_21*bb1_r + g_22*bb2_r + g_23*bb3_r)
              + ucon_r[3] * (g_31*bb1_r + g_32*bb2_r + g_33*bb3_r);
    bcon_r[1] = (bb1_r + bcon_r[0] * ucon_r[1]) / ucon_r[0];
    bcon_r[2] = (bb2_r + bcon_r[0] * ucon_r[2]) / ucon_r[0];
    bcon_r[3] = (bb3_r + bcon_r[0] * ucon_r[3]) / ucon_r[0];
    bcov_r[0] = g_00*bcon_r[0] + g_01*bcon_r[1] + g_02*bcon_r[2] + g_03*bcon_r[3];
    bcov_r[1] = g_10*bcon_r[0] + g_11*bcon_r[1] + g_12*bcon_r[2] + g_13*bcon_r[3];
    bcov_r[2] = g_20*bcon_r[0] + g_21*bcon_r[1] + g_22*bcon_r[2] + g_23*bcon_r[3];
    bcov_r[3] = g_30*bcon_r[0] + g_31*bcon_r[1] + g_32*bcon_r[2] + g_33*bcon_r[3];
    Real b_sq_r = bcon_r[0]*bcov_r[0] + bcon_r[1]*bcov_r[1] + bcon_r[2]*bcov_r[2]
        + bcon_r[3]*bcov_r[3];

    // Calculate wavespeeds in left state
    Real lambda_p_l, lambda_m_l;
    Real wgas_l = rho_l + gamma_adi/(gamma_adi-1.0) * pgas_l;
    pmy_fluid->pf_eos->FastMagnetosonicSpeedsGR(wgas_l, pgas_l, ucon_l[0], ucon_l[ivx],
        b_sq_l, g00, g0i, gii, &lambda_p_l, &lambda_m_l);

    // Calculate wavespeeds in right state
    Real lambda_p_r, lambda_m_r;
    Real wgas_r = rho_r + gamma_adi/(gamma_adi-1.0) * pgas_r;
    pmy_fluid->pf_eos->FastMagnetosonicSpeedsGR(wgas_r, pgas_r, ucon_r[0], ucon_r[ivx],
        b_sq_r, g00, g0i, gii, &lambda_p_r, &lambda_m_r);

    // Calculate extremal wavespeed
    Real lambda_l = std::min(lambda_m_l, lambda_m_r);
    Real lambda_r = std::max(lambda_p_l, lambda_p_r);
    Real lambda = std::max(lambda_r, -lambda_l);

    // Calculate conserved quantities in L region
    // (rho u^0, T^0_\mu, and B^j = *F^{j0}, where j != ivx)
    Real cons_l[NWAVE];
    Real wtot_l = wgas_l + b_sq_l;
    Real ptot_l = pgas_l + 0.5*b_sq_l;
    cons_l[IDN] = rho_l * ucon_l[0];
    cons_l[IEN] = wtot_l * ucon_l[0] * ucov_l[0] - bcon_l[0] * bcov_l[0] + ptot_l;
    cons_l[IVX] = wtot_l * ucon_l[0] * ucov_l[1] - bcon_l[0] * bcov_l[1];
    cons_l[IVY] = wtot_l * ucon_l[0] * ucov_l[2] - bcon_l[0] * bcov_l[2];
    cons_l[IVZ] = wtot_l * ucon_l[0] * ucov_l[3] - bcon_l[0] * bcov_l[3];
    cons_l[IBY] = bcon_l[ivy] * ucon_l[0] - bcon_l[0] * ucon_l[ivy];
    cons_l[IBZ] = bcon_l[ivz] * ucon_l[0] - bcon_l[0] * ucon_l[ivz];

    // Calculate fluxes in L region
    // (rho u^i, T^i_\mu, and *F^{ji}, where i = ivx and j != ivx)
    Real flux_l[NWAVE];
    flux_l[IDN] = rho_l * ucon_l[ivx];
    flux_l[IEN] = wtot_l * ucon_l[ivx] * ucov_l[0] - bcon_l[ivx] * bcov_l[0];
    flux_l[IVX] = wtot_l * ucon_l[ivx] * ucov_l[1] - bcon_l[ivx] * bcov_l[1];
    flux_l[IVY] = wtot_l * ucon_l[ivx] * ucov_l[2] - bcon_l[ivx] * bcov_l[2];
    flux_l[IVZ] = wtot_l * ucon_l[ivx] * ucov_l[3] - bcon_l[ivx] * bcov_l[3];
    flux_l[ivx] += ptot_l;
    flux_l[IBY] = bcon_l[ivy] * ucon_l[ivx] - bcon_l[ivx] * ucon_l[ivy];
    flux_l[IBZ] = bcon_l[ivz] * ucon_l[ivx] - bcon_l[ivx] * ucon_l[ivz];

    // Calculate conserved quantities in R region
    // (rho u^0, T^0_\mu, and B^j = *F^{j0}, where j != ivx)
    Real cons_r[NWAVE];
    Real wtot_r = wgas_r + b_sq_r;
    Real ptot_r = pgas_r + 0.5*b_sq_r;
    cons_r[IDN] = rho_r * ucon_r[0];
    cons_r[IEN] = wtot_r * ucon_r[0] * ucov_r[0] - bcon_r[0] * bcov_r[0] + ptot_r;
    cons_r[IVX] = wtot_r * ucon_r[0] * ucov_r[1] - bcon_r[0] * bcov_r[1];
    cons_r[IVY] = wtot_r * ucon_r[0] * ucov_r[2] - bcon_r[0] * bcov_r[2];
    cons_r[IVZ] = wtot_r * ucon_r[0] * ucov_r[3] - bcon_r[0] * bcov_r[3];
    cons_r[IBY] = bcon_r[ivy] * ucon_r[0] - bcon_r[0] * ucon_r[ivy];
    cons_r[IBZ] = bcon_r[ivz] * ucon_r[0] - bcon_r[0] * ucon_r[ivz];

    // Calculate fluxes in R region
    // (rho u^i, T^i_\mu, and *F^{ji}, where i = ivx and j != ivx)
    Real flux_r[NWAVE];
    flux_r[IDN] = rho_r * ucon_r[ivx];
    flux_r[IEN] = wtot_r * ucon_r[ivx] * ucov_r[0] - bcon_r[ivx] * bcov_r[0];
    flux_r[IVX] = wtot_r * ucon_r[ivx] * ucov_r[1] - bcon_r[ivx] * bcov_r[1];
    flux_r[IVY] = wtot_r * ucon_r[ivx] * ucov_r[2] - bcon_r[ivx] * bcov_r[2];
    flux_r[IVZ] = wtot_r * ucon_r[ivx] * ucov_r[3] - bcon_r[ivx] * bcov_r[3];
    flux_r[ivx] += ptot_r;
    flux_r[IBY] = bcon_r[ivy] * ucon_r[ivx] - bcon_r[ivx] * ucon_r[ivy];
    flux_r[IBZ] = bcon_r[ivz] * ucon_r[ivx] - bcon_r[ivx] * ucon_r[ivz];

    // Calculate fluxes in HLL region
    Real flux_hll[NWAVE];
    for (int n = 0; n < NWAVE; ++n)
      flux_hll[n] = (lambda_r*flux_l[n] - lambda_l*flux_r[n]
          + lambda_r*lambda_l * (cons_r[n] - cons_l[n]))
          / (lambda_r-lambda_l);

    // Set fluxes
    for (int n = 0; n < NWAVE; ++n)
    {
      if (lambda_l >= 0.0)  // L region
        flux(n,i) = flux_l[n];
      else if (lambda_right <= 0.0)  // R region
        flux(n,i) = flux_r[n];
      else  // HLL region
        flux(n,i) = flux_hll[n];
    }
  }
  return;
}
