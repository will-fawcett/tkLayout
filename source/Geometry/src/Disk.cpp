#include "Disk.h"
#include "MessageLogger.h"
#include "ConversionStation.h"

Disk::Disk(int id, double zOffset, double zECCentre, double zECHalfLength, const PropertyNode<int>& nodeProperty, const PropertyTree& treeProperty) :
 m_materialObject(MaterialObject::LAYER),
 m_flangeConversionStation(nullptr),
 numRings(      "numRings"   , parsedAndChecked()),
 zError(        "zError"     , parsedAndChecked()),
 m_innerRadius( "innerRadius", parsedAndChecked()),
 m_outerRadius( "outerRadius", parsedAndChecked()),
 m_bigDelta(    "bigDelta"   , parsedAndChecked()),
 m_rOverlap(    "rOverlap"   , parsedOnly(), 1.),
 m_bigParity(   "bigParity"  , parsedOnly(), 1),
 m_ringNode(    "Ring"       , parsedOnly()),
 m_stationsNode("Station"    , parsedOnly()),
 m_zEndcapHalfLength(zECHalfLength),
 m_zEndcapCentre(zECCentre),
 m_zOffset(zOffset)
{
  // Set the geometry config parameters
  this->myid(id);
  this->store(treeProperty);
  if (nodeProperty.count(id)>0) this->store(nodeProperty.at(id));

}

//
// Setup: link lambda functions to various layer related properties (use setup functions for ReadOnly Computable properties -> use UncachedComputable if everytime needs to be recalculated)
//
void Disk::setup()
{
  minZ.setup([&]() { double min = +std::numeric_limits<double>::max(); for (const Ring& r : m_rings) { min = MIN(min, r.minZ()); } return min; });
  maxZ.setup([&]() { double max = -std::numeric_limits<double>::max(); for (const Ring& r : m_rings) { max = MAX(max, r.maxZ()); } return max; }); //TODO: Make this value nicer
  minR.setup([&]() { double min = +std::numeric_limits<double>::max(); for (const Ring& r : m_rings) { min = MIN(min, r.minR()); } return min; });
  maxR.setup([&]() { double max = 0;                                   for (const Ring& r : m_rings) { max = MAX(max, r.maxR()); } return max; });
  maxRingThickness.setup([&]() { double max = 0; for (const Ring& r : m_rings) { max = MAX(max, r.thickness()); } return max; });
  totalModules.setup([&]()     { int cnt = 0;    for (const Ring& r : m_rings) { cnt += r.numModules(); } return cnt; });
}

//
// Build recursively individual subdetector systems: rings -> modules & conversion stations
//
void Disk::build(const vector<double>& buildDsDistances) {

  ConversionStation* conversionStation;
  m_materialObject.store(propertyTree());
  m_materialObject.build();

  try {

    logINFO(Form("Building %s", fullid(*this).c_str()));

    // Find optimal disc appearance using extreme cases at leftmost/rightmost edges (given defined bigParity)
    if (numRings.state()) buildTopDown(buildDsDistances);
    else                  buildBottomUp(buildDsDistances);

    // Translate disc to its final position
    translateZ(m_zOffset);

  } catch (PathfulException& pe) { pe.pushPath(fullid(*this)); throw; }

  for (auto& currentStationNode : m_stationsNode) {
    conversionStation = new ConversionStation();
    conversionStation->store(currentStationNode.second);
    conversionStation->check();
    conversionStation->build();

    if(conversionStation->stationType() == ConversionStation::Type::FLANGE) {
      if(m_flangeConversionStation == nullptr) { //take only first defined flange station
        m_flangeConversionStation = conversionStation;
      }
    } else if(conversionStation->stationType() == ConversionStation::Type::SECOND) {
      m_secondConversionStations.push_back(conversionStation);
    }
  }

  cleanup();
  builtok(true);
}

//
// Cross-check parameters provdied from geometry configuration file
//
void Disk::check() {
  PropertyObject::check();
  
  if      (numRings.state() && m_innerRadius.state()) throw PathfulException("Only one between numRings and innerRadius can be specified");
  else if (!numRings.state() && !m_innerRadius.state()) throw PathfulException("At least one between numRings and innerRadius must be specified");
}

