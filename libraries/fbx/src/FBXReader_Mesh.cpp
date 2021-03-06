//
//  FBXReader_Mesh.cpp
//  interface/src/fbx
//
//  Created by Sam Gateau on 8/27/2015.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4267 )
#endif

#include <draco/compression/decode.h>

#ifdef _WIN32
#pragma warning( pop )
#endif

#include <iostream>
#include <QBuffer>
#include <QDataStream>
#include <QIODevice>
#include <QStringList>
#include <QTextStream>
#include <QtDebug>
#include <QtEndian>
#include <QFileInfo>
#include <QHash>
#include <LogHandler.h>
#include "ModelFormatLogging.h"

#include "FBXReader.h"

#include <memory>

#include <glm/detail/type_half.hpp>
#include <glm/gtc/packing.hpp>

using vec2h = glm::tvec2<glm::detail::hdata>;

#define FBX_PACK_COLORS 1

#if FBX_PACK_COLORS
using ColorType = glm::uint32;
#define FBX_COLOR_ELEMENT gpu::Element::COLOR_RGBA_32
#else
using ColorType = glm::vec3;
#define FBX_COLOR_ELEMENT gpu::Element::VEC3F_XYZ
#endif

class Vertex {
public:
    int originalIndex;
    glm::vec2 texCoord;
    glm::vec2 texCoord1;
};

uint qHash(const Vertex& vertex, uint seed = 0) {
    return qHash(vertex.originalIndex, seed);
}

bool operator==(const Vertex& v1, const Vertex& v2) {
    return v1.originalIndex == v2.originalIndex && v1.texCoord == v2.texCoord && v1.texCoord1 == v2.texCoord1;
}

class AttributeData {
public:
    QVector<glm::vec2> texCoords;
    QVector<int> texCoordIndices;
    QString name;
    int index;
};

class MeshData {
public:
    ExtractedMesh extracted;
    QVector<glm::vec3> vertices;
    QVector<int> polygonIndices;
    bool normalsByVertex;
    QVector<glm::vec3> normals;
    QVector<int> normalIndices;

    bool colorsByVertex;
    glm::vec4 averageColor{1.0f, 1.0f, 1.0f, 1.0f};
    QVector<glm::vec4> colors;
    QVector<int> colorIndices;

    QVector<glm::vec2> texCoords;
    QVector<int> texCoordIndices;

    QHash<Vertex, int> indices;

    std::vector<AttributeData> attributes;
};


void appendIndex(MeshData& data, QVector<int>& indices, int index, bool deduplicate) {
    if (index >= data.polygonIndices.size()) {
        return;
    }
    int vertexIndex = data.polygonIndices.at(index);
    if (vertexIndex < 0) {
        vertexIndex = -vertexIndex - 1;
    }
    Vertex vertex;
    vertex.originalIndex = vertexIndex;
    
    glm::vec3 position;
    if (vertexIndex < data.vertices.size()) {
        position = data.vertices.at(vertexIndex);
    }

    glm::vec3 normal;
    int normalIndex = data.normalsByVertex ? vertexIndex : index;
    if (data.normalIndices.isEmpty()) {    
        if (normalIndex < data.normals.size()) {
            normal = data.normals.at(normalIndex);
        }
    } else if (normalIndex < data.normalIndices.size()) {
        normalIndex = data.normalIndices.at(normalIndex);
        if (normalIndex >= 0 && normalIndex < data.normals.size()) {
            normal = data.normals.at(normalIndex);
        }
    }


    glm::vec4 color;
    bool hasColors = (data.colors.size() > 1);
    if (hasColors) {
        int colorIndex = data.colorsByVertex ? vertexIndex : index;
        if (data.colorIndices.isEmpty()) {    
            if (colorIndex < data.colors.size()) {
                color = data.colors.at(colorIndex);
            }
        } else if (colorIndex < data.colorIndices.size()) {
            colorIndex = data.colorIndices.at(colorIndex);
            if (colorIndex >= 0 && colorIndex < data.colors.size()) {
                color = data.colors.at(colorIndex);
            }
        }
    }

    if (data.texCoordIndices.isEmpty()) {
        if (index < data.texCoords.size()) {
            vertex.texCoord = data.texCoords.at(index);
        }
    } else if (index < data.texCoordIndices.size()) {
        int texCoordIndex = data.texCoordIndices.at(index);
        if (texCoordIndex >= 0 && texCoordIndex < data.texCoords.size()) {
            vertex.texCoord = data.texCoords.at(texCoordIndex);
        }
    }
    
    bool hasMoreTexcoords = (data.attributes.size() > 1);
    if (hasMoreTexcoords) {
        if (data.attributes[1].texCoordIndices.empty()) {
            if (index < data.attributes[1].texCoords.size()) {
                vertex.texCoord1 = data.attributes[1].texCoords.at(index);
            }
        } else if (index < data.attributes[1].texCoordIndices.size()) {
            int texCoordIndex = data.attributes[1].texCoordIndices.at(index);
            if (texCoordIndex >= 0 && texCoordIndex < data.attributes[1].texCoords.size()) {
                vertex.texCoord1 = data.attributes[1].texCoords.at(texCoordIndex);
            }
        }
    }

    QHash<Vertex, int>::const_iterator it = data.indices.find(vertex);
    if (!deduplicate || it == data.indices.constEnd()) {
        int newIndex = data.extracted.mesh.vertices.size();
        indices.append(newIndex);
        data.indices.insert(vertex, newIndex);
        data.extracted.newIndices.insert(vertexIndex, newIndex);
        data.extracted.mesh.vertices.append(position);
        data.extracted.mesh.originalIndices.append(vertexIndex);
        data.extracted.mesh.normals.append(normal);
        data.extracted.mesh.texCoords.append(vertex.texCoord);
        if (hasColors) {
            data.extracted.mesh.colors.append(glm::vec3(color));
        }
        if (hasMoreTexcoords) {
            data.extracted.mesh.texCoords1.append(vertex.texCoord1);
        }
    } else {
        indices.append(*it);
        data.extracted.mesh.normals[*it] += normal;
    }
}

