#ifndef BVALS_FC_BVALS_FC_HPP_
#define BVALS_FC_BVALS_FC_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals_fc.hpp
//  \brief handle boundaries for any FaceField type variable that represents a physical
//         quantity indexed along / located around face-centers of cells

// C headers

// C++ headers

// Athena++ classes headers
#include "../../athena.hpp"
#include "../../athena_arrays.hpp"
#include "../bvals.hpp"
#include "../bvals_interfaces.hpp"

// MPI headers
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

//----------------------------------------------------------------------------------------
//! \class FaceCenteredBoundaryVariable
//  \brief

class FaceCenteredBoundaryVariable : public BoundaryVariable {
 public:
  FaceCenteredBoundaryVariable(MeshBlock *pmb, FaceField *var, FaceField &coarse_buf,
                               EdgeField &var_flux);
  ~FaceCenteredBoundaryVariable();

  FaceField *var_fc;

  // unlke Hydro cons vs. prim, never need to rebind FaceCentered coarse_buf, so it can be
  // a reference member: ---> must be initialized in initializer list; cannot pass nullptr
  FaceField &coarse_buf;

  AthenaArray<Real> e1;
  AthenaArray<Real> e2;
  AthenaArray<Real> e3;

  // maximum number of reserved unique "physics ID" component of MPI tag bitfield
  // must correspond to the # of "int *phys_id_" private members, below. Convert to array?
  static constexpr int max_phys_id = 5;

  // BoundaryVariable:
  int ComputeVariableBufferSize(const NeighborIndexes& ni, int cng) override;
  int ComputeFluxCorrectionBufferSize(const NeighborIndexes& ni, int cng) override;

  // BoundaryCommunication:
  void SetupPersistentMPI() override;
  void StartReceiving(BoundaryCommSubset phase) override;
  void ClearBoundary(BoundaryCommSubset phase) override;
  void StartReceivingShear(BoundaryCommSubset phase) override;

  // BoundaryBuffer:
  void ReceiveAndSetBoundariesWithWait() override;
  void SetBoundaries() override;
  void SendFluxCorrection() override;
  bool ReceiveFluxCorrection() override;

  // Shearing box Field
  void SendShearingBoxBoundaryBuffers();
  bool ReceiveShearingBoxBoundaryBuffers();

  // Shearing box EMF
  void SendEMFShearingBoxBoundaryCorrection();
  bool ReceiveEMFShearingBoxBoundaryCorrection();
  void RemapEMFShearingBoxBoundary();

  // BoundaryPhysics:
  void ReflectInnerX1(Real time, Real dt,
                      int il, int jl, int ju, int kl, int ku, int ngh) override;
  void ReflectOuterX1(Real time, Real dt,
                      int iu, int jl, int ju, int kl, int ku, int ngh) override;
  void ReflectInnerX2(Real time, Real dt,
                      int il, int iu, int jl, int kl, int ku, int ngh) override;
  void ReflectOuterX2(Real time, Real dt,
                      int il, int iu, int ju, int kl, int ku, int ngh) override;
  void ReflectInnerX3(Real time, Real dt,
                      int il, int iu, int jl, int ju, int kl, int ngh) override;
  void ReflectOuterX3(Real time, Real dt,
                      int il, int iu, int jl, int ju, int ku, int ngh) override;

  void OutflowInnerX1(Real time, Real dt,
                      int il, int jl, int ju, int kl, int ku, int ngh) override;
  void OutflowOuterX1(Real time, Real dt,
                      int iu, int jl, int ju, int kl, int ku, int ngh) override;
  void OutflowInnerX2(Real time, Real dt,
                      int il, int iu, int jl, int kl, int ku, int ngh) override;
  void OutflowOuterX2(Real time, Real dt,
                      int il, int iu, int ju, int kl, int ku, int ngh) override;
  void OutflowInnerX3(Real time, Real dt,
                      int il, int iu, int jl, int ju, int kl, int ngh) override;
  void OutflowOuterX3(Real time, Real dt,
                      int il, int iu, int jl, int ju, int ku, int ngh) override;

  void PolarWedgeInnerX2(Real time, Real dt,
                         int il, int iu, int jl, int kl, int ku, int ngh) override;
  void PolarWedgeOuterX2(Real time, Real dt,
                         int il, int iu, int ju, int kl, int ku, int ngh) override;
  //protected:

