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

// Primary header
#include "mesh.hpp"

// C++ headers
#include <cfloat>     // FLT_MAX
#include <cmath>      // std::abs(), pow()
#include <iostream>   // cout, endl
#include <sstream>    // sstream
#include <stdexcept>  // runtime_error
#include <string>     // c_str()
#include <algorithm>  // sort, find
#include <iomanip>

#include <stdlib.h>
#include <string.h>  // memcpy

// Athena headers
#include "athena.hpp"                   // enums, macros, Real
#include "athena_arrays.hpp"            // AthenaArray
#include "coordinates/coordinates.hpp"  // Coordinates
#include "fluid/fluid.hpp"              // Fluid
#include "field/field.hpp"              // Field
#include "bvals/bvals.hpp"              // BoundaryValues
#include "fluid/eos/eos.hpp"              // FluidEqnOfState
#include "fluid/integrators/fluid_integrator.hpp"  // FluidIntegrator
#include "field/integrators/field_integrator.hpp"  // FieldIntegrator
#include "parameter_input.hpp"          // ParameterInput
#include "blockuid.hpp"        // BlockUID
#include "wrapio.hpp"
#include "tasklist.hpp"

// MPI header
#ifdef MPI_PARALLEL
#include <mpi.h>
#endif

// OpenMP header
#ifdef OPENMP_PARALLEL
#include <omp.h>
#endif

//======================================================================================
//! \file mesh.cpp
//  \brief implementation of functions in classes Mesh, and MeshBlock
//======================================================================================

//--------------------------------------------------------------------------------------
// Mesh constructor, builds mesh at start of calculation using parameters in input file