ExtractedMesh FBXReader::extractMesh(const FBXNode& object, unsigned int& meshIndex, bool deduplicate) {
    MeshData data;
    data.extracted.mesh.meshIndex = meshIndex++;

    QVector<int> materials;
    QVector<int> textures;

    bool isMaterialPerPolygon = false;

    static const QVariant BY_VERTICE = QByteArray("ByVertice");
    static const QVariant INDEX_TO_DIRECT = QByteArray("IndexToDirect");

    bool isDracoMesh = false;

    foreach (const FBXNode& child, object.children) {
        if (child.name == "Vertices") {
            data.vertices = createVec3Vector(getDoubleVector(child));

        } else if (child.name == "PolygonVertexIndex") {
            data.polygonIndices = getIntVector(child);

        } else if (child.name == "LayerElementNormal") {
            data.normalsByVertex = false;
            bool indexToDirect = false;
            foreach (const FBXNode& subdata, child.children) {
                if (subdata.name == "Normals") {
                    data.normals = createVec3Vector(getDoubleVector(subdata));

                } else if (subdata.name == "NormalsIndex") {
                    data.normalIndices = getIntVector(subdata);

                } else if (subdata.name == "MappingInformationType" && subdata.properties.at(0) == BY_VERTICE) {
                    data.normalsByVertex = true;
                    
                } else if (subdata.name == "ReferenceInformationType" && subdata.properties.at(0) == INDEX_TO_DIRECT) {
                    indexToDirect = true;
                }
            }
            if (indexToDirect && data.normalIndices.isEmpty()) {
                // hack to work around wacky Makehuman exports
                data.normalsByVertex = true;
            }
        } else if (child.name == "LayerElementColor") {
            data.colorsByVertex = false;
            bool indexToDirect = false;
            foreach (const FBXNode& subdata, child.children) {
                if (subdata.name == "Colors") {
                    data.colors = createVec4VectorRGBA(getDoubleVector(subdata), data.averageColor);
                } else if (subdata.name == "ColorsIndex" || subdata.name == "ColorIndex") {
                    data.colorIndices = getIntVector(subdata);

                } else if (subdata.name == "MappingInformationType" && subdata.properties.at(0) == BY_VERTICE) {
                    data.colorsByVertex = true;
                    
                } else if (subdata.name == "ReferenceInformationType" && subdata.properties.at(0) == INDEX_TO_DIRECT) {
                    indexToDirect = true;
                }
            }
            if (indexToDirect && data.colorIndices.isEmpty()) {
                // hack to work around wacky Makehuman exports
                data.colorsByVertex = true;
            }

#if defined(FBXREADER_KILL_BLACK_COLOR_ATTRIBUTE)
            // Potential feature where we decide to kill the color attribute is to dark?
            // Tested with the model:
            // https://hifi-public.s3.amazonaws.com/ryan/gardenLight2.fbx
            // let's check if we did have true data ?
            if (glm::all(glm::lessThanEqual(data.averageColor, glm::vec4(0.09f)))) {
                data.colors.clear();
                data.colorIndices.clear();
                data.colorsByVertex = false;
                qCDebug(modelformat) << "LayerElementColor has an average value of 0.0f... let's forget it.";
            }
#endif
         
        } else if (child.name == "LayerElementUV") {
            if (child.properties.at(0).toInt() == 0) {
                AttributeData attrib;
                attrib.index = child.properties.at(0).toInt();
                foreach (const FBXNode& subdata, child.children) {
                    if (subdata.name == "UV") {
                        data.texCoords = createVec2Vector(getDoubleVector(subdata));
                        attrib.texCoords = createVec2Vector(getDoubleVector(subdata));
                    } else if (subdata.name == "UVIndex") {
                        data.texCoordIndices = getIntVector(subdata);
                        attrib.texCoordIndices = getIntVector(subdata);
                    } else if (subdata.name == "Name") {
                        attrib.name = subdata.properties.at(0).toString();
                    } 
#if defined(DEBUG_FBXREADER)
                    else {
                        int unknown = 0;
                        QString subname = subdata.name.data();
                        if ( (subdata.name == "Version")
                             || (subdata.name == "MappingInformationType")
                             || (subdata.name == "ReferenceInformationType") ) {
                        } else {
                            unknown++;
                        }
                    }
#endif
                }
                data.extracted.texcoordSetMap.insert(attrib.name, data.attributes.size());
                data.attributes.push_back(attrib);
            } else {
                AttributeData attrib;
                attrib.index = child.properties.at(0).toInt();
                foreach (const FBXNode& subdata, child.children) {
                    if (subdata.name == "UV") {
                        attrib.texCoords = createVec2Vector(getDoubleVector(subdata));
                    } else if (subdata.name == "UVIndex") {
                        attrib.texCoordIndices = getIntVector(subdata);
                    } else if  (subdata.name == "Name") {
                        attrib.name = subdata.properties.at(0).toString();
                    }
#if defined(DEBUG_FBXREADER)
                    else {
                        int unknown = 0;
                        QString subname = subdata.name.data();
                        if ( (subdata.name == "Version")
                             || (subdata.name == "MappingInformationType")
                             || (subdata.name == "ReferenceInformationType") ) {
                        } else {
                            unknown++;
                        }
                    }
#endif
                }

                QHash<QString, size_t>::iterator it = data.extracted.texcoordSetMap.find(attrib.name);
                if (it == data.extracted.texcoordSetMap.end()) {
                    data.extracted.texcoordSetMap.insert(attrib.name, data.attributes.size());
                    data.attributes.push_back(attrib);
                } else {
                    // WTF same names for different UVs?
                    qCDebug(modelformat) << "LayerElementUV #" << attrib.index << " is reusing the same name as #" << (*it) << ". Skip this texcoord attribute.";
                }
            }
        } else if (child.name == "LayerElementMaterial") {
            static const QVariant BY_POLYGON = QByteArray("ByPolygon");
            foreach (const FBXNode& subdata, child.children) {
                if (subdata.name == "Materials") {
                    materials = getIntVector(subdata);
                } else if (subdata.name == "MappingInformationType") {
                    if (subdata.properties.at(0) == BY_POLYGON) {
                        isMaterialPerPolygon = true;
                    }
                } else {
                    isMaterialPerPolygon = false;
                }
            }


        } else if (child.name == "LayerElementTexture") {
            foreach (const FBXNode& subdata, child.children) {
                if (subdata.name == "TextureId") {
                    textures = getIntVector(subdata);
                }
            }
        } else if (child.name == "DracoMesh") {
            isDracoMesh = true;
            data.extracted.mesh.wasCompressed = true;

            // load the draco mesh from the FBX and create a draco::Mesh
            draco::Decoder decoder;
            draco::DecoderBuffer decodedBuffer;
            QByteArray dracoArray = child.properties.at(0).value<QByteArray>();
            decodedBuffer.Init(dracoArray.data(), dracoArray.size());

            std::unique_ptr<draco::Mesh> dracoMesh(new draco::Mesh());
            decoder.DecodeBufferToGeometry(&decodedBuffer, dracoMesh.get());

            // prepare attributes for this mesh
            auto positionAttribute = dracoMesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
            auto normalAttribute = dracoMesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
            auto texCoordAttribute = dracoMesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
            auto extraTexCoordAttribute = dracoMesh->GetAttributeByUniqueId(DRACO_ATTRIBUTE_TEX_COORD_1);
            auto colorAttribute = dracoMesh->GetNamedAttribute(draco::GeometryAttribute::COLOR);
            auto materialIDAttribute = dracoMesh->GetAttributeByUniqueId(DRACO_ATTRIBUTE_MATERIAL_ID);
            auto originalIndexAttribute = dracoMesh->GetAttributeByUniqueId(DRACO_ATTRIBUTE_ORIGINAL_INDEX);

            // setup extracted mesh data structures given number of points
            auto numVertices = dracoMesh->num_points();

            QHash<QPair<int, int>, int> materialTextureParts;

            data.extracted.mesh.vertices.resize(numVertices);

            if (normalAttribute) {
                data.extracted.mesh.normals.resize(numVertices);
            }

            if (texCoordAttribute) {
                data.extracted.mesh.texCoords.resize(numVertices);
            }

            if (extraTexCoordAttribute) {
                data.extracted.mesh.texCoords1.resize(numVertices);
            }

            if (colorAttribute) {
                data.extracted.mesh.colors.resize(numVertices);
            }

            // enumerate the vertices and construct the extracted mesh
            for (int i = 0; i < numVertices; ++i) {
                draco::PointIndex vertexIndex(i);

                if (positionAttribute) {
                    // read position from draco mesh to extracted mesh
                    auto mappedIndex = positionAttribute->mapped_index(vertexIndex);

                    positionAttribute->ConvertValue<float, 3>(mappedIndex,
                                                              reinterpret_cast<float*>(&data.extracted.mesh.vertices[i]));
                }

                if (normalAttribute) {
                    // read normals from draco mesh to extracted mesh
                    auto mappedIndex = normalAttribute->mapped_index(vertexIndex);

                    normalAttribute->ConvertValue<float, 3>(mappedIndex,
                                                            reinterpret_cast<float*>(&data.extracted.mesh.normals[i]));
                }

                if (texCoordAttribute) {
                    // read UVs from draco mesh to extracted mesh
                    auto mappedIndex = texCoordAttribute->mapped_index(vertexIndex);

                    texCoordAttribute->ConvertValue<float, 2>(mappedIndex,
                                                              reinterpret_cast<float*>(&data.extracted.mesh.texCoords[i]));
                }

                if (extraTexCoordAttribute) {
                    // some meshes have a second set of UVs, read those to extracted mesh
                    auto mappedIndex = extraTexCoordAttribute->mapped_index(vertexIndex);

                    extraTexCoordAttribute->ConvertValue<float, 2>(mappedIndex,
                                                                   reinterpret_cast<float*>(&data.extracted.mesh.texCoords1[i]));
                }

                if (colorAttribute) {
                    // read vertex colors from draco mesh to extracted mesh
                    auto mappedIndex = colorAttribute->mapped_index(vertexIndex);

                    colorAttribute->ConvertValue<float, 3>(mappedIndex,
                                                           reinterpret_cast<float*>(&data.extracted.mesh.colors[i]));
                }

                if (originalIndexAttribute) {
                    auto mappedIndex = originalIndexAttribute->mapped_index(vertexIndex);

                    int32_t originalIndex;

                    originalIndexAttribute->ConvertValue<int32_t, 1>(mappedIndex, &originalIndex);

                    data.extracted.newIndices.insert(originalIndex, i);
                } else {
                    data.extracted.newIndices.insert(i, i);
                }
            }

            for (int i = 0; i < dracoMesh->num_faces(); ++i) {
                // grab the material ID and texture ID for this face, if we have it
                auto& dracoFace = dracoMesh->face(draco::FaceIndex(i));
                auto& firstCorner = dracoFace[0];

                uint16_t materialID { 0 };

                if (materialIDAttribute) {
                    // read material ID and texture ID mappings into materials and texture vectors
                    auto mappedIndex = materialIDAttribute->mapped_index(firstCorner);

                    materialIDAttribute->ConvertValue<uint16_t, 1>(mappedIndex, &materialID);
                }

                QPair<int, int> materialTexture(materialID, 0);

                // grab or setup the FBXMeshPart for the part this face belongs to
                int& partIndexPlusOne = materialTextureParts[materialTexture];
                if (partIndexPlusOne == 0) {
                    data.extracted.partMaterialTextures.append(materialTexture);
                    data.extracted.mesh.parts.resize(data.extracted.mesh.parts.size() + 1);
                    partIndexPlusOne = data.extracted.mesh.parts.size();
                }

                // give the mesh part this index
                FBXMeshPart& part = data.extracted.mesh.parts[partIndexPlusOne - 1];
                part.triangleIndices.append(firstCorner.value());
                part.triangleIndices.append(dracoFace[1].value());
                part.triangleIndices.append(dracoFace[2].value());
            }
        }
    }

    // when we have a draco mesh, we've already built the extracted mesh, so we don't need to do the
    // processing we do for normal meshes below
    if (!isDracoMesh) {
        bool isMultiMaterial = false;
        if (isMaterialPerPolygon) {
            isMultiMaterial = true;
        }
        // TODO: make excellent use of isMultiMaterial
        Q_UNUSED(isMultiMaterial);

        // convert the polygons to quads and triangles
        int polygonIndex = 0;
        QHash<QPair<int, int>, int> materialTextureParts;
        for (int beginIndex = 0; beginIndex < data.polygonIndices.size(); polygonIndex++) {
            int endIndex = beginIndex;
            while (endIndex < data.polygonIndices.size() && data.polygonIndices.at(endIndex++) >= 0);

            QPair<int, int> materialTexture((polygonIndex < materials.size()) ? materials.at(polygonIndex) : 0,
                                            (polygonIndex < textures.size()) ? textures.at(polygonIndex) : 0);
            int& partIndex = materialTextureParts[materialTexture];
            if (partIndex == 0) {
                data.extracted.partMaterialTextures.append(materialTexture);
                data.extracted.mesh.parts.resize(data.extracted.mesh.parts.size() + 1);
                partIndex = data.extracted.mesh.parts.size();
            }
            FBXMeshPart& part = data.extracted.mesh.parts[partIndex - 1];

            if (endIndex - beginIndex == 4) {
                appendIndex(data, part.quadIndices, beginIndex++, deduplicate);
                appendIndex(data, part.quadIndices, beginIndex++, deduplicate);
                appendIndex(data, part.quadIndices, beginIndex++, deduplicate);
                appendIndex(data, part.quadIndices, beginIndex++, deduplicate);

                int quadStartIndex = part.quadIndices.size() - 4;
                int i0 = part.quadIndices[quadStartIndex + 0];
                int i1 = part.quadIndices[quadStartIndex + 1];
                int i2 = part.quadIndices[quadStartIndex + 2];
                int i3 = part.quadIndices[quadStartIndex + 3];

                // Sam's recommended triangle slices
                // Triangle tri1 = { v0, v1, v3 };
                // Triangle tri2 = { v1, v2, v3 };
                // NOTE: Random guy on the internet's recommended triangle slices
                // Triangle tri1 = { v0, v1, v2 };
                // Triangle tri2 = { v2, v3, v0 };

                part.quadTrianglesIndices.append(i0);
                part.quadTrianglesIndices.append(i1);
                part.quadTrianglesIndices.append(i3);

                part.quadTrianglesIndices.append(i1);
                part.quadTrianglesIndices.append(i2);
                part.quadTrianglesIndices.append(i3);

            } else {
                for (int nextIndex = beginIndex + 1;; ) {
                    appendIndex(data, part.triangleIndices, beginIndex, deduplicate);
                    appendIndex(data, part.triangleIndices, nextIndex++, deduplicate);
                    appendIndex(data, part.triangleIndices, nextIndex, deduplicate);
                    if (nextIndex >= data.polygonIndices.size() || data.polygonIndices.at(nextIndex) < 0) {
                        break;
                    }
                }
                beginIndex = endIndex;
            }
        }
    }
    
    return data.extracted;
}

