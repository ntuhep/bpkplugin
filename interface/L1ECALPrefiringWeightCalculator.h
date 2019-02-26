
//Reference:
//TWiki   : https://twiki.cern.ch/twiki/bin/viewauth/CMS/L1ECALPrefiringWeightRecipe
//Internal: https://github.com/cms-sw/cmssw/blob/CMSSW_10_6_X/PhysicsTools/PatUtils/plugins/L1ECALPrefiringWeightProducer.cc

#ifndef __L1ECALPREFIRINGWEIGHTCALCULATOR__
#define __L1ECALPREFIRINGWEIGHTCALCULATOR__

#include "bpkFrameWork/bprimeKit/interface/format.h"

#include "TFile.h"
#include "TH2F.h"
#include "TLorentzVector.h"

#include<string>
#include<map>

class L1ECALPrefiringWeightCalculator{

    public:
        L1ECALPrefiringWeightCalculator(const std::string& L1Maps, const std::string& DataEra, bool UseJetEMPt = false, const double& PrefiringRateSystematicUncty = 0.2);
        ~L1ECALPrefiringWeightCalculator();

        void Calculation (const PhotonInfoBranches& photons, const JetInfoBranches& jets);
        double GetPrefiringWeight(const std::string& type = "Central") { return _weight[type]; }

    private:

        TFile* _file;
        TH2F* _h_prefmap_photon;
        TH2F* _h_prefmap_jet;
        bool _UseJetEMPt;
        double _PrefiringRateSystematicUncty;
        std::map<std::string, double> _weight;
        
        double GetPrefiringRate(const double& pt, const double& eta, TH2F* h_prefmap, const int& flag);
};

L1ECALPrefiringWeightCalculator::L1ECALPrefiringWeightCalculator(const std::string& L1Maps, 
                                                                 const std::string& DataEra, 
                                                                 bool UseJetEMPt, 
                                                                 const double& PrefiringRateSystematicUncty
                                                                )
{
    _file = TFile::Open( L1Maps.c_str() );

    std::string prefmap_photon_name = "L1prefiring_photonptvseta_" + DataEra;
    std::string prefmap_jet_name    = UseJetEMPt ? "L1prefiring_jetemptvseta_" + DataEra : "L1prefiring_jetptvseta_"+ DataEra;

    _h_prefmap_photon = (TH2F*) _file->Get( prefmap_photon_name.c_str() );
    _h_prefmap_jet    = (TH2F*) _file->Get( prefmap_jet_name.c_str()    );

    _UseJetEMPt = UseJetEMPt;
    _PrefiringRateSystematicUncty = PrefiringRateSystematicUncty;
}

L1ECALPrefiringWeightCalculator::~L1ECALPrefiringWeightCalculator()
{
    _file->Close();
}

