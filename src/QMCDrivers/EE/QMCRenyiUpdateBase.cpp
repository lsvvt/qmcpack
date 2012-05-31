//////////////////////////////////////////////////////////////////
// (c) Copyright 2003- by Jeongnim Kim
//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//   Jeongnim Kim
//   National Center for Supercomputing Applications &
//   Materials Computation Center
//   University of Illinois, Urbana-Champaign
//   Urbana, IL 61801
//   e-mail: jnkim@ncsa.uiuc.edu
//   Tel:    217-244-6319 (NCSA) 217-333-3324 (MCC)
//
// Supported by
//   National Center for Supercomputing Applications, UIUC
//   Materials Computation Center, UIUC
//////////////////////////////////////////////////////////////////
// -*- C++ -*-
#include "QMCDrivers/QMCDriver.h"
#include "QMCDrivers/CloneManager.h"
#include "QMCDrivers/EE/QMCRenyiUpdateBase.h"
#include "QMCDrivers/DriftOperators.h"


namespace qmcplusplus
{
    void add_ee_timers(vector<NewTimer*>& timers)
  {
    timers.push_back(new NewTimer("QMCRenyiUpdateBase::advance")); //timer for the walker loop
    timers.push_back(new NewTimer("QMCRenyiUpdateBase::movePbyP")); //timer for MC, ratio etc
    timers.push_back(new NewTimer("QMCRenyiUpdateBase::updateMBO")); //timer for measurements
    timers.push_back(new NewTimer("QMCRenyiUpdateBase::energy")); //timer for measurements
    for (int i=0; i<timers.size(); ++i) TimerManager.addTimer(timers[i]);
  }
  
  QMCRenyiUpdateBase::QMCRenyiUpdateBase(MCWalkerConfiguration& w, TrialWaveFunction& psi,
      QMCHamiltonian& h, RandomGenerator_t& rg,int order):
    QMCUpdateBase(w,psi,h,rg)
  {
    add_ee_timers(myTimers);
    RenyiOrder=order;
    W_vec.resize(order*2),
    W_vec[0]=&w;
    W_vec[0]->UseBoundBox=false;
    Psi_vec.resize(order*2),
    Psi_vec[0]=&psi;
    
    for(int i(1);i<RenyiOrder*2;i++)
    {
      W_vec[i]=new MCWalkerConfiguration(w);
      W_vec[i]->UseBoundBox=false;
      Psi_vec[i]=psi.makeClone(*W_vec[i]);
    }
    NumPtcl = W.getTotalNum();
    deltaR_vec.resize(RenyiOrder);
    for(int i(0);i<RenyiOrder;i++) deltaR_vec[i]= new ParticleSet::ParticlePos_t(NumPtcl);
  }
  
  QMCRenyiUpdateBase::~QMCRenyiUpdateBase()
  {
  }
  
    void QMCRenyiUpdateBase::initWalkers(WalkerIter_t it, WalkerIter_t it_end)
  {
    UpdatePbyP=false;
    for (;it != it_end; it++)
      {
        W_vec[0]->R = (*it)->R;
        W_vec[0]->update();
        RealType logpsi(Psi_vec[0]->evaluateLog((*W_vec[0])));
        //setScaledDriftPbyP(Tau*m_oneovermass,W.G,(*it)->Drift);
        RealType nodecorr=setScaledDriftPbyPandNodeCorr(m_tauovermass,(*W_vec[0]).G,drift);
//         RealType ene = H.evaluate(W);
        (*it)->resetProperty(logpsi,Psi_vec[0]->getPhase(),0,0.0,0.0, nodecorr);
        (*it)->Weight=1;
        for(int i(1);i<RenyiOrder*2;i++)
        {
          it++;
          (*it)->R = W_vec[0]->R;
          (*it)->G = W_vec[0]->G;
          (*it)->L = W_vec[0]->L;
          (*it)->resetProperty(logpsi,Psi_vec[0]->getPhase(),0,0.0,0.0, nodecorr);
          (*it)->Weight=1;
        }
      }
  }


  void QMCRenyiUpdateBase::initWalkersForPbyP(WalkerIter_t it, WalkerIter_t it_end)
  {
    UpdatePbyP=true;

    for (;it != it_end; it++)
    {
      Walker_t& awalker(**it);
      (*W_vec[0]).R=awalker.R;
      (*W_vec[0]).update();
      //W.loadWalker(awalker,UpdatePbyP);
      if (awalker.DataSet.size()) awalker.DataSet.clear();

      awalker.DataSet.rewind();
      RealType logpsi=Psi_vec[0]->registerData((*W_vec[0]),awalker.DataSet);
      RealType logpsi2=Psi_vec[0]->updateBuffer((*W_vec[0]),awalker.DataSet,false);
      W_vec[0]->saveWalker(awalker);
      
      for(int i(1);i<RenyiOrder*2;i++)
        {
          it++;
          Walker_t& bwalker(**it);
//           bwalker.makeCopy(awalker);
          (*W_vec[i]).R=awalker.R;
          (*W_vec[i]).update();
          if (bwalker.DataSet.size()) bwalker.DataSet.clear();

          bwalker.DataSet.rewind();
          logpsi=Psi_vec[i]->registerData((*W_vec[i]),bwalker.DataSet);
          logpsi2=Psi_vec[i]->updateBuffer((*W_vec[i]),bwalker.DataSet,false);
          W_vec[i]->saveWalker(bwalker);
        }
      
    }
  }
  
