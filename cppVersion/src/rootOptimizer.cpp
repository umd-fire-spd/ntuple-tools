#include "GeneticHelpers.hpp"
#include "ConfigurationManager.hpp"

#include <TFitter.h>

using namespace std;

const string configPath = "configs/autoGenConfig.md";
const string outputPath = "autoGenOutput.txt";
const int nPar = 13;

void myfuncf(Int_t&, Double_t*, Double_t &f, Double_t *par, Int_t)
{
  double chi2 = 0;
  
  UpdateParamValue(configPath, "depend_sensor",par[0] < 0.5 ? 0 : 1);
  if(par[1]<0.5)      UpdateParamValue(configPath, "energy_density_function","step");
  else if(par[1]<1.5) UpdateParamValue(configPath, "energy_density_function","gaus");
  else                UpdateParamValue(configPath, "energy_density_function","exp");
  
  UpdateParamValue(configPath, "critial_distance_EE",par[2]);
  UpdateParamValue(configPath, "critial_distance_FH",par[3]);
  UpdateParamValue(configPath, "critial_distance_BH",par[4]);
  UpdateParamValue(configPath, "deltac_EE",par[5]);
  UpdateParamValue(configPath, "deltac_FH",par[6]);
  UpdateParamValue(configPath, "deltac_BH",par[7]);
  UpdateParamValue(configPath, "kappa",par[8]);
  UpdateParamValue(configPath, "energy_min",par[9]);
  UpdateParamValue(configPath, "min_clusters",par[10]);
  UpdateParamValue(configPath, "reachedEE_only",par[11] < 0.5 ? 0 : 1);
  UpdateParamValue(configPath, "matching_max_distance",par[12]);
  
  cout<<"Running clusterization"<<endl;
  system(("../clusteringAlgo/createQualityPlots "+configPath+" > /dev/null 2>&1").c_str());
//  system(("../clusteringAlgo/createQualityPlots "+configPath).c_str());
  cout<<"Clusterization output:"<<endl;
  
  ClusteringOutput output = ReadOutput(outputPath);
  output.Print();
  
  chi2 = fabs(output.resolutionMean) + output.separationMean + 1/output.containmentMean;
  cout<<"\n\nchi2:"<<chi2<<"\n\n"<<endl;
  
  f = chi2;
  return;
}


int main()
{
  UpdateParamValue(configPath, "analyze_events_per_tuple",10);
  
  TVirtualFitter::SetDefaultFitter("Minuit");
  
  TFitter *fitter = new TFitter(nPar);
  fitter->SetFCN(myfuncf);
  
  ConfigurationManager *config = ConfigurationManager::Instance(configPath);
  
  int densityFunction;
  string functionString = config->GetEnergyDensityFunction();
  if(functionString == "step")      densityFunction = 0;
  else if(functionString == "gaus") densityFunction = 1;
  else                              densityFunction = 2;
  
  // set fitter params
  fitter->SetParameter(0, "depend_sensor",          config->GetDependSensor(), 1, 0, 1); // 0 - no dependance, 1 - depend on sensor
  fitter->SetParameter(1, "energy_density_function",densityFunction, 1, 0, 2); // 0 - step, 1 - gaus, 2 - exp
  fitter->SetParameter(2, "critial_distance_EE",    config->GetCriticalDistance(kEE), 0.1, 0.0, 200.0);
  fitter->SetParameter(3, "critial_distance_FH",    config->GetCriticalDistance(kFH), 0.1, 0.0, 200.0);
  fitter->SetParameter(4, "critial_distance_BH",    config->GetCriticalDistance(kBH), 0.1, 0.0, 200.0);
  fitter->SetParameter(5, "deltac_EE",              config->GetDeltac(kEE), 0.1, 0.0, 200.0);
  fitter->SetParameter(6, "deltac_FH",              config->GetDeltac(kFH), 0.1, 0.0, 200.0);
  fitter->SetParameter(7, "deltac_BH",	            config->GetDeltac(kBH), 0.1, 0.0, 200.0);
  fitter->SetParameter(8, "kappa",                  config->GetKappa(), 0.1, 0.0, 2000.0);
  fitter->SetParameter(9, "energy_min",             config->GetEnergyMin(), 0.01, 0.0, 100.0);
  fitter->SetParameter(10,"min_clusters",           config->GetMinClusters(), 1.0, 0.0, 100.0);
  fitter->SetParameter(11,"reachedEE_only",         config->GetReachedEEonly(), 1, 0, 1);
  fitter->SetParameter(12,"matching_max_distance",  config->GetMachingMaxDistance(), 0.1, 0.0, 100);
  
  double args = 0; // put to 0 for results only, or to -1 for no garbage
  fitter->ExecuteCommand( "SET PRINTOUT"  , &args, 1);
  //  fitter->ExecuteCommand( "SET NOWARNINGS", &args, 0);
  fitter->ExecuteCommand( "SET PRINT"     , &args, 1);
  
  //  double fitterError[1] = {5.0};
  //  fitter->ExecuteCommand( "SET ERR", fitterError, 1);
  
  double strategyLevel[1] = {2};
  fitter->ExecuteCommand( "SET STR", strategyLevel, 1);
  
  cout<<"params set"<<endl;
  double arglist[1] = {0};
  fitter->ExecuteCommand("MIGRAD", arglist, 0);
  
  
  return 0;
}