//
//  Created by Bradley Austin Davis on 2016/05/09
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "ShapeEntityItem.h"

#include <glm/gtx/transform.hpp>

#include <QtCore/QDebug>

#include <GeometryUtil.h>

#include "EntitiesLogging.h"
#include "EntityItemProperties.h"
#include "EntityTree.h"
#include "EntityTreeElement.h"

namespace entity {

    /**jsdoc
     * <p>A <code>Shape</code>, <code>Box</code>, or <code>Sphere</code> {@link Entities.EntityType|EntityType} may display as 
     * one of the following geometrical shapes:</p>
     * <table>
     *   <thead>
     *     <tr><th>Value</th><th>Dimensions</th><th>Notes</th></tr>
     *   </thead>
     *   <tbody>
     *     <tr><td><code>"Circle"</code></td><td>2D</td><td>A circle oriented in 3D.</td></tr>
     *     <tr><td><code>"Cube"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Cone"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Cylinder"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Dodecahedron"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Hexagon"</code></td><td>3D</td><td>A hexagonal prism.</td></tr>
     *     <tr><td><code>"Icosahedron"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Octagon"</code></td><td>3D</td><td>An octagonal prism.</td></tr>
     *     <tr><td><code>"Octahedron"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Quad"</code></td><td>2D</td><td>A square oriented in 3D.</td></tr>
     *     <tr><td><code>"Sphere"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Tetrahedron"</code></td><td>3D</td><td></td></tr>
     *     <tr><td><code>"Torus"</code></td><td>3D</td><td><em>Not implemented.</em></td></tr>
     *     <tr><td><code>"Triangle"</code></td><td>3D</td><td>A triangular prism.</td></tr>
     *   </tbody>
     * </table>
     * @typedef {string} Entities.Shape
     */
    static const std::array<QString, Shape::NUM_SHAPES> shapeStrings { {
        "Triangle", 
        "Quad", 
        "Hexagon",
        "Octagon",
        "Circle",
        "Cube", 
        "Sphere", 
        "Tetrahedron", 
        "Octahedron", 
        "Dodecahedron", 
        "Icosahedron", 
        "Torus",  // Not implemented yet.
        "Cone", 
        "Cylinder" 
    } };

    Shape shapeFromString(const ::QString& shapeString) {
        for (size_t i = 0; i < shapeStrings.size(); ++i) {
            if (shapeString.toLower() == shapeStrings[i].toLower()) {
                return static_cast<Shape>(i);
            }
        }
        return Shape::Sphere;
    }

    ::QString stringFromShape(Shape shape) {
        return shapeStrings[shape];
    }
}

// hullShapeCalculator is a hook for external code that knows how to configure a ShapeInfo
// for given entity::Shape and dimensions
ShapeEntityItem::ShapeInfoCalculator hullShapeCalculator = nullptr;

void ShapeEntityItem::setShapeInfoCalulator(ShapeEntityItem::ShapeInfoCalculator callback) {
    hullShapeCalculator = callback;
}

ShapeEntityItem::Pointer ShapeEntityItem::baseFactory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    Pointer entity(new ShapeEntityItem(entityID), [](EntityItem* ptr) { ptr->deleteLater(); });
    entity->setProperties(properties);
    return entity;
}

EntityItemPointer ShapeEntityItem::factory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    return baseFactory(entityID, properties);
}

EntityItemPointer ShapeEntityItem::boxFactory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    auto result = baseFactory(entityID, properties);
    result->setShape(entity::Shape::Cube);
    return result;
}

EntityItemPointer ShapeEntityItem::sphereFactory(const EntityItemID& entityID, const EntityItemProperties& properties) {
    auto result = baseFactory(entityID, properties);
    result->setShape(entity::Shape::Sphere);
    return result;
}

// our non-pure virtual subclass for now...
ShapeEntityItem::ShapeEntityItem(const EntityItemID& entityItemID) : EntityItem(entityItemID) {
    _type = EntityTypes::Shape;
    _volumeMultiplier *= PI / 6.0f;
    _material = std::make_shared<graphics::Material>();
}

EntityItemProperties ShapeEntityItem::getProperties(EntityPropertyFlags desiredProperties) const {
    EntityItemProperties properties = EntityItem::getProperties(desiredProperties); // get the properties from our base class
    properties.setShape(entity::stringFromShape(getShape()));
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(color, getXColor);
    COPY_ENTITY_PROPERTY_TO_PROPERTIES(alpha, getAlpha);

    return properties;
}

void ShapeEntityItem::setShape(const entity::Shape& shape) {
    const entity::Shape prevShape = _shape;
    _shape = shape;
    switch (_shape) {
        case entity::Shape::Cube:
            _type = EntityTypes::Box;
            break;
        case entity::Shape::Sphere:
            _type = EntityTypes::Sphere;
            break;
        case entity::Shape::Circle:
            // Circle is implicitly flat so we enforce flat dimensions
            setUnscaledDimensions(getUnscaledDimensions());
            break;
        case entity::Shape::Quad:
            // Quad is implicitly flat so we enforce flat dimensions
            setUnscaledDimensions(getUnscaledDimensions());
            break;
        default:
            _type = EntityTypes::Shape;
            break;
    }

    if (_shape != prevShape) {
        // Internally grabs writeLock
        markDirtyFlags(Simulation::DIRTY_SHAPE);
    }
}

