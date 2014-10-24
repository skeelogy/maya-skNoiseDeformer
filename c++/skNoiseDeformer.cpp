/*
 * Author: Skeel Lee
 * Contact: skeel@skeelogy.com
 * Since: 1 Jun 2014
 *
 * A noise deformer plugin for Maya. It deforms meshes using fBm (fractional
 * Brownian motion) which adds up multiple layers of Simplex noises.
 *
 * ---------Compiling-------------
 *
 * To compile the C++ plugin in Linux, you need to use a specific gcc compiler
 * version based on the Maya version that you are using. For example, for Maya
 * 2014, gcc 4.1.2 is required. You can follow the steps in the Maya 2014 Linux
 * compiler requirement documentation
 * (http://docs.autodesk.com/MAYAUL/2014/ENU/Maya-API-Documentation/index.html?
 * url=files/Shapes.htm,topicNumber=d30e14674) to get that installed.
 *
 * The makefile provided here is for Maya 2014 and uses g++412. If you are
 * using another version of Maya, you might need to edit the makefile
 * accordingly.
 *
 * Once the correct version of gcc is installed on your Linux system, you can
 * execute the following command in a terminal to compile the plugin:
 *
 *     > make
 *
 * A skNoiseDeformer.so file will be created in the bin sub-directory. This is
 * the actual compiled plugin. You can copy it manually to a suitable
 * installation folder, or you can run this in the same terminal:
 *
 *     > make install MAYA_VERSION=2014-x64
 *
 * which will install the skNoiseDeformer.so file to a plugin directory in the
 * user maya directory (the one where preferences are stored).
 *
 * You can also compile the debug version of the plugin if necessary:
 *
 *     > make BUILD=debug
 *
 * which will produce a skNoiseDeformer_d.so plugin.
 *
 * You can also install this debug plugin in a similar fashion:
 *
 *     > make install MAYA_VERSION=2014-x64 BUILD=debug
 *
 * If you need to clean the project, you can do it using the usual way:
 *
 *     > make clean
 *
 * ---------Usage-------------
 *
 * 1) Load the plugin, either using the Plug-in Manager or using the following
 * MEL command:
 *
 *     loadPlugin "skNoiseDeformer.so"
 *
 * 2) Select a mesh
 *
 * 3) Attach a new noise deformer to the mesh by executing the following MEL
 * command:
 *
 *     deformer -type skNoiseDeformer
 *
 * 4) Adjust the noise attributes (e.g. amplitude, frequency, octaves,
 * lacunarity) in the channel box accordingly
 *
 * 5) Move/rotate/scale the accessory locator to transform the noise space, as
 * desired
 *
 * ---------Credits-------------
 *
 * This plugin uses the noise library from Casey Duncan:
 * https://github.com/caseman/noise
 *
 * ---------License-------------
 *
 * Released under The MIT License (MIT) Copyright (c) 2014 Skeel Lee
 * (http://cg.skeelogy.com)
 *
 */

#include <maya/MFnPlugin.h>
#include <maya/MTypeId.h>
#include <maya/MGlobal.h>

#include <maya/MPxDeformerNode.h>
#include <maya/MItGeometry.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnMatrixAttribute.h>

#include <maya/MPoint.h>
#include <maya/MMatrix.h>

#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>

#include "libnoise/_simplex.c"

#include "skNoiseDeformer.h"

MString nodeType("skNoiseDeformer");
MString nodeVersion("1.0.1");
MTypeId SkNoiseDeformer::nodeId(0x001212C0); //unique id obtained from ADN

#define CHECK_ERROR(stat, msg) \
    if (!stat) { \
        MString errorMsg("[" + nodeType + "] " + __FILE__ + " " + __LINE__ + ": " msg); \
        MGlobal::displayError(errorMsg); \
        cerr << errorMsg << endl; \
        return MS::kFailure; \
    }

const float EPSILON = 0.0000001;

MObject SkNoiseDeformer::amp;
MObject SkNoiseDeformer::freq;
MObject SkNoiseDeformer::offset;
MObject SkNoiseDeformer::octaves;
MObject SkNoiseDeformer::lacunarity;
MObject SkNoiseDeformer::persistence;
MObject SkNoiseDeformer::locatorWorldSpace;

