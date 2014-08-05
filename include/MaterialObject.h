/**
 * @file MaterialObject.h
 *
 * @date 19/giu/2014
 * @author Stefano Martina
 */

#ifndef MATERIALOBJECT_H_
#define MATERIALOBJECT_H_

#include "Property.h"
//#include "Materialway.h"

namespace insur {
  class InactiveElement;
}

using insur::InactiveElement;

namespace material {

  class MaterialTab;
  class ConversionStation;

  class MaterialObject : public PropertyObject {
  public:
    class Element; //forward declaration for getElementIfService(Element& inputElement)
  public:
    enum Type {MODULE, ROD, SERVICE};

    MaterialObject(Type materialType);
    virtual ~MaterialObject() {};

    virtual void build();

    virtual void routeServicesTo(MaterialObject& outputObject) const;
    virtual void routeServicesTo(ConversionStation& outputObject) const;
    void addElementIfService(const Element* inputElement);
    void populateInactiveElement(InactiveElement& inactiveElement) const;


    //void chargeTrain(Materialway::Train& train) const;

    //TODO: do methods for interrogate/get materials

  private:
    static const std::map<Type, const std::string> typeString;
    Type materialType_;
    ReadonlyProperty<std::string, NoDefault> type_;
    PropertyNodeUnique<std::string> materialsNode_;

    const std::string getTypeString() const;

  public:
    class Element : public PropertyObject {
    public:
      enum Unit{GRAMS, MILLIMETERS, GRAMS_METER};
      //static const std::map<Unit, const std::string> unitString;
      static const std::map<std::string, Unit> unitStringMap;

      ReadonlyProperty<std::string, NoDefault> componentName; //only the inner component's name
      ReadonlyProperty<long, NoDefault> nStripAcross;
      ReadonlyProperty<long, NoDefault> nSegments;
      ReadonlyProperty<std::string, NoDefault> elementName;
      ReadonlyProperty<bool, NoDefault> service;
      ReadonlyProperty<bool, NoDefault> scale;
      ReadonlyProperty<double, NoDefault> quantity;
      ReadonlyProperty<std::string, NoDefault> unit;

      Element();
      virtual ~Element() {};
      void build();
      //void chargeTrain(Materialway::Train& train) const;
      double quantityInGrams(InactiveElement& inactiveElement) const;
      void populateInactiveElement(InactiveElement& inactiveElement) const;
    private:
      const MaterialTab& materialTab_;
      static const std::string msg_no_valid_unit;
      //static const std::map<std::string, Materialway::Train::UnitType> unitTypeMap;
    };

  private:

    class Component : public PropertyObject {
    public:
      ReadonlyProperty<std::string, NoDefault> componentName;
      PropertyNodeUnique<std::string> componentsNode_;
      PropertyNodeUnique<std::string> elementsNode_;
      Component();
      virtual ~Component() {};
      void build();
      void routeServicesTo(MaterialObject& outputObject) const;
      void routeServicesTo(ConversionStation& outputObject) const;
      //void chargeTrain(Materialway::Train& train) const;
      void populateInactiveElement(InactiveElement& inactiveElement) const;

      std::vector<const Component*> components;
      std::vector<const Element*> elements;
    };

    class Materials : public PropertyObject {
    public:
      PropertyNodeUnique<std::string> componentsNode_;
      //Property<double, Computable> radiationLength, interactionLenght;
      Materials();
      virtual ~Materials() {};
      void build();
      void setup();
      void routeServicesTo(MaterialObject& outputObject) const;
      void routeServicesTo(ConversionStation& outputObject) const;
      //void chargeTrain(Materialway::Train& train) const;
      void populateInactiveElement(InactiveElement& inactiveElement) const;

      std::vector<const Component*> components;
    };

    //ATTENTION: Materials objects of the same structure are shared between MaterialObject objects
    //   of the modules/layer/etc.. (for containing memory use).
    //   This is not for service routing objects.
    Materials * materials;

    std::vector<const Element*> serviceElements; //used for MaterialObject not from config file (service routing)
  };
} /* namespace material */

#endif /* MATERIALOBJECT_H_ */