bool ShapeEntityItem::setProperties(const EntityItemProperties& properties) {
    bool somethingChanged = EntityItem::setProperties(properties); // set the properties in our base class

    SET_ENTITY_PROPERTY_FROM_PROPERTIES(alpha, setAlpha);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(color, setColor);
    SET_ENTITY_PROPERTY_FROM_PROPERTIES(shape, setShape);

    if (somethingChanged) {
        bool wantDebug = false;
        if (wantDebug) {
            uint64_t now = usecTimestampNow();
            int elapsed = now - getLastEdited();
            qCDebug(entities) << "ShapeEntityItem::setProperties() AFTER update... edited AGO=" << elapsed <<
                    "now=" << now << " getLastEdited()=" << getLastEdited();
        }
        setLastEdited(properties.getLastEdited());
    }
    return somethingChanged;
}

int ShapeEntityItem::readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead,
                                                ReadBitstreamToTreeParams& args,
                                                EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                                bool& somethingChanged) {

    int bytesRead = 0;
    const unsigned char* dataAt = data;

    READ_ENTITY_PROPERTY(PROP_SHAPE, QString, setShape);
    READ_ENTITY_PROPERTY(PROP_COLOR, rgbColor, setColor);
    READ_ENTITY_PROPERTY(PROP_ALPHA, float, setAlpha);

    return bytesRead;
}

EntityPropertyFlags ShapeEntityItem::getEntityProperties(EncodeBitstreamParams& params) const {
    EntityPropertyFlags requestedProperties = EntityItem::getEntityProperties(params);
    requestedProperties += PROP_SHAPE;
    requestedProperties += PROP_COLOR;
    requestedProperties += PROP_ALPHA;
    return requestedProperties;
}

void ShapeEntityItem::appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params,
                                    EntityTreeElementExtraEncodeDataPointer modelTreeElementExtraEncodeData,
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount, 
                                    OctreeElement::AppendState& appendState) const { 

    bool successPropertyFits = true;
    APPEND_ENTITY_PROPERTY(PROP_SHAPE, entity::stringFromShape(getShape()));
    APPEND_ENTITY_PROPERTY(PROP_COLOR, getColor());
    APPEND_ENTITY_PROPERTY(PROP_ALPHA, getAlpha());
}

void ShapeEntityItem::setColor(const rgbColor& value) {
    memcpy(_color, value, sizeof(rgbColor));
    _material->setAlbedo(glm::vec3(_color[0], _color[1], _color[2]) / 255.0f);
}

xColor ShapeEntityItem::getXColor() const {
    return xColor { _color[0], _color[1], _color[2] };
}

void ShapeEntityItem::setColor(const xColor& value) {
    setColor(rgbColor { value.red, value.green, value.blue });
}

QColor ShapeEntityItem::getQColor() const {
    auto& color = getColor();
    return QColor(color[0], color[1], color[2], (int)(getAlpha() * 255));
}

void ShapeEntityItem::setColor(const QColor& value) {
    setColor(rgbColor { (uint8_t)value.red(), (uint8_t)value.green(), (uint8_t)value.blue() });
    setAlpha(value.alpha());
}

void ShapeEntityItem::setAlpha(float alpha) {
    _alpha = alpha;
    _material->setOpacity(alpha);
}

void ShapeEntityItem::setUnscaledDimensions(const glm::vec3& value) {
    const float MAX_FLAT_DIMENSION = 0.0001f;
    if ((_shape == entity::Shape::Circle || _shape == entity::Shape::Quad) && value.y > MAX_FLAT_DIMENSION) {
        // enforce flatness in Y
        glm::vec3 newDimensions = value;
        newDimensions.y = MAX_FLAT_DIMENSION;
        EntityItem::setUnscaledDimensions(newDimensions);
    } else {
        EntityItem::setUnscaledDimensions(value);
    }
}

bool ShapeEntityItem::supportsDetailedRayIntersection() const {
    return _shape == entity::Sphere;
}

bool ShapeEntityItem::findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                                                   OctreeElementPointer& element,
                                                   float& distance, BoxFace& face, glm::vec3& surfaceNormal,
                                                   QVariantMap& extraInfo, bool precisionPicking) const {
    // determine the ray in the frame of the entity transformed from a unit sphere
    glm::mat4 entityToWorldMatrix = getEntityToWorldMatrix();
    glm::mat4 worldToEntityMatrix = glm::inverse(entityToWorldMatrix);
    glm::vec3 entityFrameOrigin = glm::vec3(worldToEntityMatrix * glm::vec4(origin, 1.0f));
    glm::vec3 entityFrameDirection = glm::normalize(glm::vec3(worldToEntityMatrix * glm::vec4(direction, 0.0f)));

    float localDistance;
    // NOTE: unit sphere has center of 0,0,0 and radius of 0.5
    if (findRaySphereIntersection(entityFrameOrigin, entityFrameDirection, glm::vec3(0.0f), 0.5f, localDistance)) {
        // determine where on the unit sphere the hit point occured
        glm::vec3 entityFrameHitAt = entityFrameOrigin + (entityFrameDirection * localDistance);
        // then translate back to work coordinates
        glm::vec3 hitAt = glm::vec3(entityToWorldMatrix * glm::vec4(entityFrameHitAt, 1.0f));
        distance = glm::distance(origin, hitAt);
        bool success;
        surfaceNormal = glm::normalize(hitAt - getCenterPosition(success));
        if (!success) {
            return false;
        }
        return true;
    }
    return false;
}