Mesh::Mesh(ParameterInput *pin, int test_flag)
{
  std::stringstream msg;
  RegionSize block_size;
  BlockUID *buid;
  BlockTree tree, *neibt;
  BlockUID comp;
  MeshBlock *pfirst;
  int block_bcs[6];
  NeighborBlock nei;
  int *ranklist;
  Real *costlist;
  Real totalcost, maxcost, mincost, mycost, targetcost;
  int nbmax;
  long int lx1, lx2, lx3;
  int ll, i, j;
  long int nrbx1, nrbx2, nrbx3;

// mesh test
  if(test_flag>0) nproc=test_flag;

// read time and cycle limits from input file

  start_time = pin->GetOrAddReal("time","start_time",0.0);
  tlim       = pin->GetReal("time","tlim");
  cfl_number = pin->GetReal("time","cfl_number");
  time = start_time;
  dt   = (FLT_MAX*0.4);

  nlim = pin->GetOrAddInteger("time","nlim",-1);
  ncycle = 0;

  adaptive=false;
  if(pin->GetOrAddString("mesh","refinement","static")=="adaptive")
    adaptive=true;

// read number of OpenMP threads for mesh

  num_mesh_threads_ = pin->GetOrAddInteger("mesh","num_threads",1);
  if (num_mesh_threads_ < 1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Number of OpenMP threads must be >= 1, but num_threads=" 
        << num_mesh_threads_ << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// read number of grid cells in root level of mesh from input file.  

  mesh_size.nx1 = pin->GetInteger("mesh","nx1");
  if (mesh_size.nx1 < 4) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "In mesh block in input file nx1 must be >= 4, but nx1=" 
        << mesh_size.nx1 << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

  mesh_size.nx2 = pin->GetInteger("mesh","nx2");
  if (mesh_size.nx2 < 1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "In mesh block in input file nx2 must be >= 1, but nx2=" 
        << mesh_size.nx2 << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

  mesh_size.nx3 = pin->GetInteger("mesh","nx3");
  if (mesh_size.nx3 < 1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "In mesh block in input file nx3 must be >= 1, but nx3=" 
        << mesh_size.nx3 << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if (mesh_size.nx2 == 1 && mesh_size.nx3 > 1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "In mesh block in input file: nx2=1, nx3=" << mesh_size.nx3 
        << ", 2D problems in x1-x3 plane not supported" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// check cfl_number
  if(cfl_number > 1.0 && mesh_size.nx2==1)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The CFL number must be smaller than 1.0 in 1D simulation" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if(cfl_number > 0.5 && mesh_size.nx2 > 1)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The CFL number must be smaller than 0.5 in 2D/3D simulation" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }


// read physical size of mesh (root level) from input file.  

  mesh_size.x1min = pin->GetReal("mesh","x1min");
  mesh_size.x2min = pin->GetReal("mesh","x2min");
  mesh_size.x3min = pin->GetReal("mesh","x3min");

  mesh_size.x1max = pin->GetReal("mesh","x1max");
  mesh_size.x2max = pin->GetReal("mesh","x2max");
  mesh_size.x3max = pin->GetReal("mesh","x3max");

  if (mesh_size.x1max <= mesh_size.x1min) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Input x1max must be larger than x1min: x1min=" << mesh_size.x1min 
        << " x1max=" << mesh_size.x1max << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if (mesh_size.x2max <= mesh_size.x2min) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Input x2max must be larger than x2min: x2min=" << mesh_size.x2min 
        << " x2max=" << mesh_size.x2max << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if (mesh_size.x3max <= mesh_size.x3min) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Input x3max must be larger than x3min: x3min=" << mesh_size.x3min 
        << " x3max=" << mesh_size.x3max << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// read ratios of grid cell size in each direction

  block_size.x1rat = mesh_size.x1rat = pin->GetOrAddReal("mesh","x1rat",1.0);
  block_size.x2rat = mesh_size.x2rat = pin->GetOrAddReal("mesh","x2rat",1.0);
  block_size.x3rat = mesh_size.x3rat = pin->GetOrAddReal("mesh","x3rat",1.0);

  if (std::abs(mesh_size.x1rat - 1.0) > 0.1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Ratio of cell sizes must be 0.9 <= x1rat <= 1.1, x1rat=" 
        << mesh_size.x1rat << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if (std::abs(mesh_size.x2rat - 1.0) > 0.1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Ratio of cell sizes must be 0.9 <= x2rat <= 1.1, x2rat=" 
        << mesh_size.x2rat << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if (std::abs(mesh_size.x3rat - 1.0) > 0.1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Ratio of cell sizes must be 0.9 <= x3rat <= 1.1, x3rat=" 
        << mesh_size.x3rat << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// read BC flags for each of the 6 boundaries in turn.  Error tests performed in
// BoundaryValues constructor

  mesh_bcs[inner_x1] = pin->GetOrAddInteger("mesh","ix1_bc",0);
  mesh_bcs[outer_x1] = pin->GetOrAddInteger("mesh","ox1_bc",0);
  mesh_bcs[inner_x2] = pin->GetOrAddInteger("mesh","ix2_bc",0);
  mesh_bcs[outer_x2] = pin->GetOrAddInteger("mesh","ox2_bc",0);
  mesh_bcs[inner_x3] = pin->GetOrAddInteger("mesh","ix3_bc",0);
  mesh_bcs[outer_x3] = pin->GetOrAddInteger("mesh","ox3_bc",0);


// read MeshBlock parameters
  block_size.nx1 = pin->GetOrAddInteger("meshblock","nx1",mesh_size.nx1);
  block_size.nx2 = pin->GetOrAddInteger("meshblock","nx2",mesh_size.nx2);
  block_size.nx3 = pin->GetOrAddInteger("meshblock","nx3",mesh_size.nx3);

// check consistency of the block and mesh
  if(mesh_size.nx1%block_size.nx1 != 0
  || mesh_size.nx2%block_size.nx2 != 0
  || mesh_size.nx3%block_size.nx3 != 0) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "the mesh must be evenly divisible by the meshblock" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if(block_size.nx1 <2 || (block_size.nx2<2 && mesh_size.nx2!=1)
     || (block_size.nx3<2 && mesh_size.nx3!=1)) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "block_size must be larger than 1." << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// create lists of start and end points
  InitBoundaryBuffer(block_size.nx1,block_size.nx2,block_size.nx3);

// calculate the number of the blocks
  nrbx1=mesh_size.nx1/block_size.nx1;
  nrbx2=mesh_size.nx2/block_size.nx2;
  nrbx3=mesh_size.nx3/block_size.nx3;
  nbmax=(nrbx1>nrbx2)?nrbx1:nrbx2;
  nbmax=(nbmax>nrbx3)?nbmax:nrbx3;

  if(myrank==0)
    std::cout << "RootGrid = " << nrbx1 << " x " << nrbx2
              << " x " << nrbx3 << std::endl;

// calculate the logical root level and maximum level
  for(root_level=0;(1<<root_level)<nbmax;root_level++);
  max_level = pin->GetOrAddInteger("mesh","nlevel",1)+root_level-1;

// create Block UID list
  tree.CreateRootGrid(nrbx1,nrbx2,nrbx3,root_level);

// SMR / AMR: create finer grids here

  tree.AssignGID(nbtotal); // count blocks at the same time
  buid=new BlockUID[nbtotal];
  tree.GetIDList(buid,nbtotal);

  ranklist=new int[nbtotal];
  nslist=new int[nproc];
  nblist=new int[nproc];
  costlist=new Real[nbtotal];
  maxcost=0.0;
  mincost=(FLT_MAX);
  totalcost=0.0;
  for(i=0;i<nbtotal;i++)
  {
    costlist[i]=1.0; // the simplest estimate; all the blocks are equal
    totalcost+=costlist[i];
    mincost=std::min(mincost,costlist[i]);
    maxcost=std::max(maxcost,costlist[i]);
  }
  j=nproc-1;
  targetcost=totalcost/nproc;
  mycost=0.0;
  // create rank list from the end: the master node should have less load
  for(i=nbtotal-1;i>=0;i--)
  {
    mycost+=costlist[i];
    ranklist[i]=j;
    if(mycost >= targetcost && j>0)
    {
      j--;
      totalcost-=mycost;
      mycost=0.0;
      targetcost=totalcost/(j+1);
    }
  }
  nslist[0]=0;
  j=0;
  for(i=1;i<nbtotal;i++) // make the list of nbstart
  {
    if(ranklist[i]!=ranklist[i-1]) {
      nblist[j]=i-nslist[j];
      nslist[++j]=i;
    }
  }
  nblist[j]=nbtotal-nslist[j];

  // store my nbstart and nbend
  nbstart=nslist[myrank];
  if(myrank+1==nproc)
    nbend=nbtotal-1;
  else 
    nbend=nslist[myrank+1]-1;

// check if there are sufficient blocks
#ifdef MPI_PARALLEL
  if(nbtotal < nproc)
  {
    if(test_flag==0) {
      msg << "### FATAL ERROR in Mesh constructor" << std::endl
          << "Too few blocks: nbtotal (" << nbtotal << ") < nproc ("<< nproc
          << ")" << std::endl;
      throw std::runtime_error(msg.str().c_str());
    }
    else { // test
      std::cout << "### Warning in Mesh constructor" << std::endl
          << "Too few blocks: nbtotal (" << nbtotal << ") < nproc ("<< nproc
          << ")" << std::endl;
    }
  }
  if(nbtotal % nproc != 0 && adaptive == false && maxcost == mincost && myrank==0)
  {
    std::cout << "### Warning in Mesh constructor" << std::endl
              << "The number of MeshBlocks cannot be divided evenly. "
              << "This will cause a poor load balance." << std::endl;
  }
#endif

  // Mesh test only; do not create meshes
  if(test_flag>0)
  {
    if(myrank==0)
      MeshTest(buid,ranklist,costlist);
    delete [] buid;
    delete [] ranklist;
    delete [] costlist;
    return;
  }

// create MeshBlock list for this process
  for(i=nbstart;i<=nbend;i++)
  {
    buid[i].GetLocation(lx1,lx2,lx3,ll);

    // calculate physical block size, x1
    if(lx1==0)
    {
      block_size.x1min=mesh_size.x1min;
      block_bcs[inner_x1]=mesh_bcs[inner_x1];
    }
    else
    {
      Real rx=(Real)lx1/(Real)(nrbx1<<(ll-root_level));
      block_size.x1min=MeshGeneratorX1(rx,mesh_size);
      block_bcs[inner_x1]=-1;
    }
    if(lx1==(nrbx1<<(ll-root_level))-1)
    {
      block_size.x1max=mesh_size.x1max;
      block_bcs[outer_x1]=mesh_bcs[outer_x1];
    }
    else
    {
      Real rx=(Real)(lx1+1)/(Real)(nrbx1<<(ll-root_level));
      block_size.x1max=MeshGeneratorX1(rx,mesh_size);
      block_bcs[outer_x1]=-1;
    }

    // calculate physical block size, x2
    if(lx2==0)
    {
      block_size.x2min=mesh_size.x2min;
      block_bcs[inner_x2]=mesh_bcs[inner_x2];
    }
    else
    {
      Real rx=(Real)lx2/(Real)(nrbx2<<(ll-root_level));
      block_size.x2min=MeshGeneratorX2(rx,mesh_size);
      block_bcs[inner_x2]=-1;
    }
    if(lx2==(nrbx2<<(ll-root_level))-1)
    {
      block_size.x2max=mesh_size.x2max;
      block_bcs[outer_x2]=mesh_bcs[outer_x2];
    }
    else
    {
      Real rx=(Real)(lx2+1)/(Real)(nrbx2<<(ll-root_level));
      block_size.x2max=MeshGeneratorX2(rx,mesh_size);
      block_bcs[outer_x2]=-1;
    }

    // calculate physical block size, x3
    if(lx3==0)
    {
      block_size.x3min=mesh_size.x3min;
      block_bcs[inner_x3]=mesh_bcs[inner_x3];
    }
    else
    {
      Real rx=(Real)lx3/(Real)(nrbx3<<(ll-root_level));
      block_size.x3min=MeshGeneratorX3(rx,mesh_size);
      block_bcs[inner_x3]=-1;
    }
    if(lx3==(nrbx3<<(ll-root_level))-1)
    {
      block_size.x3max=mesh_size.x3max;
      block_bcs[outer_x3]=mesh_bcs[outer_x3];
    }
    else
    {
      Real rx=(Real)(lx3+1)/(Real)(nrbx3<<(ll-root_level));
      block_size.x3max=MeshGeneratorX3(rx,mesh_size);
      block_bcs[outer_x3]=-1;
    }

    // create a block and add into the link list
    if(i==nbstart) {
      pblock = new MeshBlock(i, i-nbstart, buid[i], block_size, block_bcs, this, pin);
      pfirst = pblock;
    }
    else {
      pblock->next = new MeshBlock(i, i-nbstart, buid[i], block_size, block_bcs, this, pin);
      pblock->next->prev = pblock;
      pblock = pblock->next;
    }

    // search the neighboring block from the ID list.
    if(lx1==0 && mesh_bcs[inner_x1]!=4)
      pblock->SetNeighbor(inner_x1,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(inner_x1,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(inner_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(1,0,0)->GetNeighbor();
        pblock->SetNeighbor(inner_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(1,0,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(1,1,0)->GetNeighbor();
        pblock->SetNeighbor(inner_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(1,1,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }
    if(lx1==(nrbx1<<(ll-root_level))-1 && mesh_bcs[outer_x1]!=4)
      pblock->SetNeighbor(outer_x1,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(outer_x1,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(outer_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(0,0,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(0,0,1)->GetNeighbor();
        pblock->SetNeighbor(outer_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(0,1,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(0,1,1)->GetNeighbor();
        pblock->SetNeighbor(outer_x1, ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }

    if((lx2==0 && mesh_bcs[inner_x2]!=4) || mesh_size.nx2 == 1)
      pblock->SetNeighbor(inner_x2,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(inner_x2,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(inner_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(0,1,0)->GetNeighbor();
        pblock->SetNeighbor(inner_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(0,1,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(1,1,0)->GetNeighbor();
        pblock->SetNeighbor(inner_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(1,1,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }
    if((lx2==(nrbx2<<(ll-root_level))-1 && mesh_bcs[outer_x2]!=4) || mesh_size.nx2 == 1)
      pblock->SetNeighbor(outer_x2,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(outer_x2,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(outer_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(0,0,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(0,0,1)->GetNeighbor();
        pblock->SetNeighbor(outer_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(1,0,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(1,0,1)->GetNeighbor();
        pblock->SetNeighbor(outer_x2,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }

    if((lx3==0 && mesh_bcs[inner_x3]!=4) || mesh_size.nx3 == 1)
      pblock->SetNeighbor(inner_x3,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(inner_x3,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(inner_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(0,0,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(0,1,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(1,0,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(1,1,1)->GetNeighbor();
        pblock->SetNeighbor(inner_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }
    if((lx3==(nrbx3<<(ll-root_level))-1 && mesh_bcs[outer_x3]!=4) || mesh_size.nx3 == 1)
      pblock->SetNeighbor(outer_x3,-1,-1,-1,-1);
    else
    {
      neibt=tree.FindNeighbor(outer_x3,buid[i],nrbx1,nrbx2,nrbx3,root_level);
      if(neibt==NULL)
      {
        msg << "### FATAL ERROR in Mesh constructor" << std::endl
            << "the neighbor search failed, the mesh structure is broken" << std::endl;
        throw std::runtime_error(msg.str().c_str());
      }
      nei=neibt->GetNeighbor();
      if(nei.level==ll || nei.level==ll-1) // same or coarser
        pblock->SetNeighbor(outer_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]]);
      else // finer
      {
        nei= neibt->GetLeaf(0,0,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,0);
        nei= neibt->GetLeaf(0,1,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],0,1);
        nei= neibt->GetLeaf(1,0,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,0);
        nei= neibt->GetLeaf(1,1,0)->GetNeighbor();
        pblock->SetNeighbor(outer_x3,ranklist[nei.gid], nei.level, nei.gid,
                            nei.gid-nslist[ranklist[nei.gid]],1,1);
      }
    }
  }
  pblock=pfirst;

// clean up the temporary block id array
  delete [] buid;
  delete [] ranklist;
  delete [] costlist;
}


//--------------------------------------------------------------------------------------
// Mesh constructor for restarting. Load the restarting file

Mesh::Mesh(ParameterInput *pin, WrapIO& resfile, int test_flag)
{
  std::stringstream msg;
  RegionSize block_size;
  MeshBlock *pfirst;
  int idl, i, j, ll, nerr;
  long int lx1, lx2, lx3;
  WrapIOSize_t *offset;
  Real *costlist;
  int *ranklist;
  Real totalcost, targetcost, maxcost, mincost, mycost;
  BlockUID *buid;
  ID_t *rawid;

// mesh test
  if(test_flag>0) nproc=test_flag;

// read time and cycle limits from input file

  start_time = pin->GetOrAddReal("time","start_time",0.0);
  tlim       = pin->GetReal("time","tlim");
  cfl_number = pin->GetReal("time","cfl_number");
  nlim = pin->GetOrAddInteger("time","nlim",-1);

// read number of OpenMP threads for mesh
  num_mesh_threads_ = pin->GetOrAddInteger("mesh","num_threads",1);
  if (num_mesh_threads_ < 1) {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Number of OpenMP threads must be >= 1, but num_threads=" 
        << num_mesh_threads_ << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

  adaptive=false;
  if(pin->GetOrAddString("mesh","refinement","static")=="adaptive")
    adaptive=true;

  // read from the restarting file (everyone)
  // the file is already open and the pointer is set to after <par_end>
  nerr=0;
  if(resfile.Read(&nbtotal, sizeof(int), 1)!=1) nerr++;
  if(resfile.Read(&idl, sizeof(int), 1)!=1) nerr++;
  if(resfile.Read(&root_level, sizeof(int), 1)!=1) nerr++;
  if(resfile.Read(&max_level, sizeof(int), 1)!=1) nerr++;
  if(resfile.Read(&mesh_size, sizeof(RegionSize), 1)!=1) nerr++;
  if(resfile.Read(mesh_bcs, sizeof(int), 6)!=6) nerr++;
  if(resfile.Read(&time, sizeof(Real), 1)!=1) nerr++;
  if(resfile.Read(&dt, sizeof(Real), 1)!=1) nerr++;
  if(resfile.Read(&ncycle, sizeof(int), 1)!=1) nerr++;
  if(nerr>0)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The restarting file is broken." << std::endl;
    resfile.Close();
    throw std::runtime_error(msg.str().c_str());
  }

  if(idl>IDLENGTH)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "IDLENGTH in the restarting files is larger than the current configuration"
        << std::endl << "Please reconfigure the code accordingly." << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

// check cfl_number
  if(cfl_number > 1.0 && mesh_size.nx2==1)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The CFL number must be smaller than 1.0 in 1D simulation" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if(cfl_number > 0.5 && mesh_size.nx2 > 1)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The CFL number must be smaller than 0.5 in 2D/3D simulation" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }

  //initialize
  buid=new BlockUID[nbtotal];
  offset=new WrapIOSize_t[nbtotal];
  costlist=new Real[nbtotal];
  ranklist=new int[nbtotal];
  nslist=new int[nproc];
  nblist=new int[nproc];
  rawid=new ID_t[IDLENGTH];
  for(int i=0;i<IDLENGTH;i++) rawid[i]=0;

  int nx1 = pin->GetOrAddReal("meshblock","nx1",mesh_size.nx1);
  int nx2 = pin->GetOrAddReal("meshblock","nx2",mesh_size.nx2);
  int nx3 = pin->GetOrAddReal("meshblock","nx3",mesh_size.nx3);
  InitBoundaryBuffer(nx1,nx2,nx3);

  // read the id list (serial, because we need the costs for load balancing)
  // ... perhaps I should pack them.
  totalcost=0.0;
  nerr=0;
  maxcost=0.0;
  mincost=(FLT_MAX);
  for(int i=0;i<nbtotal;i++)
  {
    int bgid,level;
    if(resfile.Read(&bgid,sizeof(int),1)!=1) nerr++;
    if(resfile.Read(&level,sizeof(int),1)!=1) nerr++;
    if(resfile.Read(rawid,sizeof(ID_t),idl)!=idl) nerr++;
    if(resfile.Read(&(costlist[i]),sizeof(Real),1)!=1) nerr++;
    if(resfile.Read(&(offset[i]),sizeof(WrapIOSize_t),1)!=1) nerr++;
    buid[i].SetUID(rawid,level);
    totalcost+=costlist[i];
    mincost=std::min(mincost,costlist[i]);
    maxcost=std::max(maxcost,costlist[i]);
  }
  if(nerr>0)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "The restarting file is broken." << std::endl;
    resfile.Close();
    throw std::runtime_error(msg.str().c_str());
  }
  delete [] rawid; // no longer needed

#ifdef MPI_PARALLEL
  if(nbtotal < nproc)
  {
    if(test_flag==0) {
      msg << "### FATAL ERROR in Mesh constructor" << std::endl
          << "Too few blocks: nbtotal (" << nbtotal << ") < nproc ("<< nproc
          << ")" << std::endl;
      throw std::runtime_error(msg.str().c_str());
    }
    else { // test
      std::cout << "### Warning in Mesh constructor" << std::endl
          << "Too few blocks: nbtotal (" << nbtotal << ") < nproc ("<< nproc
          << ")" << std::endl;
      return;
    }
  }
  if(nbtotal % nproc != 0 && adaptive == false && maxcost == mincost && myrank==0)
  {
    std::cout << "### Warning in Mesh constructor" << std::endl
              << "The number of MeshBlocks cannot be divided evenly. "
              << "This will cause a poor load balance." << std::endl;
  }
#endif

  // divide the list evenly and distribute among the processes
  // note: ordering should be maintained, although it might not be optimal.
  j=nproc-1;
  targetcost=totalcost/nproc;
  mycost=0.0;
  // create rank list from the end: the master node should have less load
  for(i=nbtotal-1;i>=0;i--)
  {
    mycost+=costlist[i];
    ranklist[i]=j;
    if(mycost >= targetcost && j>0)
    {
      j--;
      totalcost-=mycost;
      mycost=0.0;
      targetcost=totalcost/(j+1);
    }
  }

  if(nbtotal < nproc && test_flag==0)
  {
    msg << "### FATAL ERROR in Mesh constructor" << std::endl
        << "Too few blocks: nbtotal (" << nbtotal << ") < nproc ("<< nproc
        << ")" << std::endl;
    throw std::runtime_error(msg.str().c_str());
  }
  if(nbtotal % nproc != 0 && adaptive == false && maxcost == mincost && myrank==0)
  {
    std::cout << "### Warning in Mesh constructor" << std::endl
              << "The number of MeshBlocks cannot be divided evenly. "
              << "This will cause a poor load balance." << std::endl;
  }

  nslist[0]=0;
  j=0;
  for(i=1;i<nbtotal;i++) // make the list of nbstart
  {
    if(ranklist[i]!=ranklist[i-1]) {
      nblist[j]=i-nslist[j];
      nslist[++j]=i;
    }
  }
  nblist[j]=nbtotal-nslist[j];
  // store my nbstart and nbend
  nbstart=nslist[myrank];
  if(myrank+1==nproc)
    nbend=nbtotal-1;
  else 
    nbend=nslist[myrank+1]-1;

  // Mesh test only; do not create meshes
  if(test_flag>0)
  {
    if(myrank==0)
      MeshTest(buid,ranklist,costlist);
    delete [] buid;
    delete [] offset;
    delete [] costlist;
    delete [] ranklist;
    return;
  }

  // load MeshBlocks (parallel)
  for(i=nbstart;i<=nbend;i++)
  {
    // create a block and add into the link list
    if(i==nbstart) {
      pblock = new MeshBlock(i, i-nbstart, this, pin, buid, resfile, offset[i],
                             costlist[i], ranklist, nslist);
      pfirst = pblock;
    }
    else {
      pblock->next = new MeshBlock(i, i-nbstart, this, pin, buid, resfile,
                                   offset[i], costlist[i], ranklist, nslist);
      pblock->next->prev = pblock;
      pblock = pblock->next;
    }
  }
  pblock=pfirst;

// clean up
  delete [] buid;
  delete [] offset;
  delete [] costlist;
  delete [] ranklist;
}


// destructor

Mesh::~Mesh()
{
  while(pblock->prev != NULL) // should not be true
    delete pblock->prev;
  while(pblock->next != NULL)
    delete pblock->next;
  delete pblock;
  delete [] nslist;
  delete [] nblist;
}


//--------------------------------------------------------------------------------------
//! \fn void Mesh::MeshTest(BlockUID *buid, int *ranklist, Real *costlist)
//  \brief print the mesh structure information
void Mesh::MeshTest(BlockUID *buid, int *ranklist, Real *costlist)
{
  int i, j, nbt=0;
  long int lx1, lx2, lx3;
  int ll;
  Real mycost=0, mincost=FLT_MAX, maxcost=0.0, totalcost=0.0;
  int *nb=new int [max_level-root_level+1];
  std::cout << "Logical level of the physical root grid = "<< root_level << std::endl;
  std::cout << "Logical level of maximum refinement = "<< max_level << std::endl;
  std::cout << "List of MeshBlocks" << std::endl;
  for(i=root_level;i<=max_level;i++)
  {
    nb[i-root_level]=0;
    for(j=0;j<nbtotal;j++)
    {
      if(buid[j].GetLevel()==i)
      {
        buid[j].GetLocation(lx1,lx2,lx3,ll);
        std::cout << "MeshBlock " << j << ", lx1 = "
                  << lx1 << ", lx2 = " << lx2 <<", lx3 = " << lx3
                  << ", logical level = " << ll << ", cost = " << costlist[j]
                  << ", rank = " << ranklist[j] << std::endl;
        mincost=std::min(mincost,costlist[i]);
        maxcost=std::max(maxcost,costlist[i]);
        totalcost+=costlist[i];
        nb[i-root_level]++;
      }
    }
  }

  for(i=root_level;i<=max_level;i++)
    std::cout << "Logical Level " << i << ": " << nb[i-root_level] << " Blocks" << std::endl;

  std::cout << "In Total : " << nbtotal << " Blocks" << std::endl << std::endl;
  std::cout << "Load Balance :" << std::endl;
  std::cout << "Minimum cost = " << mincost << ", Maximum cost = " << maxcost
            << ", Average cost = " << totalcost/nbtotal << std::endl;
  j=0;
  nbt=0;
  for(i=0;i<nbtotal;i++)
  {
    if(ranklist[i]==j)
    {
      mycost+=costlist[i];
      nbt++;
    }
    else if(ranklist[i]!=j)
    {
      std::cout << "Rank " << j << ": " << nbt <<" Blocks, cost = " << mycost << std::endl;
      mycost=costlist[i];
      nbt=1;
      j++;
    }
  }
  std::cout << "Rank " << j << ": " << nbt <<" Blocks, cost = " << mycost << std::endl;

  delete [] nb;
  return;
}


//--------------------------------------------------------------------------------------
// MeshBlock constructor: builds 1D vectors of cell positions and spacings, and
// constructs coordinate, boundary condition, fluid and field objects.

MeshBlock::MeshBlock(int igid, int ilid, BlockUID iuid, RegionSize input_block,
                     int *input_bcs, Mesh *pm, ParameterInput *pin)
{
  std::stringstream msg;
  long int lx1, lx2, lx3;
  int ll, root_level;
  RegionSize& mesh_size  = pm->mesh_size;
  long long nrootmesh, noffset;
  pmy_mesh = pm;
  root_level = pm->root_level;
  block_size = input_block;
  for(int i=0; i<6; i++) block_bcs[i] = input_bcs[i];
  prev=NULL;
  next=NULL;
  gid=igid;
  lid=ilid;
  uid=iuid;
  cost=1.0;
  task=NULL;

// initialize grid indices

  is = NGHOST;
  ie = is + block_size.nx1 - 1;

  if (block_size.nx2 > 1) {
    js = NGHOST;
    je = js + block_size.nx2 - 1;
  } else {
    js = je = 0;
  }

  if (block_size.nx3 > 1) {
    ks = NGHOST;
    ke = ks + block_size.nx3 - 1;
  } else {
    ks = ke = 0;
  }

  uid.GetLocation(lx1,lx2,lx3,ll);
  std::cout << "MeshBlock " << gid << ", rank = " << myrank << ", lx1 = "
            << lx1 << ", lx2 = " << lx2 <<", lx3 = " << lx3 << ", level = "
            << ll << std::endl;
  std::cout << "is=" << is << " ie=" << ie << " x1min=" << block_size.x1min
            << " x1max=" << block_size.x1max << std::endl;
  std::cout << "js=" << js << " je=" << je << " x2min=" << block_size.x2min
            << " x2max=" << block_size.x2max << std::endl;
  std::cout << "ks=" << ks << " ke=" << ke << " x3min=" << block_size.x3min
            << " x3max=" << block_size.x3max << std::endl;

// allocate arrays for sizes and positions of cells

  int ncells1 = block_size.nx1 + 2*(NGHOST);
  int ncells2 = 1, ncells3 = 1;
  if (block_size.nx2 > 1) ncells2 = block_size.nx2 + 2*(NGHOST);
  if (block_size.nx3 > 1) ncells3 = block_size.nx3 + 2*(NGHOST);

// cell sizes
  dx1f.NewAthenaArray(ncells1);
  dx2f.NewAthenaArray(ncells2);
  dx3f.NewAthenaArray(ncells3);
  dx1v.NewAthenaArray(ncells1);
  dx2v.NewAthenaArray(ncells2);
  dx3v.NewAthenaArray(ncells3);

// cell positions. Note the extra element for cell face positions
  x1f.NewAthenaArray((ncells1+1));
  x2f.NewAthenaArray((ncells2+1));
  x3f.NewAthenaArray((ncells3+1));
  x1v.NewAthenaArray(ncells1);
  x2v.NewAthenaArray(ncells2);
  x3v.NewAthenaArray(ncells3);

// X1-DIRECTION: initialize sizes and positions of cell FACES (dx1f,x1f)

  nrootmesh=mesh_size.nx1*(1L<<(ll-root_level));
  if(block_size.x1rat == 1.0) { // uniform
    Real dx=(block_size.x1max-block_size.x1min)/block_size.nx1;
    for(int i=is-NGHOST; i<=ie+NGHOST; ++i)
      dx1f(i)=dx;
    x1f(is-NGHOST)=block_size.x1min-NGHOST*dx;
    for(int i=is-NGHOST+1;i<=ie+NGHOST+1;i++)
      x1f(i)=x1f(i-1)+dx;
    x1f(is) = block_size.x1min;
    x1f(ie+1) = block_size.x1max;
  }
  else {
    for (int i=is-NGHOST; i<=ie+NGHOST+1; ++i) {
       // if there are too many levels, this won't work or be precise enough
      noffset=(i-is)+(long long)lx1*block_size.nx1;
      Real rx=(Real)noffset/(Real)nrootmesh;
      x1f(i)=pm->MeshGeneratorX1(rx,mesh_size);
    }
    x1f(is) = block_size.x1min;
    x1f(ie+1) = block_size.x1max;
    for(int i=is-NGHOST; i<=ie+NGHOST; ++i)
      dx1f(i)=x1f(i+1)-x1f(i);
  }

// correct cell face positions in ghost zones for reflecting boundary condition
  if (block_bcs[inner_x1] == 1) {
    for (int i=1; i<=(NGHOST); ++i) {
      dx1f(is-i) = dx1f(is+i-1);
       x1f(is-i) =  x1f(is-i+1) - dx1f(is-i);
    }
  }
  if (block_bcs[outer_x1] == 1) {
    for (int i=1; i<=(NGHOST); ++i) {
      dx1f(ie+i  ) = dx1f(ie-i+1);
       x1f(ie+i+1) =  x1f(ie+i) + dx1f(ie+i);
    }
  }

// X2-DIRECTION: initialize spacing and positions of cell FACES (dx2f,x2f)

  if(block_size.nx2 > 1)
  {
    nrootmesh=mesh_size.nx2*(1L<<(ll-root_level));
    if(block_size.x2rat == 1.0) { // uniform
      Real dx=(block_size.x2max-block_size.x2min)/block_size.nx2;
      for(int j=js-NGHOST; j<=je+NGHOST; ++j)
        dx2f(j)=dx;
      x2f(js-NGHOST)=block_size.x2min-NGHOST*dx;
      for(int j=js-NGHOST+1;j<=je+NGHOST+1;j++)
        x2f(j)=x2f(j-1)+dx;
      x2f(js) = block_size.x2min;
      x2f(je+1) = block_size.x2max;
    }
    else {
      for (int j=js-NGHOST; j<=je+NGHOST+1; ++j) {
         // if there are too many levels, this won't work or be precise enough
        noffset=(j-js)+(long long)lx2*block_size.nx2;
        Real rx=(Real)noffset/(Real)nrootmesh;
        x2f(j)=pm->MeshGeneratorX2(rx,mesh_size);
      }
      x2f(js) = block_size.x2min;
      x2f(je+1) = block_size.x2max;
      for(int j=js-NGHOST; j<=je+NGHOST; ++j)
        dx2f(j)=x2f(j+1)-x2f(j);
    }

  // correct cell face positions in ghost zones for reflecting boundary condition
    if (block_bcs[inner_x2] == 1) {
      for (int j=1; j<=(NGHOST); ++j) {
        dx2f(js-j) = dx2f(js+j-1);
         x2f(js-j) =  x2f(js-j+1) - dx2f(js-j);
      }
    }
    if (block_bcs[outer_x2] == 1) {
      for (int j=1; j<=(NGHOST); ++j) {
        dx2f(je+j  ) = dx2f(je-j+1);
         x2f(je+j+1) =  x2f(je+j) + dx2f(je+j);
      }
    }
  }
  else {
    dx2f(js) = block_size.x2max-block_size.x2min;
    x2f(js  ) = block_size.x2min;
    x2f(je+1) = block_size.x2max;
  }


// X3-DIRECTION: initialize spacing and positions of cell FACES (dx3f,x3f)


  if(block_size.nx3 > 1)
  {
    nrootmesh=mesh_size.nx3*(1L<<(ll-root_level));
    if(block_size.x3rat == 1.0) { // uniform
      Real dx=(block_size.x3max-block_size.x3min)/block_size.nx3;
      for(int k=ks-NGHOST; k<=ke+NGHOST; ++k)
        dx3f(k)=dx;
      x3f(ks-NGHOST)=block_size.x3min-NGHOST*dx;
      for(int k=ks-NGHOST+1;k<=ke+NGHOST+1;k++)
        x3f(k)=x3f(k-1)+dx;
      x3f(ks) = block_size.x3min;
      x3f(ke+1) = block_size.x3max;
    }
    else {
      for (int k=ks-NGHOST; k<=ke+NGHOST+1; ++k) {
         // if there are too many levels, this won't work or be precise enough
        noffset=(k-ks)+(long long)lx3*block_size.nx3;
        Real rx=(Real)noffset/(Real)nrootmesh;
        x3f(k)=pm->MeshGeneratorX3(rx,mesh_size);
      }
      x3f(ks) = block_size.x3min;
      x3f(ke+1) = block_size.x3max;
      for(int k=ks-NGHOST; k<=ke+NGHOST; ++k)
        dx3f(k)=x3f(k+1)-x3f(k);
    }

  // correct cell face positions in ghost zones for reflecting boundary condition
    if (block_bcs[inner_x3] == 1) {
      for (int k=1; k<=(NGHOST); ++k) {
        dx3f(ks-k) = dx3f(ks+k-1);
         x3f(ks-k) =  x3f(ks-k+1) - dx3f(ks-k);
      }
    }
    if (block_bcs[outer_x3] == 1) {
      for (int k=1; k<=(NGHOST); ++k) {
        dx3f(ke+k  ) = dx3f(ke-k+1);
         x3f(ke+k+1) =  x3f(ke+k) + dx3f(ke+k);
      }
    }
  }
  else {
    dx3f(ks) = block_size.x3max-block_size.x3min;
    x3f(ks  ) = block_size.x3min;
    x3f(ke+1) = block_size.x3max;
  }

// construct Coordinates and Fluid objects stored in MeshBlock class.  Note that the
// initial conditions for the fluid are set in problem generator called from main, not
// in the Fluid constructor
 
  pcoord = new Coordinates(this, pin);
  pfluid = new Fluid(this, pin);
  pfield = new Field(this, pin);
  pbval  = new BoundaryValues(this, pin);

  return;
}

//--------------------------------------------------------------------------------------
// MeshBlock constructor for restarting

MeshBlock::MeshBlock(int igid, int ilid, Mesh *pm, ParameterInput *pin, BlockUID *list,
           WrapIO& resfile, WrapIOSize_t offset, Real icost, int *ranklist, int *nslist)
{
  std::stringstream msg;
  pmy_mesh = pm;
  prev=NULL;
  next=NULL;
  gid=igid;
  lid=ilid;
  uid=list[gid];
  cost=icost;
  int nerr=0;
  task=NULL;

  // seek the file
  resfile.Seek(offset);
  // load block structure and neighbor
  if(resfile.Read(&block_size, sizeof(RegionSize), 1)!=1) nerr++;
  if(resfile.Read(block_bcs, sizeof(int), 6)!=6) nerr++;
  if(resfile.Read(&neighbor, sizeof(NeighborBlock), 6*2*2)!=6*2*2) nerr++;

  if(nerr>0)
  {
    msg << "### FATAL ERROR in MeshBlock constructor" << std::endl
        << "The restarting file is broken." << std::endl;
    resfile.Close();
    throw std::runtime_error(msg.str().c_str());
  }

  // set rank and local id
  for(int k=0; k<6; k++) {
    for(int j=0; j<=1; j++) {
      for(int i=0; i<=1; i++) {
        if(neighbor[k][j][i].gid!=-1) {
          neighbor[k][j][i].rank=ranklist[neighbor[k][j][i].gid];
          neighbor[k][j][i].lid=neighbor[k][j][i].gid-nslist[neighbor[k][j][i].rank];
        }
      }
    }
  }

// initialize grid indices

  is = NGHOST;
  ie = is + block_size.nx1 - 1;

  if (block_size.nx2 > 1) {
    js = NGHOST;
    je = js + block_size.nx2 - 1;
  } else {
    js = je = 0;
  }

  if (block_size.nx3 > 1) {
    ks = NGHOST;
    ke = ks + block_size.nx3 - 1;
  } else {
    ks = ke = 0;
  }

  long int lx1, lx2, lx3;
  int ll;
  uid.GetLocation(lx1,lx2,lx3,ll);
  std::cout << "MeshBlock " << gid << ", rank = " << myrank << ", lx1 = "
            << lx1 << ", lx2 = " << lx2 <<", lx3 = " << lx3 << ", level = "
            << ll << std::endl;
  std::cout << "is=" << is << " ie=" << ie << " x1min=" << block_size.x1min
            << " x1max=" << block_size.x1max << std::endl;
  std::cout << "js=" << js << " je=" << je << " x2min=" << block_size.x2min
            << " x2max=" << block_size.x2max << std::endl;
  std::cout << "ks=" << ks << " ke=" << ke << " x3min=" << block_size.x3min
            << " x3max=" << block_size.x3max << std::endl;

// allocate arrays for sizes and positions of cells

  int ncells1 = block_size.nx1 + 2*(NGHOST);
  int ncells2 = 1, ncells3 = 1;
  if (block_size.nx2 > 1) ncells2 = block_size.nx2 + 2*(NGHOST);
  if (block_size.nx3 > 1) ncells3 = block_size.nx3 + 2*(NGHOST);

// cell sizes
  dx1f.NewAthenaArray(ncells1);
  dx2f.NewAthenaArray(ncells2);
  dx3f.NewAthenaArray(ncells3);
  dx1v.NewAthenaArray(ncells1);
  dx2v.NewAthenaArray(ncells2);
  dx3v.NewAthenaArray(ncells3);

// cell positions. Note the extra element for cell face positions
  x1f.NewAthenaArray((ncells1+1));
  x2f.NewAthenaArray((ncells2+1));
  x3f.NewAthenaArray((ncells3+1));
  x1v.NewAthenaArray(ncells1);
  x2v.NewAthenaArray(ncells2);
  x3v.NewAthenaArray(ncells3);

  //load x1f, x2f, x3f
  nerr=0;
  if(resfile.Read(x1f.GetArrayPointer(),sizeof(Real),x1f.GetDim1())
     !=x1f.GetDim1()) nerr++;
  if(resfile.Read(x2f.GetArrayPointer(),sizeof(Real),x2f.GetDim1())
     !=x2f.GetDim1()) nerr++;
  if(resfile.Read(x3f.GetArrayPointer(),sizeof(Real),x3f.GetDim1())
     !=x3f.GetDim1()) nerr++;
  if(resfile.Read(dx1f.GetArrayPointer(),sizeof(Real),dx1f.GetDim1())
     !=dx1f.GetDim1()) nerr++;
  if(resfile.Read(dx2f.GetArrayPointer(),sizeof(Real),dx2f.GetDim1())
     !=dx2f.GetDim1()) nerr++;
  if(resfile.Read(dx3f.GetArrayPointer(),sizeof(Real),dx3f.GetDim1())
     !=dx3f.GetDim1()) nerr++;
  if(nerr>0)
  {
    msg << "### FATAL ERROR in MeshBlock constructor" << std::endl
        << "The restarting file is broken." << std::endl;
    resfile.Close();
    throw std::runtime_error(msg.str().c_str());
  }

  // create coordinates, fluid, field, and boundary conditions
  pcoord = new Coordinates(this, pin);
  pfluid = new Fluid(this, pin);
  pfield = new Field(this, pin);
  pbval  = new BoundaryValues(this, pin);

  // load fluid and field data
  nerr=0;
  if(resfile.Read(pfluid->u.GetArrayPointer(),sizeof(Real),
                         pfluid->u.GetSize())!=pfluid->u.GetSize()) nerr++;
  if (GENERAL_RELATIVITY) {
    if(resfile.Read(pfluid->w.GetArrayPointer(),sizeof(Real),
                           pfluid->w.GetSize())!=pfluid->w.GetSize()) nerr++;
    if(resfile.Read(pfluid->w1.GetArrayPointer(),sizeof(Real),
                           pfluid->w1.GetSize())!=pfluid->w1.GetSize()) nerr++;
  }
  if (MAGNETIC_FIELDS_ENABLED) {
    if(resfile.Read(pfield->b.x1f.GetArrayPointer(),sizeof(Real),
               pfield->b.x1f.GetSize())!=pfield->b.x1f.GetSize()) nerr++;
    if(resfile.Read(pfield->b.x2f.GetArrayPointer(),sizeof(Real),
               pfield->b.x2f.GetSize())!=pfield->b.x2f.GetSize()) nerr++;
    if(resfile.Read(pfield->b.x3f.GetArrayPointer(),sizeof(Real),
               pfield->b.x3f.GetSize())!=pfield->b.x3f.GetSize()) nerr++;
  }
  if(nerr>0)
  {
    msg << "### FATAL ERROR in MeshBlock constructor" << std::endl
        << "The restarting file is broken." << std::endl;
    resfile.Close();
    throw std::runtime_error(msg.str().c_str());
  }
  return;
}

// destructor

MeshBlock::~MeshBlock()
{
  dx1f.DeleteAthenaArray();  
  dx2f.DeleteAthenaArray();  
  dx3f.DeleteAthenaArray();  
  dx1v.DeleteAthenaArray();  
  dx2v.DeleteAthenaArray();  
  dx3v.DeleteAthenaArray();  
  x1f.DeleteAthenaArray();
  x2f.DeleteAthenaArray();
  x3f.DeleteAthenaArray();
  x1v.DeleteAthenaArray();
  x2v.DeleteAthenaArray();
  x3v.DeleteAthenaArray();

  if(prev!=NULL) prev->next=next;
  if(next!=NULL) next->prev=prev;

  delete pcoord;
  delete pfluid;
  delete pfield;
  delete pbval;
  if(task!=NULL)
    delete [] task;
}

//--------------------------------------------------------------------------------------
// \!fn void Mesh::NewTimeStep(void)
// \brief function that loops over all MeshBlocks and find new timestep
//        this assumes that pfluid->NewBlockTimeStep is already called
void Mesh::NewTimeStep(void)
{
  MeshBlock *pmb = pblock;
  Real min_dt=pmb->new_block_dt;
  pmb=pmb->next;
  while (pmb != NULL)  {
    min_dt=std::min(min_dt,pmb->new_block_dt);
    pmb=pmb->next;
  }
#ifdef MPI_PARALLEL
  MPI_Allreduce(MPI_IN_PLACE,&min_dt,1,MPI_ATHENA_REAL,MPI_MIN,MPI_COMM_WORLD);
#endif
  // set it
  dt=std::min(min_dt*cfl_number,2.0*dt);
}

//--------------------------------------------------------------------------------------
// \!fn void Mesh::Initialize(int res_flag, ParameterInput *pin)
// \brief  initialization before the main loop

void Mesh::Initialize(int res_flag, ParameterInput *pin)
{
  MeshBlock *pmb;
  Fluid *pfluid;
  Field *pfield;
  BoundaryValues *pbval;

  if(res_flag==0) {
    pmb = pblock;
    while (pmb != NULL)  {
      pfluid=pmb->pfluid;
      pfield=pmb->pfield;
      pbval=pmb->pbval;
      ProblemGenerator(pfluid,pfield,pin);
      pbval->CheckBoundary();
      pmb=pmb->next;
    }
  }

  pmb = pblock;
  while (pmb != NULL)  {
    pmb->pbval->Initialize();
    pmb->pbval->StartReceivingForInit();
    pmb=pmb->next;
  }

  pmb = pblock;
  while (pmb != NULL)  {
    pfluid=pmb->pfluid;
    pfield=pmb->pfield;
    pbval=pmb->pbval;
    pbval->LoadAndSendFluidBoundaryBuffer(inner_x1,pfluid->u,0);
    pbval->LoadAndSendFluidBoundaryBuffer(outer_x1,pfluid->u,0);
    if (MAGNETIC_FIELDS_ENABLED) {
      pbval->LoadAndSendFieldBoundaryBuffer(inner_x1,pfield->b,0);
      pbval->LoadAndSendFieldBoundaryBuffer(outer_x1,pfield->b,0);
    }
    pmb=pmb->next;
  }

  pmb = pblock;
  while (pmb != NULL)  {
    pfluid=pmb->pfluid;
    pfield=pmb->pfield;
    pbval=pmb->pbval;
    pbval->ReceiveAndSetFluidBoundaryWithWait(outer_x1,pfluid->u, 0);
    pbval->ReceiveAndSetFluidBoundaryWithWait(inner_x1,pfluid->u, 0);
    if (MAGNETIC_FIELDS_ENABLED) {
      pbval->ReceiveAndSetFieldBoundaryWithWait(outer_x1,pfield->b, 0);
      pbval->ReceiveAndSetFieldBoundaryWithWait(inner_x1,pfield->b, 0);
    }
    if(mesh_size.nx2 > 1) {
      pbval->LoadAndSendFluidBoundaryBuffer(inner_x2,pfluid->u,0);
      pbval->LoadAndSendFluidBoundaryBuffer(outer_x2,pfluid->u,0);
      if (MAGNETIC_FIELDS_ENABLED) {
        pbval->LoadAndSendFieldBoundaryBuffer(inner_x2,pfield->b,0);
        pbval->LoadAndSendFieldBoundaryBuffer(outer_x2,pfield->b,0);
      }
    }
    pmb=pmb->next;
  }

  if(mesh_size.nx2 > 1) {
    pmb = pblock;
    while (pmb != NULL)  {
      pfluid=pmb->pfluid;
      pfield=pmb->pfield;
      pbval=pmb->pbval;
      pbval->ReceiveAndSetFluidBoundaryWithWait(outer_x2,pfluid->u ,0);
      pbval->ReceiveAndSetFluidBoundaryWithWait(inner_x2,pfluid->u ,0);
      if (MAGNETIC_FIELDS_ENABLED) {
        pbval->ReceiveAndSetFieldBoundaryWithWait(outer_x2,pfield->b ,0);
        pbval->ReceiveAndSetFieldBoundaryWithWait(inner_x2,pfield->b ,0);
      }
      if(mesh_size.nx3 > 1) {
        pbval->LoadAndSendFluidBoundaryBuffer(inner_x3,pfluid->u,0);
        pbval->LoadAndSendFluidBoundaryBuffer(outer_x3,pfluid->u,0);
        if (MAGNETIC_FIELDS_ENABLED) {
          pbval->LoadAndSendFieldBoundaryBuffer(inner_x3,pfield->b,0);
          pbval->LoadAndSendFieldBoundaryBuffer(outer_x3,pfield->b,0);
        }
      }
      pmb=pmb->next;
    }
  }

  if(mesh_size.nx3 > 1) {
    pmb = pblock;
    while (pmb != NULL)  {
      pfluid=pmb->pfluid;
      pfield=pmb->pfield;
      pbval=pmb->pbval;
      pbval->ReceiveAndSetFluidBoundaryWithWait(outer_x3,pfluid->u ,0);
      pbval->ReceiveAndSetFluidBoundaryWithWait(inner_x3,pfluid->u ,0);
      if (MAGNETIC_FIELDS_ENABLED) {
        pbval->ReceiveAndSetFieldBoundaryWithWait(outer_x3,pfield->b ,0);
        pbval->ReceiveAndSetFieldBoundaryWithWait(inner_x3,pfield->b ,0);
      }
      pmb=pmb->next;
    }
  }

  pmb = pblock;
  while (pmb != NULL)  {
    pfluid=pmb->pfluid;
    pfield=pmb->pfield;
    pmb->pbval->ClearBoundaryForInit();
    pfluid->pf_eos->ConservedToPrimitive(pfluid->u, pfluid->w1, pfield->b, 
                                         pfluid->w, pfield->bcc);
    pmb=pmb->next;
  }

  if(res_flag==0) {
    pmb = pblock;
    while (pmb != NULL)  {
      pmb->pfluid->NewBlockTimeStep(pmb);
      pmb=pmb->next;
    }
    NewTimeStep();
  }
  return;
}


//--------------------------------------------------------------------------------------
//! \fn int64_t Mesh::GetTotalCells(void)
//  \brief return the total number of cells for performance counting
int64_t Mesh::GetTotalCells(void)
{
  return (int64_t)nbtotal*pblock->block_size.nx1*pblock->block_size.nx2*pblock->block_size.nx3;
}

//--------------------------------------------------------------------------------------
//! \fn long int MeshBlock::GetBlockSizeInBytes(void)
//  \brief Calculate the block data size required for restarting.
size_t MeshBlock::GetBlockSizeInBytes(void)
{
  size_t size;

  size =sizeof(NeighborBlock)*6*2*2+sizeof(RegionSize)+sizeof(int)*6;
  size+=sizeof(Real)*(x1f.GetSize()+x2f.GetSize()+x3f.GetSize());
  size+=sizeof(Real)*(dx1f.GetSize()+dx2f.GetSize()+dx3f.GetSize());
  size+=sizeof(Real)*pfluid->u.GetSize();
  if (GENERAL_RELATIVITY) {
    size+=sizeof(Real)*pfluid->w.GetSize();
    size+=sizeof(Real)*pfluid->w1.GetSize();
  }
  if (MAGNETIC_FIELDS_ENABLED)
    size+=sizeof(Real)*(pfield->b.x1f.GetSize()+pfield->b.x2f.GetSize()
                       +pfield->b.x3f.GetSize());
  // please add the size counter here when new physics is introduced

  return size;
}


//--------------------------------------------------------------------------------------
//! \fn void Mesh::UpdateOneStep(void)
//  \brief process the task list and advance one time step
void Mesh::UpdateOneStep(void)
{
  int nb=nbend-nbstart+1;
  MeshBlock *pmb = pblock;
  // initialize
  while (pmb != NULL)  {
    pmb->firsttask=0;
    pmb->ntodo=pmb->ntask;
    pmb->task_flag=0L;
    pmb->pbval->StartReceivingAll();
    pmb=pmb->next;
  }

  // main loop
  while(nb>0) {
    pmb = pblock;
    while (pmb != NULL)  {
      if(pmb->DoOneTask()==complete) // task list completed
        nb--;
      pmb=pmb->next;
    }
  }

  pmb = pblock;
  while (pmb != NULL)  {
    pmb->pbval->ClearBoundaryAll();
    pmb=pmb->next;
  }
  return;
}

//--------------------------------------------------------------------------------------
//! \fn void Mesh::SetTaskList(TaskList& tl)
//  \brief set task list for all the meshblocks
void Mesh::SetTaskList(TaskList& tl)
{
  MeshBlock *pmb = pblock;
  while (pmb != NULL)  {
    pmb->SetTaskList(tl);
    pmb=pmb->next;
  }
  return;
}

//--------------------------------------------------------------------------------------
//! \fn void MeshBlock::SetTaskList(TaskList& tl)
//  \brief set task list for the meshblock
void MeshBlock::SetTaskList(TaskList& tl)
{
  if(task!=NULL)
    delete [] task;
  task=new Task[tl.ntask];
  ntask=tl.ntask;
  memcpy(task, tl.task, sizeof(Task)*ntask);
  return;
}


//--------------------------------------------------------------------------------------
//! \fn enum tlstatus MeshBlock::DoOneTask(void)
//  \brief process one task (if possible), return true if the list is completed
enum tlstatus MeshBlock::DoOneTask(void) {
  int skip=0;
  bool lf, rf;
  std::stringstream msg;
  if(ntodo==0) return nothing;
  for(int i=firsttask; i<ntask; i++) {
    Task &ti=task[i];
    if((ti.taskid & task_flag)==0L) { // this task is not done
      if (((ti.depend & task_flag) == ti.depend)) { // dependency clear
        if(ti.TaskFunc(this,ti.task_arg)==true) {
          ntodo--;
          task_flag |= ti.taskid;
          if(skip==0)
            firsttask++;
//          std::cout << "MeshBlock " << gid << " task " << i << " " << ti.taskid << " done." << std::endl;
          if(ntodo==0)
            return complete;
          return running;
        }
      }
      skip++;
    }
    else if(skip==0) // this task is done and at the top of the list
      firsttask++;
  }
  
  return stuck;
}