 private:
  BoundaryStatus *flux_north_flag_;
  BoundaryStatus *flux_south_flag_;
  Real **flux_north_send_, **flux_north_recv_;
  Real **flux_south_send_, **flux_south_recv_;

  const bool *flip_across_pole_;

  bool edge_flag_[12];
  int nedge_fine_[12];

  // variable switch used in 2x functions, ReceiveFluxCorrection() and StartReceiving():
  // ready to recv flux from same level and apply correction? false= 2nd pass for fine lvl
  bool recv_flx_same_lvl_;

#ifdef MPI_PARALLEL
  int fc_phys_id_, fc_flx_phys_id_, fc_flx_pole_phys_id_;
  MPI_Request *req_flux_north_send_, *req_flux_north_recv_;
  MPI_Request *req_flux_south_send_, *req_flux_south_recv_;
#endif

  // BoundaryBuffer:
  int LoadBoundaryBufferSameLevel(Real *buf, const NeighborBlock& nb) override;
  void SetBoundarySameLevel(Real *buf, const NeighborBlock& nb) override;
  int LoadBoundaryBufferToCoarser(Real *buf, const NeighborBlock& nb) override;
  int LoadBoundaryBufferToFiner(Real *buf, const NeighborBlock& nb) override;
  void SetBoundaryFromCoarser(Real *buf, const NeighborBlock& nb) override;
  void SetBoundaryFromFiner(Real *buf, const NeighborBlock& nb) override;
  void PolarBoundarySingleAzimuthalBlock() override;

  // Face-centered/Field/EMF unique class methods:

  // called in SetBoundaries() and ReceiveAndSetBoundariesWithWait()
  void PolarFieldBoundaryAverage();

  // all 3x only called in SendFluxCorrection():
  int LoadFluxBoundaryBufferSameLevel(Real *buf, const NeighborBlock& nb);
  int LoadFluxBoundaryBufferToCoarser(Real *buf, const NeighborBlock& nb);
  int LoadFluxBoundaryBufferToPolar(Real *buf, const SimpleNeighborBlock &nb,
                                    bool is_north);

  // all 7x only called in ReceiveFluxCorrection():
  void SetFluxBoundarySameLevel(Real *buf, const NeighborBlock& nb);
  void SetFluxBoundaryFromFiner(Real *buf, const NeighborBlock& nb);
  void SetFluxBoundaryFromPolar(Real **buf_list, int num_bufs, bool is_north);

  void ClearCoarseFluxBoundary();
  void AverageFluxBoundary();  // average flux from fine and equal lvls
  void PolarFluxBoundarySingleAzimuthalBlock();
  void CountFineEdges();   // called in SetupPersistentMPI()

  void CopyPolarBufferSameProcess(const SimpleNeighborBlock& nb, int ssize,
                                  int polar_block_index, bool is_north);
  // Shearing box Field
  FaceField shear_fc_[2];
  FaceField shear_flx_fc_[2];
  int shear_send_count_fc_[2][4], shear_recv_count_fc_[2][4];

#ifdef MPI_PARALLEL
  int shear_fc_phys_id_;
#endif

  void LoadShearing(FaceField &src, Real *buf, int nb);
  void SetShearingBoxBoundarySameLevel(Real *buf, const int nb);
  void RemapFlux(const int k, const int jinner, const int jouter, const int i,
                 const Real eps, const AthenaArray<Real> &var,
                 AthenaArray<Real> &flux);

  // Shearing box EMF correction
  EdgeField shear_var_emf_[2];
  EdgeField shear_map_emf_[2];
  EdgeField shear_flx_emf_[2];
  int shear_send_count_emf_[2][4], shear_recv_count_emf_[2][4];
#ifdef MPI_PARALLEL
  int shear_emf_phys_id_;
#endif

  void LoadEMFShearing(EdgeField &src, Real *buf, const int nb);
  void SetEMFShearingBoxBoundarySameLevel(EdgeField &dst, Real *buf, const int nb);
  void ClearEMFShearing(EdgeField &work);
  void RemapFluxEMF(const int k, const int jinner, const int jouter, const Real eps,
                    const AthenaArray<Real> &var, AthenaArray<Real> &flux);
};

#endif // BVALS_FC_BVALS_FC_HPP_
