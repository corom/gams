/*********************************************************************
 * Usage of this software requires acceptance of the SMASH-CMU License,
 * which can be found at the following URL:
 *
 * https://code.google.com/p/smash-cmu/wiki/License
 *********************************************************************/

/*********************************************************************
 * MadaraSystemController.h - Class that simulates a System Controller.
 *********************************************************************/

#pragma once

#ifndef _MADARA_SYSTEM_CONTROLLER_H
#define _MADARA_SYSTEM_CONTROLLER_H

#include "madara/knowledge_engine/Knowledge_Base.h"
#include <vector>
#include <string>
using std::string;
#include "utilities/Position.h"
#include "utilities/CommonMadaraVariables.h"

// Class that simulates the Madara controller of the system (though it also acts as a bridge between
// the drone information given by the simualted drones and the Madara knowledge base).
class MadaraController
{
private:
    // By default we identify ourselves by the hostname set in our OS.
    std::string m_host;

    // Used for updating various transport settings
    Madara::Transport::Settings m_transportSettings;

    // The controller's id.
    int m_id;

    // The communications range for the network.
    double m_commRange;

    // The min altitude for the flying devices.
    double m_minAltitude;

    // The actual knowledge base.
    Madara::Knowledge_Engine::Knowledge_Base* m_knowledge;

    // A counter for the regions created.
    int m_regionId;
public:
    MadaraController(int id, double commRange, double minAltitude);
    ~MadaraController();
    void terminate();

    void updateNetworkStatus(const int& numberOfDrones);

    // Bridge methods.
    void setupBridgeRequest(int bridgeId, SMASH::Utilities::Region startRegion, 
                                          SMASH::Utilities::Region endRegion);

    // Area coverage methods.
    void setNewSearchArea(int searchAreaId, SMASH::Utilities::Region areaBoundaries);
    void requestAreaCoverage(int droneId, int searchAreaId, string algo = AREA_COVERAGE_SNAKE);
};

#endif