//main deform method
MStatus SkNoiseDeformer::deform(MDataBlock& dataBlock,
                                MItGeometry& geomIter,
                                const MMatrix& localToWorldMat,
                                unsigned int multiIndex)
{
    MStatus stat = MS::kSuccess;

    //get envelope value, return if sufficiently near to 0
    MDataHandle envDataHandle = dataBlock.inputValue(envelope, &stat);
    CHECK_ERROR(stat, "Unable to get envelope data handle\n");
    float env = envDataHandle.asFloat();
    if (EPSILON >= env)
    {
        return stat;
    }

    //get attribute values
    MDataHandle ampDataHandle = dataBlock.inputValue(amp, &stat);
    CHECK_ERROR(stat, "Unable to get amplitude data handle\n");
    float *amps = ampDataHandle.asFloat3();

    MDataHandle freqDataHandle = dataBlock.inputValue(freq, &stat);
    CHECK_ERROR(stat, "Unable to get frequency data handle\n");
    float *freqs = freqDataHandle.asFloat3();

    MDataHandle offsetDataHandle = dataBlock.inputValue(offset, &stat);
    CHECK_ERROR(stat, "Unable to get offset data handle\n");
    float *offsets = offsetDataHandle.asFloat3();

    MDataHandle octavesDataHandle = dataBlock.inputValue(octaves, &stat);
    CHECK_ERROR(stat, "Unable to get octaves data handle\n");
    int octaves = octavesDataHandle.asInt();

    MDataHandle lacunarityDataHandle = dataBlock.inputValue(lacunarity, &stat);
    CHECK_ERROR(stat, "Unable to get lacunarity data handle\n");
    float lacunarity = lacunarityDataHandle.asFloat();

    MDataHandle persistenceDataHandle = dataBlock.inputValue(persistence, &stat);
    CHECK_ERROR(stat, "Unable to get persistence data handle\n");
    float persistence = persistenceDataHandle.asFloat();

    MDataHandle locatorWorldSpaceDataHandle = dataBlock.inputValue(locatorWorldSpace, &stat);
    CHECK_ERROR(stat, "Unable to get locatorWorldSpace data handle\n");
    MMatrix locatorWorldSpaceMat = locatorWorldSpaceDataHandle.asMatrix();

    //precompute some transformation matrices
    MMatrix localToLocatorSpaceMat = localToWorldMat * locatorWorldSpaceMat.inverse();
    MMatrix locatorToLocalSpaceMat = locatorWorldSpaceMat * localToWorldMat.inverse();

    //iterate through all the points
    float weight;
    MPoint pos;
    float noiseInput[3];
    float envTimesWeight;
    for (geomIter.reset(); !geomIter.isDone(); geomIter.next())
    {
        //get weight value for this point, continue if sufficiently near to 0
        weight = weightValue(dataBlock, multiIndex, geomIter.index());
        if (EPSILON >= weight)
        {
            continue;
        }

        //get locator space position
        pos = geomIter.position();
        pos *= localToLocatorSpaceMat;

        //precompute some values
        noiseInput[0] = freqs[0] * pos.x - offsets[0];
        noiseInput[1] = freqs[1] * pos.y - offsets[1];
        noiseInput[2] = freqs[2] * pos.z - offsets[2];
        envTimesWeight = env * weight;

        //calculate new position
        pos.x += amps[0] * fbm_noise3(noiseInput[0], noiseInput[1], noiseInput[2], octaves, persistence, lacunarity) * envTimesWeight;
        pos.y += amps[1] * fbm_noise3(noiseInput[0] + 123, noiseInput[1] + 456, noiseInput[2] + 789, octaves, persistence, lacunarity) * envTimesWeight;
        pos.z += amps[2] * fbm_noise3(noiseInput[0] + 234, noiseInput[1] + 567, noiseInput[2] + 890, octaves, persistence, lacunarity) * envTimesWeight;

        //convert back to local space
        pos *= locatorToLocalSpaceMat;

        //set new position
        geomIter.setPosition(pos);
    }

    return stat;
}

//accessory locator setup method
MStatus SkNoiseDeformer::accessoryNodeSetup(MDagModifier& dagMod)
{
    MStatus stat = MS::kSuccess;

    MObject thisObj = thisMObject();

    //get current object name
    MFnDependencyNode thisFn(thisObj);
    MString thisObjName = thisFn.name(&stat);
    CHECK_ERROR(stat, "Unable to get the name of this deformer node\n");

    //create an accessory locator for user to manipulate a local deformation space
    MObject locObj = dagMod.createNode("locator", MObject::kNullObj, &stat);
    CHECK_ERROR(stat, "Unable to create locator node\n");
    stat = dagMod.doIt();
    CHECK_ERROR(stat, "Unable to execute DAG modifications for creating locator\n");

    //rename transform and shape nodes
    stat = dagMod.renameNode(locObj, thisObjName + "_loc");
    CHECK_ERROR(stat, "Unable to rename locator transform node\n");
    MDagPath locDagPath;
    MFnDagNode locDagFn(locObj);
    stat = locDagFn.getPath(locDagPath);
    CHECK_ERROR(stat, "Unable to get DAG path of locator\n");
    stat = locDagPath.extendToShape();
    CHECK_ERROR(stat, "Unable to get shape DAG path from given DAG path\n");
    MObject locShapeObj = locDagPath.node(&stat);
    CHECK_ERROR(stat, "Unable to get MObject from given locator DAG path\n");
    stat = dagMod.renameNode(locShapeObj, thisObjName + "_locShape");
    CHECK_ERROR(stat, "Unable to rename locator shape node\n");

    //connect locator's worldMatrix to locatorWorldSpace
    MFnDependencyNode locFn(locObj);
    MObject worldMatrixAttr = locFn.attribute("worldMatrix", &stat);
    CHECK_ERROR(stat, "Unable to get worldMatrix attribute for locator\n");
    stat = dagMod.connect(locObj, worldMatrixAttr, thisObj, locatorWorldSpace);
    CHECK_ERROR(stat, "Unable to connect locator worldMatrix to deformer locatorWorldSpace\n");

    return stat;
}

