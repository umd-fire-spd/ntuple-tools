#include "Event.hpp"
#include "RecHitCalibration.hpp"
#include "ImagingAlgo.hpp"
#include "Helpers.hpp"
#include "ConfigurationManager.hpp"

#include <TROOT.h>
#include <TFile.h>
#include <TTree.h>
#include <TH2D.h>

#include <string>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <utility>
#include <memory>

using namespace std;

/// Get clustered hexels by re-running the clustering algorithm
/// \param hexelsClustered Will be filled with non-halo hexels containing info about cluster index and layer
/// \param hits Rec hits to be clusterized
/// \param algo Algorithm to be used for clusterization
void getRecClustersFromImagingAlgo(vector<shared_ptr<Hexel>> &hexelsClustered, shared_ptr<RecHits> &hits, ImagingAlgo *algo)
{
  // get 3D array of hexels (per layer, per 2D cluster)
  vector<vector<vector<std::unique_ptr<Hexel>>>> clusters2D;
  algo->makeClusters(clusters2D, hits);

  // get flat list of 2D clusters (as basic clusters)
  std::vector<unique_ptr<BasicCluster>> clusters2Dflat;
  algo->getClusters(clusters2Dflat, clusters2D);

  // keep only non-halo hexels
  for(auto &basicCluster : clusters2Dflat){
    for(auto hexel : basicCluster->GetHexelsInThisCluster()){
      if(!hexel->isHalo){
        hexelsClustered.push_back(hexel);
      }
    }
  }
}