void 
L1ECALPrefiringWeightCalculator::Calculation(const PhotonInfoBranches& photons, const JetInfoBranches& jets)
{
    _weight.clear();

    //Probability for the event NOT to prefire, computed with the prefiring maps per object. 
    //Up and down values correspond to the resulting value when shifting up/down all prefiring rates in prefiring maps. 
    double NonPrefiringProb[3] = {1., 1., 1.}; //0: central, 1: up, 2: down

    for (int flag = 0; flag < 3; flag++) {
        std::vector<int> involvedPho_index;

        //Now applying the prefiring maps to photons in the involved regions. 
        for (int i = 0; i < photons.Size; i++) {
            double pho_pt  = photons.Pt[i];
            double pho_eta = photons.Eta[i];

            if (pho_pt < 20.) continue;
            if (fabs(pho_eta) < 2. || fabs(pho_eta) > 3.) continue;
            involvedPho_index.push_back(i);

            double PrefiringProb_photon = GetPrefiringRate(pho_pt, pho_eta, _h_prefmap_photon, flag);
            NonPrefiringProb[flag] *= (1. - PrefiringProb_photon);
        }

        //Now applying the prefiring maps to jets in the involved regions. 
        for (int i = 0; i < jets.Size; i++) {
            double jet_pt  = jets.Pt[i];
            double jet_eta = jets.Eta[i];
            double jet_phi = jets.Phi[i];

            if (jet_pt < 20) continue;
            if (fabs(jet_eta) < 2. || fabs(jet_eta) > 3.) continue;

            //Loop over photons to remove overlap
            double NonPrefiringProb_OverLappingPhoton = 1.;
            for (int i_pho = 0; i_pho < (int)involvedPho_index.size(); i_pho++) {
                double pho_pt  = photons.Pt[involvedPho_index[i_pho]];
                double pho_eta = photons.Eta[involvedPho_index[i_pho]];
                double pho_phi = photons.Phi[involvedPho_index[i_pho]];

                TLorentzVector jet, pho;
                pho.SetPtEtaPhiM(0.1, pho_eta, pho_phi, 0); // 0.1 is dummy value, we only care about direction
                jet.SetPtEtaPhiM(0.1, jet_eta, jet_phi, 0.1);
                double dR = pho.DeltaR(jet);
                if (dR > 0.4) continue;

                double PrefiringProb_photon =  GetPrefiringRate(pho_pt, pho_eta, _h_prefmap_photon, flag);
                NonPrefiringProb_OverLappingPhoton *= (1. - PrefiringProb_photon);
            }

            double jet_ptem = jet_pt * (jets.CEF[i] + jets.NEF[i]);
            double PrefiringProb_jet = _UseJetEMPt ? GetPrefiringRate(jet_ptem, jet_eta, _h_prefmap_jet, flag) 
                                                   : GetPrefiringRate(jet_pt,   jet_eta, _h_prefmap_jet, flag);
            double NonPrefiringProb_OverLappingJet = (1. - PrefiringProb_jet); 

            //If there are no overlapping photons, just multiply by the jet non prefiring rate
            if (NonPrefiringProb_OverLappingPhoton == 1.) NonPrefiringProb[flag] *= NonPrefiringProb_OverLappingJet;

            //If overlapping photons have a non prefiring rate larger than the jet, then replace these weights by the jet one
            else if (NonPrefiringProb_OverLappingPhoton > NonPrefiringProb_OverLappingJet) {
                if (NonPrefiringProb_OverLappingPhoton != 0.) NonPrefiringProb[flag] *= NonPrefiringProb_OverLappingJet / NonPrefiringProb_OverLappingPhoton;
                else NonPrefiringProb[flag] = 0.;
            } 

            //If overlapping photons have a non prefiring rate smaller than the jet, don't consider the jet in the event weight
            else if(NonPrefiringProb_OverLappingPhoton < NonPrefiringProb_OverLappingJet) NonPrefiringProb[flag] = 1.;
        }

    }

    _weight.insert( std::make_pair("Central", NonPrefiringProb[0]) );
    _weight.insert( std::make_pair("Up",      NonPrefiringProb[1]) );
    _weight.insert( std::make_pair("Down",    NonPrefiringProb[2]) );

}

double
L1ECALPrefiringWeightCalculator::GetPrefiringRate(const double& pt, const double& eta, TH2F* h_prefmap, const int& flag)
{
    int nbinsy  = h_prefmap->GetNbinsY();
    double maxy = h_prefmap->GetYaxis()->GetBinLowEdge(nbinsy+1);
    double pt_ = pt;
    if(pt_ >= maxy) pt_ = maxy - 0.01;
    int thebin = h_prefmap->FindBin(eta, pt_);
          
    double prefrate =  h_prefmap->GetBinContent(thebin);
    double statuncty = h_prefmap->GetBinError(thebin); 
    double systuncty = _PrefiringRateSystematicUncty * prefrate;

    if(flag == 1) prefrate = std::min(1., prefrate + sqrt(statuncty * statuncty + systuncty * systuncty));
    if(flag == 2) prefrate = std::max(0., prefrate - sqrt(statuncty * statuncty + systuncty * systuncty));
 
    return prefrate;
}

#endif