double Disk::getDsDistance(const vector<double>& buildDsDistances, int rindex) const {
  return rindex-1 >= buildDsDistances.size() ? buildDsDistances.back() : buildDsDistances.at(rindex-1);
}

//
// Limit disk geometry by eta cut
//
void Disk::cutAtEta(double eta) { 
  for (auto& r : m_rings) r.cutAtEta(eta);
  //  m_rings.erase_if([](const Ring& r) { return r.numModules() == 0; }); // get rid of rods which have been completely pruned
  m_rings.erase_if([](const Ring& r) { return r.modules().size() == 0; }); // get rid of rods which have been completely pruned
  numRings(m_rings.size());
  minZ.clear();
  minR.clear();
  maxZ.clear();
  maxR.clear();
}

//
// Use top to bottom approach when building rings -> internally called by build method
//
void Disk::buildBottomUp(const vector<double>& buildDsDistances) {
//  auto childmap = propertyTree().getChildMap<int>("Ring");
  double lastRho = 0;
  double lastSmallDelta;

  for (int i = 1, parity = -m_bigParity(); lastRho < m_outerRadius(); i++, parity *= -1) {
    Ring* ring = GeometryFactory::make<Ring>();
    ring->buildDirection(Ring::BOTTOMUP);
    ring->buildCropRadius(m_outerRadius());
    ring->store(propertyTree());
    if (m_ringNode.count(i) > 0) ring->store(m_ringNode.at(i)); /*childmap.count(i) > 0 ? childmap[i] : propertyTree()*/
    if (i == 1) {
      ring->buildStartRadius(m_innerRadius());
    } else {

      // Calcaluate new and last position in Z
      double newZ  = m_zEndcapCentre + (parity > 0 ? + m_bigDelta() : - m_bigDelta()) - ring->smallDelta() - getDsDistance(buildDsDistances, i)/2;
      double lastZ = m_zEndcapCentre + (parity > 0 ? - m_bigDelta() : + m_bigDelta()) + lastSmallDelta     + getDsDistance(buildDsDistances, i)/2;
      //double originZ = parity > 0 ? -zError() : +zError();

      // Calculate shift in Z position of extreme cases (either innemost or outermost disc)
      // Remember that disc put always in to the centre of endcap
      double shiftZ        = parity > 0 ? -m_zEndcapHalfLength : +m_zEndcapHalfLength;

      zError(10);
      // Calculate shift in Z due to beam spot
      double errorShiftZ   = parity > 0 ? -zError() : +zError();

      // Calculate next rho taking into account overlap in extreme cases of innermost or outermost disc
      double nextRhoWOverlap  = (lastRho - m_rOverlap())/(lastZ - shiftZ)*(newZ - shiftZ);
      // Calculate next rho taking into account overlap zError in extreme cases of innermost or outermost disc
      double nextRhoWZError   = (lastRho)/(lastZ - shiftZ - errorShiftZ)*(newZ - shiftZ - errorShiftZ);

      // New next rho as a minimum
      double nextRho = MIN(nextRhoWOverlap, nextRhoWZError);
      ring->buildStartRadius(nextRho);

       // For debug test only
       //std::cout << ">>> noOverlap:        " << (lastRho)/lastZ * newZ              << " New [ ; ]: " << (lastRho)/(lastZ - zHalfLength()) * (newZ - zHalfLength()) << " " << (lastRho)/(lastZ + zHalfLength()) * (newZ + zHalfLength()) << std::endl;
       //std::cout << ">>> yesOverlap:       " << (lastRho - m_rOverlap())/lastZ * newZ << " New [ ; ]: " << (lastRho - m_rOverlap())/(lastZ - zHalfLength()) * (newZ - zHalfLength()) << " " << (lastRho - m_rOverlap())/(lastZ + zHalfLength()) * (newZ + zHalfLength()) << std::endl;
       //std::cout << ">>> yesZErr:          " << (lastRho)/lastZ * newZ              << " New [ ; ]: " << (lastRho)/(lastZ - zError() - zHalfLength()) * (newZ - zError() - zHalfLength()) << " " << (lastRho)/(lastZ + zError() + zHalfLength()) * (newZ + zError() + zHalfLength()) << std::endl;
       //
       //std::cout << ">>> ShiftZ: " << shiftZ << " ErrorZ: " << errorShiftZ << " nR: "<< nextRhoWOverlap << " nRShifted: " << nextRhoWZError << std::endl;
    }
    ring->myid(i);
    ring->build();
    ring->translateZ(parity > 0 ? m_bigDelta() : -m_bigDelta());
    m_rings.push_back(ring);
    //m_ringIndexMap.insert(i, ring);
    m_ringIndexMap[i] = ring;

    // Keep for next calculation
    lastRho        = ring->maxR();
    lastSmallDelta = ring->smallDelta();
  }
  numRings(m_rings.size());
}