int main(int argc, char* argv[])
{
  if(argc != 2){
    cout<<"Usage: algoBenchmark path_to_config"<<endl;
    exit(0);
  }
  string configPath(argv[1]);
  ConfigurationManager *config = ConfigurationManager::Instance(configPath);
  
  gROOT->ProcessLine(".L loader.C+");

  std::system(("mkdir -p "+config->GetOutputPath()).c_str());

  ImagingAlgo *algo = new ImagingAlgo();
  
  for(int nTupleIter=config->GetMinNtuple();nTupleIter<=config->GetMaxNtuple();nTupleIter++){
    cout<<"\nCurrent ntup: "<<nTupleIter<<endl;

    TFile *inFile = TFile::Open(Form("%s%i.root",config->GetInputPath().c_str(),nTupleIter));
    TTree *tree = (TTree*)inFile->Get("ana/hgc");
    long long nEvents = tree->GetEntries();


    cout<<"\n\nLoading ntuple...";
    auto start = now();
    unique_ptr<Event> hgCalEvent(new Event(tree));
    auto end = now();
    cout<<" done ("<<duration(start,end)<<" s)"<<endl;

    cout<<"n entries:"<<nEvents<<endl;

    // start event loop
    for(int iEvent=0;iEvent<nEvents;iEvent++){
      if(iEvent>config->GetMaxEventsPerTuple()) break;
      auto startEvent = now();
      hgCalEvent->GoToEvent(iEvent);

      // check if particles reached EE
      bool skipEvent = false;
      for(auto reachedEE : *(hgCalEvent->GetGenParticles()->GetReachedEE())){
        if(reachedEE==0){
          skipEvent = true;
          break;
        }
      }
      if(skipEvent) continue;

      string eventDir = config->GetOutputPath()+"/ntup"+to_string(nTupleIter)+"/event"+to_string(iEvent);
      std::system(("mkdir -p "+eventDir).c_str());

      cout<<"\nCurrent event:"<<iEvent<<endl;

      shared_ptr<RecHits> recHitsRaw = hgCalEvent->GetRecHits();
      shared_ptr<SimClusters> simClusters = hgCalEvent->GetSimClusters();

      // get simulated hits associated with a cluster
      cout<<"preparing simulated hits and clusters...";
      start = now();
      vector<RecHits*> simHitsPerClusterArray;
      recHitsRaw->GetHitsPerSimCluster(simHitsPerClusterArray, simClusters, config->GetEnergyMin());
      end = now();
      cout<<" done ("<<duration(start,end)<<" s)"<<endl;


      // get original 2d clusters from the tree
      cout<<"preparing 2d clusters from the original reco...";
      start = now();
      shared_ptr<Clusters2D> clusters2D = hgCalEvent->GetClusters2D();
      end = now();
      cout<<" done ("<<duration(start,end)<<" s)"<<endl;
      
      // re-run clustering with HGCalAlgo, save to file
      cout<<"running clustering algorithm...";
      start = now();
      std::vector<shared_ptr<Hexel>> recClusters;
      getRecClustersFromImagingAlgo(recClusters, recHitsRaw, algo);
      end = now();
      cout<<" done ("<<duration(start,end)<<" s)"<<endl;

      // recClusters -> array of hexel objects
      cout<<"looking for hits associated with hexels...";
      start = now();
      vector<RecHits*> recHitsPerClusterArray;
      recHitsRaw->GetRecHitsPerHexel(recHitsPerClusterArray, recClusters, config->GetEnergyMin());
      end = now();
      cout<<" done ("<<duration(start,end)<<" s)\n"<<endl;

      // perform final analysis, fill in histograms and save to files
      TH2D *energyComparisonHist = new TH2D("energy comparison","energy comparison",100,0,100,100,0,100);
      TH2D *energyComparisonOverlapHist = new TH2D("energy comparison overlap.","energy comparison overlap.",100,0,100,100,0,100);

      cout<<"\n\nGenerating final hists...\n\n";
      start = now();


      for(int layer=config->GetMinLayer();layer<config->GetMaxLayer();layer++){
        
        cout<<"\n\nLayer:"<<layer<<"\n"<<endl;
        
        unique_ptr<Clusters2D> clusters2DinLayer(new Clusters2D());
        clusters2D->GetClustersInLayer(clusters2DinLayer, layer);
        
        if(config->GetVerbosityLevel() >= 1){
          for(int i=0;i<clusters2DinLayer->N();i++){
            if(clusters2DinLayer->GetEnergy(i) > 0.0000001){
              cout<<"Cluster E:"<<clusters2DinLayer->GetEnergy(i)<<"\teta:"<<clusters2DinLayer->GetEta(i)<<endl;
            }
          }
        }
        for(uint recClusterIndex=0;recClusterIndex<recHitsPerClusterArray.size();recClusterIndex++){

          RecHits *recCluster = recHitsPerClusterArray[recClusterIndex];
          unique_ptr<RecHits> recHitsInLayerInCluster = recCluster->GetHitsInLayer(layer);

          if(recHitsInLayerInCluster->N()==0) continue;

          if(config->GetVerbosityLevel() >= 1){
            cout<<"\tRec Cluster E:"<<recHitsInLayerInCluster->GetTotalEnergy()<<"\teta:"<<recHitsInLayerInCluster->GetCenterEta()<<endl;
          }
          double recEnergy = recHitsInLayerInCluster->GetTotalEnergy();
          double xMaxRec   = recHitsInLayerInCluster->GetXmax();
          double xMinRec   = recHitsInLayerInCluster->GetXmin();
          double yMaxRec   = recHitsInLayerInCluster->GetYmax();
          double yMinRec   = recHitsInLayerInCluster->GetYmin();

          double recClusterX = xMinRec+(xMaxRec-xMinRec)/2.;
          double recClusterY = yMinRec+(yMaxRec-yMinRec)/2.;
          double recClusterR = max((xMaxRec-xMinRec)/2.,(yMaxRec-yMinRec)/2.);

          double assocSimEnergy = 0;

          for(uint simClusterIndex=0;simClusterIndex<simHitsPerClusterArray.size();simClusterIndex++){
            RecHits *simCluster = simHitsPerClusterArray[simClusterIndex];
            unique_ptr<RecHits> simHitsInLayerInCluster = simCluster->GetHitsInLayer(layer);

            if(simHitsInLayerInCluster->N()==0) continue;

            double simEnergy = simHitsInLayerInCluster->GetTotalEnergy();
            double xMaxSim   = simHitsInLayerInCluster->GetXmax();
            double xMinSim   = simHitsInLayerInCluster->GetXmin();
            double yMaxSim   = simHitsInLayerInCluster->GetYmax();
            double yMinSim   = simHitsInLayerInCluster->GetYmin();

            double simClusterX = xMinSim+(xMaxSim-xMinSim)/2.;
            double simClusterY = yMinSim+(yMaxSim-yMinSim)/2.;
            // double simClusterR = max((xMaxSim-xMinSim)/2.,(yMaxSim-yMinSim)/2.);

            if(recEnergy*simEnergy != 0){
              energyComparisonHist->Fill(recEnergy,simEnergy);
            }

            if(pointWithinCircle(simClusterX,simClusterY,recClusterX,recClusterY,recClusterR)){
              assocSimEnergy += simEnergy;
            }
          }
          if(recEnergy*assocSimEnergy != 0){
            energyComparisonOverlapHist->Fill(recEnergy,assocSimEnergy);
          }
        }
      }
      energyComparisonHist->SaveAs(Form("%s/energyComparisonHist.root",eventDir.c_str()));
      energyComparisonOverlapHist->SaveAs(Form("%s/energyComparisonOverlapHist.root",eventDir.c_str()));
      end = now();
      cout<<" done ("<<duration(start,end)<<" s)"<<endl;

      auto endEvent = now();
      cout<<"Total event processing time: "<<duration(startEvent,endEvent)<<" s"<<endl;

      recHitsPerClusterArray.clear();
      simHitsPerClusterArray.clear();

    }
    delete tree;
    inFile->Close();
    delete inFile;

  }
  delete algo;

  return 0;
}
