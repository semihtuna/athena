//======================================================================================
// Athena++ astrophysical MHD code
// Copyright (C) 2014 James M. Stone  <jmstone@princeton.edu>
//
// This program is free software: you can redistribute and/or modify it under the terms
// of the GNU General Public License (GPL) as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
//
// You should have received a copy of GNU GPL in the file LICENSE included in the code
// distribution.  If not see <http://www.gnu.org/licenses/>.
//======================================================================================
//! \file blast.cpp
//  \brief Problem generator for spherical blast wave problem.
//
// REFERENCE: P. Londrillo & L. Del Zanna, "High-order upwind schemes for 
//   multidimensional MHD", ApJ, 530, 508 (2000), and references therein.
//======================================================================================

// Athena++ headers
#include "../athena.hpp"
#include "../athena_arrays.hpp"
#include "../parameter_input.hpp"
#include "../mesh.hpp"
#include "../hydro/hydro.hpp"
#include "../field/field.hpp"
#include "../hydro/eos/eos.hpp"
#include "../coordinates/coordinates.hpp"

//======================================================================================
//! \fn ProblemGenerator
//  \brief Spherical blast wave test problem generator
//======================================================================================

void Mesh::ProblemGenerator(Hydro *phyd, Field *pfld, ParameterInput *pin)
{
  MeshBlock *pmb = phyd->pmy_block;
  int is = pmb->is; int js = pmb->js; int ks = pmb->ks;
  int ie = pmb->ie; int je = pmb->je; int ke = pmb->ke;

  Real rin = pin->GetReal("problem","radius");
  Real pa  = pin->GetReal("problem","pamb");
  Real da  = pin->GetOrAddReal("problem","damb",1.0);
  Real drat = pin->GetOrAddReal("problem","drat",1.0);
  Real prat = pin->GetReal("problem","prat");
  Real b0,theta;
  if (MAGNETIC_FIELDS_ENABLED) {
    b0 = pin->GetReal("problem","b0");
    theta = (PI/180.0)*pin->GetReal("problem","angle");
  }
  Real gamma = phyd->pf_eos->GetGamma();
  Real gm1 = gamma - 1.0;

// setup uniform ambient medium with spherical over-pressured region

  for (int k=ks; k<=ke; k++) {
  for (int j=js; j<=je; j++) {
  for (int i=is; i<=ie; i++) {
    Real rad = sqrt(SQR(pmb->pcoord->x1v(i)) + SQR(pmb->pcoord->x2v(j)) + SQR(pmb->pcoord->x3v(k)));
    Real den = da;
    if (rad < rin) den = drat*da;

    phyd->u(IDN,k,j,i) = den;
    phyd->u(IM1,k,j,i) = 0.0;
    phyd->u(IM2,k,j,i) = 0.0;
    phyd->u(IM3,k,j,i) = 0.0;
    if (NON_BAROTROPIC_EOS) {
      Real pres = pa;
      if (rad < rin) pres = prat*pa;
      phyd->u(IEN,k,j,i) = pres/gm1;
      if (RELATIVISTIC_DYNAMICS)  // this should only ever be SR with this file
        phyd->u(IEN,k,j,i) += den;
    }
  }}}

// initialize interface B and total energy

  if (MAGNETIC_FIELDS_ENABLED) {
    for (int k=ks; k<=ke; k++) {
    for (int j=js; j<=je; j++) {
    for (int i=is; i<=ie+1; i++) {
      pfld->b.x1f(k,j,i) = b0*cos(theta);
    }}}
    for (int k=ks; k<=ke; k++) {
    for (int j=js; j<=je+1; j++) {
    for (int i=is; i<=ie; i++) {
      pfld->b.x2f(k,j,i) = b0*sin(theta);
    }}}
    for (int k=ks; k<=ke+1; k++) {
    for (int j=js; j<=je; j++) {
    for (int i=is; i<=ie; i++) {
      pfld->b.x3f(k,j,i) = 0.0;
    }}}
    for (int k=ks; k<=ke; k++) {
    for (int j=js; j<=je; j++) {
    for (int i=is; i<=ie+1; i++) {
      phyd->u(IEN,k,j,i) += 0.5*b0*b0;
    }}}
  }

}