//
// Use bottom to top approach when building rings -> internally called by build method
//
void Disk::buildTopDown(const vector<double>& buildDsDistances) {
  double lastRho;
  double lastSmallDelta;
  for (int i = numRings(), parity = -m_bigParity(); i > 0; i--, parity *= -1) {
    Ring* ring = GeometryFactory::make<Ring>();
    ring->buildDirection(Ring::TOPDOWN);
    ring->store(propertyTree());
    if (m_ringNode.count(i) > 0) ring->store(m_ringNode.at(i));
    if (i == numRings()) {
      ring->buildStartRadius(m_outerRadius());
    } else {

      // Calcaluate new and last position in Z
      double newZ  = m_zEndcapCentre + (parity > 0 ? + m_bigDelta() : - m_bigDelta()) + ring->smallDelta() + getDsDistance(buildDsDistances, i)/2; // CUIDADO was + smallDelta + dsDistances[nRing-1]/2;
      double lastZ = m_zEndcapCentre + (parity > 0 ? - m_bigDelta() : + m_bigDelta()) - lastSmallDelta     - getDsDistance(buildDsDistances, i+1)/2; // CUIDADO was - smallDelta - dsDistances[nRing-1]/2; // try with prevRing here

      // Calculate shift in Z position of extreme cases (either innemost or outermost disc)
      // Remember that disc put always in to the centre of endcap
      double shiftZ = parity > 0 ? +m_zEndcapHalfLength : -m_zEndcapHalfLength;

      // Calculate shift in Z due to beam spot
      double errorShiftZ   = parity > 0 ? +zError() : -zError();

      // Calculate next rho taking into account overlap in extreme cases of innermost or outermost disc
      double nextRhoWOverlap  = (lastRho + m_rOverlap())/(lastZ - shiftZ)*(newZ - shiftZ);
      // Calculate next rho taking into account overlap zError in extreme cases of innermost or outermost disc
      double nextRhoWZError   = (lastRho)/(lastZ - shiftZ - errorShiftZ)*(newZ - shiftZ - errorShiftZ);

      double nextRho = MAX(nextRhoWOverlap, nextRhoWZError);
      ring->buildStartRadius(nextRho);
    }
    ring->myid(i);
    ring->build();
    ring->translateZ(parity > 0 ? m_bigDelta() : -m_bigDelta());
    //m_rings.push_back(ring);
    m_rings.insert(m_rings.begin(), ring);
    //m_ringIndexMap.insert(i, ring);
    m_ringIndexMap[i] = ring;

    // Keep for next calculation
    lastRho        = ring->minR();
    lastSmallDelta = ring->smallDelta();
  }
}

//
// GeometryVisitor pattern -> layer visitable
//
void Disk::accept(GeometryVisitor& v) {
  v.visit(*this);
  for (auto& r : m_rings) { r.accept(v); }
}

//
// GeometryVisitor pattern -> layer visitable (const. option)
//
void Disk::accept(ConstGeometryVisitor& v) const {
  v.visit(*this);
  for (const auto& r : m_rings) { r.accept(v); }
}

//
// Helper method translating Disc z position by given offset
//
void Disk::translateZ(double z) { averageZ_ += z; for (auto& r : m_rings) r.translateZ(z); }

//
// Helper method mirroring the whole Disc from zPos to -zPos or vice versa
//
void Disk::mirrorZ() {
  averageZ_ = -averageZ_;
  for (auto& r : m_rings) r.mirrorZ();
}