//accessory attribute method
MObject& SkNoiseDeformer::accessoryAttribute() const
{
    return locatorWorldSpace;
}

//creator method
void* SkNoiseDeformer::creator()
{
    return new SkNoiseDeformer();
}

//init method
MStatus SkNoiseDeformer::initialize()
{
    MStatus stat = MS::kSuccess;

    MFnNumericAttribute nAttr;
    MFnMatrixAttribute mAttr;

    //amplitude attr
    amp = nAttr.createPoint("amplitude", "amp", &stat);
    CHECK_ERROR(stat, "Unable to create amplitude attribute\n");
    nAttr.setDefault(1.0, 1.0, 1.0);
    nAttr.setKeyable(true);
    stat = addAttribute(amp);
    CHECK_ERROR(stat, "Unable to add amplitude attribute\n");
    stat = attributeAffects(SkNoiseDeformer::amp, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from amp to outputGeom");

    //frequency attr
    freq = nAttr.createPoint("frequency", "freq", &stat);
    CHECK_ERROR(stat, "Unable to create frequency attribute\n");
    nAttr.setDefault(1.0, 1.0, 1.0);
    nAttr.setKeyable(true);
    stat = addAttribute(freq);
    CHECK_ERROR(stat, "Unable to add frequency attribute\n");
    stat = attributeAffects(SkNoiseDeformer::freq, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from freq to outputGeom");

    //offset attr
    offset = nAttr.createPoint("offset", "off", &stat);
    CHECK_ERROR(stat, "Unable to create offset attribute\n");
    nAttr.setDefault(0.0, 0.0, 0.0);
    nAttr.setKeyable(true);
    stat = addAttribute(offset);
    CHECK_ERROR(stat, "Unable to add offset attribute\n");
    stat = attributeAffects(SkNoiseDeformer::offset, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from offset to outputGeom");

    //octaves attr
    octaves = nAttr.create("octaves", "oct", MFnNumericData::kInt, 1, &stat);
    CHECK_ERROR(stat, "Unable to create octaves attribute\n");
    nAttr.setMin(1);
    nAttr.setKeyable(true);
    stat = addAttribute(octaves);
    CHECK_ERROR(stat, "Unable to add octaves attribute\n");
    stat = attributeAffects(SkNoiseDeformer::octaves, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from octaves to outputGeom");

    //lacunarity attr
    lacunarity = nAttr.create("lacunarity", "lac", MFnNumericData::kFloat, 2.0, &stat);
    CHECK_ERROR(stat, "Unable to create lacunarity attribute\n");
    nAttr.setKeyable(true);
    stat = addAttribute(lacunarity);
    CHECK_ERROR(stat, "Unable to add lacunarity attribute\n");
    stat = attributeAffects(SkNoiseDeformer::lacunarity, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from lacunarity to outputGeom");

    //persistence attr
    persistence = nAttr.create("persistence", "per", MFnNumericData::kFloat, 0.5, &stat);
    CHECK_ERROR(stat, "Unable to create persistence attribute\n");
    nAttr.setKeyable(true);
    stat = addAttribute(persistence);
    CHECK_ERROR(stat, "Unable to add persistence attribute\n");
    stat = attributeAffects(SkNoiseDeformer::persistence, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from persistence to outputGeom");

    //locatorWorldSpace attr
    locatorWorldSpace = mAttr.create("locatorWorldSpace", "locsp", MFnMatrixAttribute::kDouble, &stat);
    CHECK_ERROR(stat, "Unable to create locatorWorldSpace attribute\n");
    mAttr.setStorable(false);
    mAttr.setHidden(true);
    stat = addAttribute(locatorWorldSpace);
    CHECK_ERROR(stat, "Unable to add locatorWorldSpace attribute\n");
    stat = attributeAffects(SkNoiseDeformer::locatorWorldSpace, SkNoiseDeformer::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from locatorWorldSpace to outputGeom");

    return stat;
}

//init plugin
MStatus initializePlugin(MObject obj)
{
    MStatus stat;
    MFnPlugin plugin(obj, "Skeel Lee", nodeVersion.asChar(), "Any");
    stat = plugin.registerNode(nodeType, SkNoiseDeformer::nodeId, SkNoiseDeformer::creator, SkNoiseDeformer::initialize, MPxNode::kDeformerNode);
    CHECK_ERROR(stat, "Failed to register node: " + nodeType + "\n")
    return stat;
}

//uninit plugin
MStatus uninitializePlugin(MObject obj)
{
    MStatus stat;
    MFnPlugin plugin(obj);
    stat = plugin.deregisterNode(SkNoiseDeformer::nodeId);
    CHECK_ERROR(stat, "Failed to register node: " + nodeType + "\n")
    return stat;
}
