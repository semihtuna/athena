#ifndef BVALS_CC_BVALS_CC_HPP_
#define BVALS_CC_BVALS_CC_HPP_
//========================================================================================
// Athena++ astrophysical MHD code
// Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
// Licensed under the 3-clause BSD License, see LICENSE file for details
//========================================================================================
//! \file bvals_cc.hpp
//  \brief

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
//! \class CellCenteredBoundaryVariable
//  \brief

class CellCenteredBoundaryVariable : public BoundaryVariable {
  // Allow functions to access most any variable to allow for fully general BC dependence
  friend class Hydro;
  friend class Field;
 public:
  CellCenteredBoundaryVariable();
  virtual ~CellCenteredBoundaryVariable();

  AthenaArray<Real> &var_cc;

  // BoundaryCommunication pure virtual functions:
  virtual void InitBoundaryData(BoundaryData &bd, enum BoundaryType type) override;
  virtual void DestroyBoundaryData(BoundaryData &bd) override;
  virtual void Initialize(void) override;
  virtual void StartReceivingForInit(bool cons_and_field) override;
  virtual void ClearBoundaryForInit(bool cons_and_field) override;
  virtual void StartReceivingAll(const Real time) override;
  virtual void ClearBoundaryAll(void) override;

  // BoundaryBuffer pure virtual functions:
  virtual int LoadBoundaryBufferSameLevel(
       int nl, int nu, Real *buf, const NeighborBlock& nb) override;
  virtual void SendBoundaryBuffers( enum CCBoundaryType type) override;
  virtual bool ReceiveBoundaryBuffers(enum CCBoundaryType type) override;
  virtual void ReceiveAndSetBoundariesWithWait(
                                               enum CCBoundaryType type) override;
  virtual void SetBoundaries( enum CCBoundaryType type) override;
  virtual void SetBoundarySameLevel(
       int nl, int nu, Real *buf,
      const NeighborBlock& nb, bool *flip) override;
  virtual int LoadBoundaryBufferToCoarser(
       int nl, int nu, Real *buf, AthenaArray<Real> &cbuf,
      const NeighborBlock& nb) override;
  virtual int LoadBoundaryBufferToFiner(
       int nl, int nu, Real *buf, const NeighborBlock& nb) override;
  virtual void SetBoundaryFromCoarser(
      int nl, int nu, Real *buf, AthenaArray<Real> &cbuf,
      const NeighborBlock& nb, bool *flip) override;
  virtual void SetBoundaryFromFiner(
       int nl, int nu,
      Real *buf, const NeighborBlock& nb, bool *flip) override;

  virtual void SendFluxCorrection(enum FluxCorrectionType type) override;
  virtual bool ReceiveFluxCorrection(enum FluxCorrectionType type) override;
  // TODO(felker): FLUX_HYDRO=0 is the only defined FluxCorrectionType enum in athena.hpp
  // TODO(felker): handle the 6x unique Field-related flux correction functions
  // Cell-centered flux correction functions are much simpler than Field counterpart
  // In addition to 2x simple Send/Recv EMFCorrection() functions, there are:
  // - 6x Load/Set EMF (not correction). No Load to finer, to Set to coarser, but
  //   Set/LoadEMFBoundaryPolarBuffer()
  // - AverageEMFBoundary(), ClearCoarseEMFBoundary(),
  //                         PolarBoundarySingleAzimuthalBlockEMF()

  // optional: compare to PolarBoundarySingleAzimuthalBlockField(),
  //                      PolarBoundarySingleAzimuthalBlockEMF()
  // what about PolarBoundaryAverageField()?
  void PolarBoundarySingleAzimuthalBlock(int nl, int nu);

  // Shearingbox Hydro
  // void LoadHydroShearing(AthenaArray<Real> &src, Real *buf, int nb);
  // void SendHydroShearingboxBoundaryBuffersForInit(AthenaArray<Real> &src, bool cons);
  // void SendHydroShearingboxBoundaryBuffers(AthenaArray<Real> &src, bool cons);

  // void SetHydroShearingboxBoundarySameLevel(AthenaArray<Real> &dst, Real *buf,
  //                                           const int nb);
  // bool ReceiveHydroShearingboxBoundaryBuffers(AthenaArray<Real> &dst);
  // void FindShearBlock(const Real time);
  // void RemapFlux(const int n, const int k, const int jinner, const int jouter,
  //                const int i, const Real eps, const AthenaArray<Real> &U,
  //                AthenaArray<Real> &Flux);

  // BoundaryPhysics pure virtual functions:
  virtual void ReflectInnerX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void ReflectInnerX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void ReflectInnerX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void ReflectOuterX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void ReflectOuterX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void ReflectOuterX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;

  virtual void OutflowInnerX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void OutflowInnerX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void OutflowInnerX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void OutflowOuterX1(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void OutflowOuterX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;
  virtual void OutflowOuterX3(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                              int il, int iu, int jl, int ju,
                              int kl, int ku, int nu, int ngh) override;

  virtual void PolarWedgeInnerX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                                 int il, int iu, int jl,
                                 int ju, int kl, int ku, int nu, int ngh) override;
  virtual void PolarWedgeOuterX2(MeshBlock *pmb, Coordinates *pco, Real time, Real dt,
                                 int il, int iu, int jl,
                                 int ju, int kl, int ku, int nu, int ngh) override;
  //protected:

 private:
  // standard cell-centered and flux BV private variables
  BoundaryData bd_hydro_, bd_flcor_;

  // Shearingbox Hydro
  //   enum BoundaryStatus shbox_inner_hydro_flag_[4], shbox_outer_hydro_flag_[4];
  //   // working arrays of remapped quantities
  //   AthenaArray<Real>  shboxvar_inner_hydro_, shboxvar_outer_hydro_;
  //   // Hydro flux from conservative remapping
  //   AthenaArray<Real>  flx_inner_hydro_, flx_outer_hydro_;
  //   int  send_innersize_hydro_[4], recv_innersize_hydro_[4]; // buffer sizes
  //   Real *send_innerbuf_hydro_[4], *recv_innerbuf_hydro_[4]; // send and recv buffers
  //   int  send_outersize_hydro_[4], recv_outersize_hydro_[4]; // buffer sizes
  //   Real *send_outerbuf_hydro_[4], *recv_outerbuf_hydro_[4]; // send and recv buffers
  // #ifdef MPI_PARALLEL
  //   // MPI request for send and recv msgs
  //   MPI_Request rq_innersend_hydro_[4], rq_innerrecv_hydro_[4];
  //   MPI_Request rq_outersend_hydro_[4], rq_outerrecv_hydro_[4];
  // #endif
};

#endif // BVALS_CC_BVALS_CC_HPP_
