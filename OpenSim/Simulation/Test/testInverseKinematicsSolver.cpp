/* -------------------------------------------------------------------------- *
 *                OpenSim:  testInverseKinematicsSolver.cpp                   *
 * -------------------------------------------------------------------------- *
 * The OpenSim API is a toolkit for musculoskeletal modeling and simulation.  *
 * See http://opensim.stanford.edu and the NOTICE file for more information.  *
 * OpenSim is developed at Stanford University and supported by the US        *
 * National Institutes of Health (U54 GM072970, R24 HD065690) and by DARPA    *
 * through the Warrior Web program.                                           *
 *                                                                            *
 * Copyright (c) 2005-2016 Stanford University and the Authors                *
 * Author(s): Ajay Seth                                                       *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

//=============================================================================
// testInverseKinematicsSolver verifies that changes to the accuracy and marker
// weights have expected effects on the inverse kinematics results/errors
//=============================================================================
#include <OpenSim/Simulation/osimSimulation.h>
#include <OpenSim/Simulation/InverseKinematicsSolver.h>
#include <OpenSim/Simulation/MarkersReference.h>
#include <OpenSim/Common/Constant.h>
#include <OpenSim/Common/STOFileAdapter.h>
#include <random>

using namespace OpenSim;
using namespace std;

// Utility function to build a simple pendulum with markers attached
Model* constructPendulumWithMarkers();
// Using a model with markers and trajectory of states, create synthetic
// marker data. If noiseRadius is provided use it to scale the noise
// that perturbs the marker data. Optional fixed parameter to use the same
// noise for each time frame or to compute noise to be added at each frame.
MarkerData* generateMarkerDataFromModelAndStates(const Model& model,
    const StatesTrajectory& states, double noiseRadius=0, bool fixed=false);

// Verify that accuracy improves the number of decimals points to which
// the solver solution (coordinates) can be trusted as it is tightened.
void testAccuracy();
// Verify that the marker weights impact the solver and has the expected
// effect of reducing the error for the marker weight that is increased. 
void testUpdateMarkerWeights();
// Verify that the track() solution is also effected by updating marker
// weights and marker error is being reduced as its weighting increases.
void testTrackWithUpdateMarkerWeights();

int main()
{
    SimTK::Array_<std::string> failures;

    try { testAccuracy(); }
    catch (const std::exception& e) {
        cout << e.what() << endl; failures.push_back("testAccuracy");
    }
    try { testUpdateMarkerWeights(); }
    catch (const std::exception& e) {
        cout << e.what() << endl; failures.push_back("testUpdateGoalWeights");
    }
    try { testTrackWithUpdateMarkerWeights(); }
    catch (const std::exception& e) {
        cout << e.what() << endl; failures.push_back("testAccuracy");
    }

    if (!failures.empty()) {
        cout << "Done, with failure(s): " << failures << endl;
        return 1;
    }

    cout << "Done. All cases passed." << endl;

    return 0;
}

//=============================================================================
// Test Cases
//=============================================================================

void testAccuracy()
{
    cout << "\ntestInverseKinematicsSolver::testAccuracy()" << endl;

    std::unique_ptr<Model> pendulum{ constructPendulumWithMarkers() };
    Coordinate& coord = pendulum->getCoordinateSet()[0];

    double refVal = 0.123456789;
    double looseAccuracy = 1e-3;
    double tightAccuracy = 1.0e-9;

    SimTK::Array_<CoordinateReference> coordRefs;
    CoordinateReference coordRef(coord.getName(), Constant(refVal));
    coordRef.setWeight(1.0);
    coordRefs.push_back(coordRef);

    SimTK::State state = pendulum->initSystem();
    double coordValue = coord.getValue(state);
    double refVal0 = coordRefs[0].getValue(state);

    cout.precision(10);
    cout << "Initial " << coord.getName() << " value = " << coordValue <<
        " referenceValue = " << refVal0 << endl;

    coord.setValue(state, refVal);
    StatesTrajectory states;
    states.append(state);

    MarkerData* markerData =
        generateMarkerDataFromModelAndStates(*pendulum, states);
    // MarkersReference takes ownership of the markerData
    MarkersReference markersRef(markerData);
    markersRef.setDefaultWeight(1.0);

    // Reset the initial coordinate value
    coord.setValue(state, 0.0);
    InverseKinematicsSolver ikSolver(*pendulum, markersRef, coordRefs);
    ikSolver.setAccuracy(looseAccuracy);
    ikSolver.assemble(state);

    coordValue = coord.getValue(state);
    cout << "Assembled " << coord.getName() << " value = " << coordValue << endl;

    double accuracy = abs(coordValue - refVal);
    cout << "Specified accuracy: " << looseAccuracy << "; achieved: "
        << accuracy << endl;

    // test should fail if we did not meet the target accuracy
    SimTK_ASSERT_ALWAYS(accuracy <= looseAccuracy,
        "InverseKinematicsSolver failed to meet specified accuracy");
    
    SimTK::Array_<double> sqMarkerErrors;
    double looseSumSqError = 0;
    ikSolver.computeCurrentSquaredMarkerErrors(sqMarkerErrors);
    for (const double& err : sqMarkerErrors) {
        looseSumSqError += err;
    }
    
    cout << "For accuracy: " << looseAccuracy << "; Sum-squred Error: " 
        << looseSumSqError << endl;

    // Reset the initial coordinate value
    coord.setValue(state, 0.0);
    ikSolver.setAccuracy(tightAccuracy);
    ikSolver.assemble(state);

    coordValue = coord.getValue(state);
    cout << "Assembled " << coord.getName() <<" value = "<< coordValue << endl;

    accuracy = abs(coord.getValue(state) - refVal);

    cout << "Specified accuracy: " << tightAccuracy << "; achieved: "
        << accuracy << endl;

    // test should fail if we did not meet the new tighter target accuracy
    SimTK_ASSERT_ALWAYS(accuracy <= tightAccuracy,
        "InverseKinematicsSolver failed to meet specified accuracy");

    double tightSumSqError = 0;
    ikSolver.computeCurrentSquaredMarkerErrors(sqMarkerErrors);
    for (const double& err : sqMarkerErrors) {
        tightSumSqError += err;
    }

    cout << "For accuracy: " << tightAccuracy << "; Sum-squred Error: "
        << tightSumSqError << endl;

    // refining the accuracy should not increase tracking errors
    SimTK_ASSERT_ALWAYS(tightSumSqError <= looseSumSqError,
        "InverseKinematicsSolver failed to maintain or lower marker errors "
        "when accuracy was tightened.");
}

void testUpdateMarkerWeights()
{
    cout << "\ntestInverseKinematicsSolver::testUpdateMarkerWeights()" << endl;

    std::unique_ptr<Model> pendulum{ constructPendulumWithMarkers() };
    Coordinate& coord = pendulum->getCoordinateSet()[0];

    double refVal = 0.123456789;

    SimTK::State state = pendulum->initSystem();
    coord.setValue(state, refVal);

    StatesTrajectory states;
    states.append(state);

    MarkerData* markerData =
        generateMarkerDataFromModelAndStates(*pendulum, states, 0.02);
    MarkersReference markersRef(markerData);
    auto& markerNames = markersRef.getNames();

    for (const auto& name : markerNames) {
        markersRef.updMarkerWeightSet().adoptAndAppend(
            new MarkerWeight(name, 1.0));
    }

    SimTK::Array_<CoordinateReference> coordRefs;
    // Reset the initial coordinate value
    coord.setValue(state, 0.0);
    InverseKinematicsSolver ikSolver(*pendulum, markersRef, coordRefs);
    ikSolver.setAccuracy(1.0e-8);
    ikSolver.assemble(state);

    double coordValue = coord.getValue(state);
    cout << "Assembled " << coord.getName() << " value = "
        << coordValue << endl;

    SimTK::Array_<double> nominalMarkerErrors;
    ikSolver.computeCurrentMarkerErrors(nominalMarkerErrors);

    SimTK::Array_<double> markerWeights;
    markersRef.getWeights(state, markerWeights);

    for (unsigned int i = 0; i < markerNames.size(); ++i) {
        cout << markerNames[i] << "(weight = " << markerWeights[i]
            << ") error = " << nominalMarkerErrors[i] << endl;
    }

    // Increase the weight of the right marker 
    markerWeights[1] *= 10.0;
    ikSolver.updateMarkerWeights(markerWeights);

    // Reset the initial coordinate value
    coord.setValue(state, 0.0);
    ikSolver.assemble(state);

    coordValue = coord.getValue(state);
    cout << "Assembled " << coord.getName() << " value = "
        << coordValue << endl;

    SimTK::Array_<double> rightMarkerWeightedErrors;
    ikSolver.computeCurrentMarkerErrors(rightMarkerWeightedErrors);

    for (unsigned int i = 0; i < markerNames.size(); ++i) {
        cout << markerNames[i] << "(weight = " << markerWeights[i]
            << ") error = " << rightMarkerWeightedErrors[i] << endl;
    }

    // increasing the marker weight (marker[1] = "mR") should cause that marker
    // error to decrease
    SimTK_ASSERT_ALWAYS(rightMarkerWeightedErrors[1] < nominalMarkerErrors[1],
        "InverseKinematicsSolver failed to lower 'right' marker error when "
        "marker weight was increased.");

    // update the marker weights and repeat for the left hand marker "mL"
    markerWeights[2] *= 20.0; // "mL"
    ikSolver.updateMarkerWeights(markerWeights);

    // Reset the initial coordinate value and reassemble
    coord.setValue(state, 0.0);
    ikSolver.assemble(state);

    coordValue = coord.getValue(state);
    cout << "Assembled " << coord.getName() << " value = "
        << coordValue << endl;

    SimTK::Array_<double> leftMarkerWeightedErrors;
    ikSolver.computeCurrentMarkerErrors(leftMarkerWeightedErrors);

    for (unsigned int i = 0; i < markerNames.size(); ++i) {
        cout << markerNames[i] << "(weight = " << markerWeights[i]
            << ") error = " << leftMarkerWeightedErrors[i] << endl;
    }

    // increasing the marker weight (marker[2] = "mL") should cause that marker
    // error to decrease
    SimTK_ASSERT_ALWAYS(
        leftMarkerWeightedErrors[2] < rightMarkerWeightedErrors[2],
        "InverseKinematicsSolver failed to lower 'left' marker error when "
        "marker weight was increased.");
}

void testTrackWithUpdateMarkerWeights()
{
    cout << 
        "\ntestInverseKinematicsSolver::testTrackWithUpdateMarkerWeights()" 
        << endl;
    std::unique_ptr<Model> pendulum{ constructPendulumWithMarkers() };
    Coordinate& coord = pendulum->getCoordinateSet()[0];

    SimTK::State state = pendulum->initSystem();

    StatesTrajectory states;

    // sample time
    double dt = 0.01;

    for (int i = 0; i < 101; ++i) {
        state.updTime()=i*dt;
        coord.setValue(state, SimTK::Pi / 3);
        states.append(state);
    } 

    MarkerData* markerData =
        generateMarkerDataFromModelAndStates(*pendulum, states, 0.02, true);
    MarkersReference markersRef(markerData);
    auto& markerNames = markersRef.getNames();

    for (const auto& name : markerNames) {
        markersRef.updMarkerWeightSet().adoptAndAppend(
            new MarkerWeight(name, 1.0));
    }

    SimTK::Array_<CoordinateReference> coordRefs;
    // Reset the initial coordinate value
    coord.setValue(state, 0.0);
    InverseKinematicsSolver ikSolver(*pendulum, markersRef, coordRefs);
    ikSolver.setAccuracy(1e-6);
    ikSolver.assemble(state);

    int nt = markerData->getNumFrames();

    SimTK::Array_<double> markerWeights;
    markersRef.getWeights(state, markerWeights);

    SimTK::Array_<double> leftMarkerWeightedErrors;

    double previousErr = 0.1;

    for (int i = 0; i < nt; ++i) {
        state.updTime() = i*dt;
        // increment the weight of the left marker each time  
        markerWeights[2] = 0.1*i+1;
        ikSolver.updateMarkerWeights(markerWeights);
        ikSolver.track(state);

        if (i>0 && (i % 10 == 0)) {
            //get the marker errors
            ikSolver.computeCurrentMarkerErrors(leftMarkerWeightedErrors);

            cout << "time: " << state.getTime() << " | " << markerNames[2] 
                << "(weight = " << markerWeights[2] << ") error = " 
                << leftMarkerWeightedErrors[2] << endl;

            // increasing the marker weight (marker[2] = "mL") should cause
            //  that marker error to decrease
            SimTK_ASSERT_ALWAYS(
                leftMarkerWeightedErrors[2] < previousErr,
                "InverseKinematicsSolver track failed to lower 'left' "
                "marker error when marker weight was increased.");

            previousErr = leftMarkerWeightedErrors[2];
        }
    }
}

Model* constructPendulumWithMarkers()
{
    Model* pendulum = new Model();
    pendulum->setName("pendulum");
    Body* ball =
        new Body("ball", 1.0, SimTK::Vec3(0), SimTK::Inertia::sphere(0.05));
    pendulum->addBody(ball);

    // PinJoint hinge is 1m above ground origin and 1m above the ball in the ball
    // reference frame such that the ball center is at the origin with the hinge
    // angle is zero
    PinJoint* hinge = new PinJoint("hinge", pendulum->getGround(),
        SimTK::Vec3(0, 1.0, 0), SimTK::Vec3(0),
        *ball, SimTK::Vec3(0, 1.0, 0), SimTK::Vec3(0));
    hinge->updCoordinate().setName("theta");
    pendulum->addJoint(hinge);

    // Add Markers
    Marker*m0 = new Marker();
    m0->setName("m0");
    m0->setParentFrame(*ball);
    m0->set_location(SimTK::Vec3(0));
    pendulum->addMarker(m0);
    
    // Shifted Right 1cm
    Marker*mR = new Marker();
    mR->setName("mR");
    mR->setParentFrame(*ball);
    mR->set_location(SimTK::Vec3(0.01, 0, 0));
    pendulum->addMarker(mR);
    
    // Shifted Left 2cm
    Marker*mL = new Marker();
    mL->setName("mL");
    mL->setParentFrame(*ball);
    mL->set_location(SimTK::Vec3(-0.02, 0, 0));
    pendulum->addMarker(mL);

    return pendulum;
}

MarkerData* generateMarkerDataFromModelAndStates(const Model& model,
    const StatesTrajectory& states, double noiseRadius, bool fixed)
{
    // use a fixed seed so that we can reproduce and debug failures.
    std::mt19937 gen(0);
    std::normal_distribution<double> noise(0.0, 1);

    unique_ptr<Model> m{ model.clone() };
    m->finalizeFromProperties();
    
    auto* markerReporter = new TableReporterVec3();
    
    auto markers = m->getComponentList<Marker>();
    for (const auto& marker : markers) {
        markerReporter->updInput().
            connect(marker.getOutput("location"), marker.getName());
    }

    m->addComponent(markerReporter);

    SimTK::State s = m->initSystem();

    for (const auto& state : states) {
        // collect results into reporter
        m->realizeReport(state);
    }

    auto maxval = noise.max();

    // make a copy of the reported table
    auto results = markerReporter->getTable();

    SimTK::Vec3 offset = noiseRadius*SimTK::Vec3(double(noise(gen)),
        double(noise(gen)),
        double(noise(gen)));

    if (noiseRadius >= SimTK::Eps) {
        for (size_t i = 0; i < results.getNumRows(); ++i) {
            auto& row = results.updRowAtIndex(i);
            for (int j = 0; j < row.size(); ++j) {
                if (!fixed) {
                    offset = noiseRadius*SimTK::Vec3(double(noise(gen)),
                        double(noise(gen)),
                        double(noise(gen)));
                }
                // add noise to each marker
                row[j] += offset;
            }
        }
    }

    std::vector<std::string> suffixes{ ".x", ".y", ".z" };
    STOFileAdapter_<double>::write(results.flatten(suffixes),
        "tmp_markers.sto");

    auto* md = new MarkerData("tmp_markers.sto");

    return md;
}