void ShapeEntityItem::debugDump() const {
    quint64 now = usecTimestampNow();
    qCDebug(entities) << "SHAPE EntityItem id:" << getEntityItemID() << "---------------------------------------------";
    qCDebug(entities) << "               name:" << _name;
    qCDebug(entities) << "              shape:" << stringFromShape(_shape) << " (EnumId: " << _shape << " )";
    qCDebug(entities) << " collisionShapeType:" << ShapeInfo::getNameForShapeType(getShapeType());
    qCDebug(entities) << "              color:" << _color[0] << "," << _color[1] << "," << _color[2];
    qCDebug(entities) << "           position:" << debugTreeVector(getWorldPosition());
    qCDebug(entities) << "         dimensions:" << debugTreeVector(getScaledDimensions());
    qCDebug(entities) << "      getLastEdited:" << debugTime(getLastEdited(), now);
    qCDebug(entities) << "SHAPE EntityItem Ptr:" << this;
}

void ShapeEntityItem::computeShapeInfo(ShapeInfo& info) {

    // This will be called whenever DIRTY_SHAPE flag (set by dimension change, etc)
    // is set.

    const glm::vec3 entityDimensions = getScaledDimensions();

    switch (_shape){
        case entity::Shape::Quad:
            // Quads collide like flat Cubes
        case entity::Shape::Cube: {
            _collisionShapeType = SHAPE_TYPE_BOX;
        }
        break;
        case entity::Shape::Sphere: {
            float diameter = entityDimensions.x;
            const float MIN_DIAMETER = 0.001f;
            const float MIN_RELATIVE_SPHERICAL_ERROR = 0.001f;
            if (diameter > MIN_DIAMETER
                && fabsf(diameter - entityDimensions.y) / diameter < MIN_RELATIVE_SPHERICAL_ERROR
                && fabsf(diameter - entityDimensions.z) / diameter < MIN_RELATIVE_SPHERICAL_ERROR) {

                _collisionShapeType = SHAPE_TYPE_SPHERE;
            } else {
                _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            }
        }
        break;
        case entity::Shape::Circle:
            // Circles collide like flat Cylinders
        case entity::Shape::Cylinder: {
            float diameter = entityDimensions.x;
            const float MIN_DIAMETER = 0.001f;
            const float MIN_RELATIVE_SPHERICAL_ERROR = 0.001f;
            if (diameter > MIN_DIAMETER
                && fabsf(diameter - entityDimensions.z) / diameter < MIN_RELATIVE_SPHERICAL_ERROR) {
                _collisionShapeType = SHAPE_TYPE_CYLINDER_Y;
            } else if (hullShapeCalculator) {
                hullShapeCalculator(this, info);
                _collisionShapeType = SHAPE_TYPE_SIMPLE_HULL;
            } else {
                // woops, someone forgot to hook up the hullShapeCalculator()!
                // final fallback is ellipsoid
                _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            }
        }
        break;
        case entity::Shape::Cone: {
            if (hullShapeCalculator) {
                hullShapeCalculator(this, info);
                _collisionShapeType = SHAPE_TYPE_SIMPLE_HULL;
            } else {
                _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            }
        }
        break;
        // gons, ones, & angles built via GeometryCache::extrudePolygon
        case entity::Shape::Triangle:
        case entity::Shape::Hexagon:
        case entity::Shape::Octagon: {
            if (hullShapeCalculator) {
                hullShapeCalculator(this, info);
                _collisionShapeType = SHAPE_TYPE_SIMPLE_HULL;
            } else {
                _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            }
        }
        break;
        // hedrons built via GeometryCache::setUpFlatShapes
        case entity::Shape::Tetrahedron:
        case entity::Shape::Octahedron:
        case entity::Shape::Dodecahedron:
        case entity::Shape::Icosahedron: {
            if ( hullShapeCalculator ) {
                hullShapeCalculator(this, info);
                _collisionShapeType = SHAPE_TYPE_SIMPLE_HULL;
            } else {
                _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            }
        }
        break;
        case entity::Shape::Torus: {
            // Not in GeometryCache::buildShapes, unsupported.
            _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
            //TODO handle this shape more correctly
        }
        break;
        default: {
            _collisionShapeType = SHAPE_TYPE_ELLIPSOID;
        }
        break;
    }

    EntityItem::computeShapeInfo(info);
}

// This value specifies how the shape should be treated by physics calculations.
ShapeType ShapeEntityItem::getShapeType() const {
    return _collisionShapeType;
}