  void QMCRenyiUpdateBase::check_region(WalkerIter_t it, WalkerIter_t it_end, RealType v, string shape, ParticleSet::ParticlePos_t& ed, ParticleSet::ParticlePos_t& Center, int maxN, int minN)
  {
    computeEE=shape;
    vsize=v;
    C.resize(Center.size());
    for (int x=0;x<DIM;x++) C[0][x]=Center[0][x];
    Edge.resize(ed.size());
    for (int x=0;x<DIM;x++) Edge[0][x]=ed[0][x];
    
    Walker_t& bwalker(**it);
    regions.resize(NumPtcl+4,0);
    regions[NumPtcl+2]=1;
    
    mxN=maxN+1; mnN=minN;
    n_region.resize(maxN-minN+1,0);
    
//     centering
    while (it!=it_end)
    {
      Walker_t& awalker(**it);
      awalker.R=bwalker.R;
      for (int g=0; g<W.groups(); ++g)
        for (int iat=W.first(g); iat<W.last(g); ++iat)
        {
          for(int x(0);x<DIM;x++)
          {
            while (awalker.R[iat][x]<0) awalker.R[iat][x]+=Edge[0][x];
            while (awalker.R[iat][x]>Edge[0][x]) awalker.R[iat][x]-=Edge[0][x];
          }
        }
        it++;
    }
    
    for (int g=0; g<W.groups(); ++g)
    for (int iat=W.first(g); iat<W.last(g); ++iat)
    {
    //several volumes we can integrate out
      RealType z;
      if(computeEE=="square")
      {
        z=(bwalker.R[iat][0]);
        for (int x=0;x<DIM;x++) z=std::max(z,bwalker.R[iat][x]);
      }
      else if(computeEE=="sphere")
      {
        z=0;
        for (int x=0;x<DIM;x++) z+=std::pow(bwalker.R[iat][x]-C[0][x],2);
      }
      else if(computeEE=="halfspace")
      {
        z=(bwalker.R[iat][DIM-1]);
      }
      
      if (z<vsize)
        regions[iat]=1;
      else
        regions[iat]=0;
      
      regions[NumPtcl+regions[iat]]+=1;
    }
    if((regions[NumPtcl+1]<mxN)and(regions[NumPtcl+1]>mnN))
      n_region[regions[NumPtcl+1]-mnN]+=1;
  }
  
  void QMCRenyiUpdateBase::double_check_region(WalkerIter_t it, WalkerIter_t it_end)
  {
    Walker_t& bwalker(**it);
    std::vector<RealType> tregions;
    tregions.resize(NumPtcl+2,0);
    for (int g=0; g<W.groups(); ++g)
    for (int iat=W.first(g); iat<W.last(g); ++iat)
    {
    //several volumes we can integrate out
      RealType z;
      if(computeEE=="square")
      {
        z=(bwalker.R[iat][0]);
        for (int x=0;x<DIM;x++) z=std::max(z,bwalker.R[iat][x]);
      }
      else if(computeEE=="sphere")
      {
        z=0;
        for (int x=0;x<DIM;x++) z+=std::pow(bwalker.R[iat][x]-C[0][x],2);
      }
      else if(computeEE=="halfspace")
      {
        z=(bwalker.R[iat][DIM-1]);
      }
      
      if (z<vsize)
        tregions[iat]=1;
      else
        tregions[iat]=0;
      
      tregions[NumPtcl+regions[iat]]+=1;
    }
    for(int i(0);i<NumPtcl+2;i++)
      if(tregions[i]!=regions[i])
        cerr<<"region mismatch in"<<i<<endl;
    
  }
  
  void QMCRenyiUpdateBase::put_in_box(PosType& Pos)
  {
    for(int x(0);x<DIM;x++)
    {
      while (Pos[x]<0) Pos[x]+=Edge[0][x];
      while (Pos[x]>Edge[0][x]) Pos[x]-=Edge[0][x];
    }  
  }
  
  int QMCRenyiUpdateBase::get_region(ParticleSet::ParticlePos_t& Pos,int iat)
  {
    PosType tmpP(Pos[iat]);
    put_in_box(tmpP);
    
    RealType z;
    if(computeEE=="square")
    {
      z=(Pos[iat][0]);
      for (int x=0;x<DIM;x++) z=std::max(z,tmpP[x]);
    }
    else if(computeEE=="sphere")
    {
      z=0;
      for (int x=0;x<DIM;x++) z+=std::pow(tmpP[x]-C[0][x],2);
    }
    else if(computeEE=="halfspace")
    {
      z=(tmpP[DIM-1]);
    }
    
    if (z<vsize)
      return 1;
    else
      return 0;
  }
  
  void QMCRenyiUpdateBase::print_all()
  {
    for(int x(0);x<NumPtcl;x++)
    {
      for(int i(0);i<RenyiOrder*2;i++)
        cerr<<W_vec[i]->R[x]<<" ";
      cerr<<regions[x]<<endl;
    }
    cerr<<regions[NumPtcl+2]<<endl;
    cerr<<regions[NumPtcl+3]<<endl;
  }

}

/***************************************************************************
 * $RCSfile: VMCRenyiOMP.h,v $   $Author: jnkim $
 * $Revision: 1.5 $   $Date: 2006/07/17 14:29:40 $
 * $Id: VMCRenyiOMP.h,v 1.5 2006/07/17 14:29:40 jnkim Exp $
 ***************************************************************************/