glm::vec3 FBXReader::normalizeDirForPacking(const glm::vec3& dir) {
    auto maxCoord = glm::max(fabsf(dir.x), glm::max(fabsf(dir.y), fabsf(dir.z)));
    if (maxCoord > 1e-6f) {
        return dir / maxCoord;
    }
    return dir;
}

void FBXReader::buildModelMesh(FBXMesh& extractedMesh, const QString& url) {
    unsigned int totalSourceIndices = 0;
    foreach(const FBXMeshPart& part, extractedMesh.parts) {
        totalSourceIndices += (part.quadTrianglesIndices.size() + part.triangleIndices.size());
    }

    static int repeatMessageID = LogHandler::getInstance().newRepeatedMessageID();

    if (!totalSourceIndices) {
        HIFI_FCDEBUG_ID(modelformat(), repeatMessageID, "buildModelMesh failed -- no indices, url = " << url);
        return;
    }

    if (extractedMesh.vertices.size() == 0) {
        HIFI_FCDEBUG_ID(modelformat(), repeatMessageID, "buildModelMesh failed -- no vertices, url = " << url);
        return;
    }

    FBXMesh& fbxMesh = extractedMesh;
    graphics::MeshPointer mesh(new graphics::Mesh());

    // Grab the vertices in a buffer
    auto vb = std::make_shared<gpu::Buffer>();
    vb->setData(extractedMesh.vertices.size() * sizeof(glm::vec3),
                (const gpu::Byte*) extractedMesh.vertices.data());
    gpu::BufferView vbv(vb, gpu::Element(gpu::VEC3, gpu::FLOAT, gpu::XYZ));
    mesh->setVertexBuffer(vbv);

    if (!fbxMesh.normals.empty() && fbxMesh.tangents.empty()) {
        // Fill with a dummy value to force tangents to be present if there are normals
        fbxMesh.tangents.reserve(fbxMesh.normals.size());
        std::fill_n(std::back_inserter(fbxMesh.tangents), fbxMesh.normals.size(), Vectors::UNIT_X);
    }
    // Same thing with blend shapes
    for (auto& blendShape : fbxMesh.blendshapes) {
        if (!blendShape.normals.empty() && blendShape.tangents.empty()) {
            // Fill with a dummy value to force tangents to be present if there are normals
            blendShape.tangents.reserve(blendShape.normals.size());
            std::fill_n(std::back_inserter(blendShape.tangents), blendShape.normals.size(), Vectors::UNIT_X);
        }
    }

    // evaluate all attribute channels sizes
    const int normalsSize = fbxMesh.normals.size() * sizeof(NormalType);
    const int tangentsSize = fbxMesh.tangents.size() * sizeof(NormalType);
    // If there are normals then there should be tangents
    
    assert(normalsSize <= tangentsSize);
    if (tangentsSize > normalsSize) {
        qWarning() << "Unexpected tangents in " << url;
    }
    const auto normalsAndTangentsSize = normalsSize + tangentsSize;
    const int normalsAndTangentsStride = 2 * sizeof(NormalType);
    const int colorsSize = fbxMesh.colors.size() * sizeof(ColorType);
    // Texture coordinates are stored in 2 half floats
    const int texCoordsSize = fbxMesh.texCoords.size() * sizeof(vec2h);
    const int texCoords1Size = fbxMesh.texCoords1.size() * sizeof(vec2h);

    int clusterIndicesSize = fbxMesh.clusterIndices.size() * sizeof(uint8_t);
    if (fbxMesh.clusters.size() > UINT8_MAX) {
        // we need 16 bits instead of just 8 for clusterIndices
        clusterIndicesSize *= 2;
    }

    const int clusterWeightsSize = fbxMesh.clusterWeights.size() * sizeof(uint16_t);

    // Normals and tangents are interleaved
    const int normalsOffset = 0;
    const int tangentsOffset = normalsOffset + sizeof(NormalType);
    const int colorsOffset = normalsOffset + normalsSize + tangentsSize;
    const int texCoordsOffset = colorsOffset + colorsSize;
    const int texCoords1Offset = texCoordsOffset + texCoordsSize;
    const int clusterIndicesOffset = texCoords1Offset + texCoords1Size;
    const int clusterWeightsOffset = clusterIndicesOffset + clusterIndicesSize;
    const int totalAttributeSize = clusterWeightsOffset + clusterWeightsSize;

    // Copy all attribute data in a single attribute buffer
    auto attribBuffer = std::make_shared<gpu::Buffer>();
    attribBuffer->resize(totalAttributeSize);

    // Interleave normals and tangents
    if (normalsSize > 0) {
        std::vector<NormalType> normalsAndTangents;

        normalsAndTangents.reserve(fbxMesh.normals.size() + fbxMesh.tangents.size());
        for (auto normalIt = fbxMesh.normals.constBegin(), tangentIt = fbxMesh.tangents.constBegin();
             normalIt != fbxMesh.normals.constEnd();
             ++normalIt, ++tangentIt) {
#if FBX_PACK_NORMALS
            const auto normal = normalizeDirForPacking(*normalIt);
            const auto tangent = normalizeDirForPacking(*tangentIt);
            const auto packedNormal = glm::packSnorm3x10_1x2(glm::vec4(normal, 0.0f));
            const auto packedTangent = glm::packSnorm3x10_1x2(glm::vec4(tangent, 0.0f));
#else
            const auto packedNormal = *normalIt;
            const auto packedTangent = *tangentIt;
#endif
            normalsAndTangents.push_back(packedNormal);
            normalsAndTangents.push_back(packedTangent);
        }
        attribBuffer->setSubData(normalsOffset, normalsAndTangentsSize, (const gpu::Byte*) normalsAndTangents.data());
    }

    if (colorsSize > 0) {
#if FBX_PACK_COLORS
        std::vector<ColorType> colors;

        colors.reserve(fbxMesh.colors.size());
        for (const auto& color : fbxMesh.colors) {
            colors.push_back(glm::packUnorm4x8(glm::vec4(color, 1.0f)));
        }
        attribBuffer->setSubData(colorsOffset, colorsSize, (const gpu::Byte*) colors.data());
#else
        attribBuffer->setSubData(colorsOffset, colorsSize, (const gpu::Byte*) fbxMesh.colors.constData());
#endif
    }

    if (texCoordsSize > 0) {
        QVector<vec2h> texCoordData;
        texCoordData.reserve(fbxMesh.texCoords.size());
        for (auto& texCoordVec2f : fbxMesh.texCoords) {
            vec2h texCoordVec2h;

            texCoordVec2h.x = glm::detail::toFloat16(texCoordVec2f.x);
            texCoordVec2h.y = glm::detail::toFloat16(texCoordVec2f.y);
            texCoordData.push_back(texCoordVec2h);
        }
        attribBuffer->setSubData(texCoordsOffset, texCoordsSize, (const gpu::Byte*) texCoordData.constData());
    }

    if (texCoords1Size > 0) {
        QVector<vec2h> texCoordData;
        texCoordData.reserve(fbxMesh.texCoords1.size());
        for (auto& texCoordVec2f : fbxMesh.texCoords1) {
            vec2h texCoordVec2h;

            texCoordVec2h.x = glm::detail::toFloat16(texCoordVec2f.x);
            texCoordVec2h.y = glm::detail::toFloat16(texCoordVec2f.y);
            texCoordData.push_back(texCoordVec2h);
        }
        attribBuffer->setSubData(texCoords1Offset, texCoords1Size, (const gpu::Byte*) texCoordData.constData());
    }

    if (fbxMesh.clusters.size() < UINT8_MAX) {
        // yay! we can fit the clusterIndices within 8-bits
        int32_t numIndices = fbxMesh.clusterIndices.size();
        QVector<uint8_t> clusterIndices;
        clusterIndices.resize(numIndices);
        for (int32_t i = 0; i < numIndices; ++i) {
            assert(fbxMesh.clusterIndices[i] <= UINT8_MAX);
            clusterIndices[i] = (uint8_t)(fbxMesh.clusterIndices[i]);
        }
        attribBuffer->setSubData(clusterIndicesOffset, clusterIndicesSize, (const gpu::Byte*) clusterIndices.constData());
    } else {
        attribBuffer->setSubData(clusterIndicesOffset, clusterIndicesSize, (const gpu::Byte*) fbxMesh.clusterIndices.constData());
    }
    attribBuffer->setSubData(clusterWeightsOffset, clusterWeightsSize, (const gpu::Byte*) fbxMesh.clusterWeights.constData());

    if (normalsSize) {
        mesh->addAttribute(gpu::Stream::NORMAL,
                           graphics::BufferView(attribBuffer, normalsOffset, normalsAndTangentsSize,
                           normalsAndTangentsStride, FBX_NORMAL_ELEMENT));
        mesh->addAttribute(gpu::Stream::TANGENT,
                           graphics::BufferView(attribBuffer, tangentsOffset, normalsAndTangentsSize,
                           normalsAndTangentsStride, FBX_NORMAL_ELEMENT));
    }
    if (colorsSize) {
        mesh->addAttribute(gpu::Stream::COLOR,
                           graphics::BufferView(attribBuffer, colorsOffset, colorsSize, FBX_COLOR_ELEMENT));
    }
    if (texCoordsSize) {
        mesh->addAttribute(gpu::Stream::TEXCOORD,
                           graphics::BufferView( attribBuffer, texCoordsOffset, texCoordsSize,
                           gpu::Element(gpu::VEC2, gpu::HALF, gpu::UV)));
    }
    if (texCoords1Size) {
        mesh->addAttribute( gpu::Stream::TEXCOORD1,
                            graphics::BufferView(attribBuffer, texCoords1Offset, texCoords1Size,
                            gpu::Element(gpu::VEC2, gpu::HALF, gpu::UV)));
    } else if (texCoordsSize) {
        mesh->addAttribute(gpu::Stream::TEXCOORD1,
                           graphics::BufferView(attribBuffer, texCoordsOffset, texCoordsSize,
                           gpu::Element(gpu::VEC2, gpu::HALF, gpu::UV)));
    }

    if (clusterIndicesSize) {
        if (fbxMesh.clusters.size() < UINT8_MAX) {
            mesh->addAttribute(gpu::Stream::SKIN_CLUSTER_INDEX,
                               graphics::BufferView(attribBuffer, clusterIndicesOffset, clusterIndicesSize,
                                                 gpu::Element(gpu::VEC4, gpu::UINT8, gpu::XYZW)));
        } else {
            mesh->addAttribute(gpu::Stream::SKIN_CLUSTER_INDEX,
                               graphics::BufferView(attribBuffer, clusterIndicesOffset, clusterIndicesSize,
                                                 gpu::Element(gpu::VEC4, gpu::UINT16, gpu::XYZW)));
        }
    }
    if (clusterWeightsSize) {
        mesh->addAttribute(gpu::Stream::SKIN_CLUSTER_WEIGHT,
                          graphics::BufferView(attribBuffer, clusterWeightsOffset, clusterWeightsSize,
                                            gpu::Element(gpu::VEC4, gpu::NUINT16, gpu::XYZW)));
    }


    unsigned int totalIndices = 0;
    foreach(const FBXMeshPart& part, extractedMesh.parts) {
        totalIndices += (part.quadTrianglesIndices.size() + part.triangleIndices.size());
    }

    if (! totalIndices) {
        qCDebug(modelformat) << "buildModelMesh failed -- no indices, url = " << url;
        return;
    }

    auto indexBuffer = std::make_shared<gpu::Buffer>();
    indexBuffer->resize(totalIndices * sizeof(int));

    int indexNum = 0;
    int offset = 0;

    std::vector< graphics::Mesh::Part > parts;
    if (extractedMesh.parts.size() > 1) {
        indexNum = 0;
    }
    foreach(const FBXMeshPart& part, extractedMesh.parts) {
        graphics::Mesh::Part modelPart(indexNum, 0, 0, graphics::Mesh::TRIANGLES);
        
        if (part.quadTrianglesIndices.size()) {
            indexBuffer->setSubData(offset,
                            part.quadTrianglesIndices.size() * sizeof(int),
                            (gpu::Byte*) part.quadTrianglesIndices.constData());
            offset += part.quadTrianglesIndices.size() * sizeof(int);
            indexNum += part.quadTrianglesIndices.size();
            modelPart._numIndices += part.quadTrianglesIndices.size();
        }

        if (part.triangleIndices.size()) {
            indexBuffer->setSubData(offset,
                            part.triangleIndices.size() * sizeof(int),
                            (gpu::Byte*) part.triangleIndices.constData());
            offset += part.triangleIndices.size() * sizeof(int);
            indexNum += part.triangleIndices.size();
            modelPart._numIndices += part.triangleIndices.size();
        }

        parts.push_back(modelPart);
    }

    gpu::BufferView indexBufferView(indexBuffer, gpu::Element(gpu::SCALAR, gpu::UINT32, gpu::XYZ));
    mesh->setIndexBuffer(indexBufferView);

    if (parts.size()) {
        auto pb = std::make_shared<gpu::Buffer>();
        pb->setData(parts.size() * sizeof(graphics::Mesh::Part), (const gpu::Byte*) parts.data());
        gpu::BufferView pbv(pb, gpu::Element(gpu::VEC4, gpu::UINT32, gpu::XYZW));
        mesh->setPartBuffer(pbv);
    } else {
        qCDebug(modelformat) << "buildModelMesh failed -- no parts, url = " << url;
        return;
    }

    // graphics::Box box =
    mesh->evalPartBound(0);

    extractedMesh._mesh = mesh;
}
