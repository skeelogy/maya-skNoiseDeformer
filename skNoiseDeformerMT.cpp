/*
 * Author: Skeel Lee
 * Contact: skeel@skeelogy.com
 * Since: 1 Jun 2014
 *
 * A multi-threaded noise deformer plugin for Maya. It deforms meshes using fBm
 * (fractional Brownian motion) which adds up multiple layers of Simplex
 * noises.
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
 *     > make TYPE=MT
 *
 * A skNoiseDeformerMT.so file will be created in the bin sub-directory. This is
 * the actual compiled plugin. You can copy it manually to a suitable
 * installation folder, or you can run this in the same terminal:
 *
 *     > make install MAYA_VERSION=2014-x64 TYPE=MT
 *
 * which will install the skNoiseDeformerMT.so file to a plugin directory in the
 * user maya directory (the one where preferences are stored).
 *
 * You can also compile the debug version of the plugin if necessary:
 *
 *     > make TYPE=MT BUILD=debug
 *
 * which will produce a skNoiseDeformerMT_d.so plugin.
 *
 * You can also install this debug plugin in a similar fashion:
 *
 *     > make install MAYA_VERSION=2014-x64 TYPE=MT BUILD=debug
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
 *     loadPlugin "skNoiseDeformerMT.so"
 *
 * 2) Select a mesh
 *
 * 3) Attach a new noise deformer to the mesh by executing the following MEL
 * command:
 *
 *     deformer -type skNoiseDeformerMT
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

#include <cmath>
#include <stdlib.h>

#include <maya/MFnPlugin.h>
#include <maya/MTypeId.h>
#include <maya/MGlobal.h>

#include <maya/MPxDeformerNode.h>
#include <maya/MItGeometry.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MPlug.h>

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnMatrixAttribute.h>

#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MMatrix.h>
#include <maya/MFloatArray.h>

#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>

#include <maya/MThreadPool.h>

#include "libnoise/_simplex.c"

#include "skNoiseDeformerMT.h"

MString nodeType("skNoiseDeformerMT");
MString nodeVersion("1.0");
MTypeId SkNoiseDeformerMT::nodeId(0x001212C2); //unique id obtained from ADN

#define CHECK_ERROR(stat, msg) \
    if (!stat) { \
        MString errorMsg("[" + nodeType + "] " + __FILE__ + " " + __LINE__ + ": " msg); \
        MGlobal::displayError(errorMsg); \
        cerr << errorMsg << endl; \
        return MS::kFailure; \
    }

#define CHECK_ERROR_NO_RETURN(stat, msg) \
    if (!stat) { \
        MString errorMsg("[" + nodeType + "] " + __FILE__ + " " + __LINE__ + ": " msg); \
        MGlobal::displayError(errorMsg); \
        cerr << errorMsg << endl; \
    }

const float EPSILON = 0.0000001;

typedef struct
{
    int start;
    int end;
    MPointArray *points;
    MFloatArray *weights;
    float env;
    int width;
    int numTasks;
    float *amps;
    float *freqs;
    float *offsets;
    int octaves;
    float lacunarity;
    float persistence;
    MMatrix *localToLocatorSpaceMat;
    MMatrix *locatorToLocalSpaceMat;
} SharedData;

typedef struct
{
    int id;
    SharedData *sharedData;
} ThreadData;

MObject SkNoiseDeformerMT::numTasks;
MObject SkNoiseDeformerMT::amp;
MObject SkNoiseDeformerMT::freq;
MObject SkNoiseDeformerMT::offset;
MObject SkNoiseDeformerMT::octaves;
MObject SkNoiseDeformerMT::lacunarity;
MObject SkNoiseDeformerMT::persistence;
MObject SkNoiseDeformerMT::locatorWorldSpace;

//constructor
SkNoiseDeformerMT::SkNoiseDeformerMT()
{
    //init thread pool
    cerr << "[" << nodeType << "] Initializing thread pool" << endl;
    MStatus stat = MThreadPool::init();
    CHECK_ERROR_NO_RETURN(stat, "Unable to create thread pool\n");
}

//destructor
SkNoiseDeformerMT::~SkNoiseDeformerMT()
{
    //release thread pool
    cerr << "[" << nodeType << "] Releasing thread pool" << endl;
    MThreadPool::release();
}

//main task method for a single thread
MThreadRetVal threadTask(void* data)
{
    ThreadData *threadData = static_cast<ThreadData*>(data);

    //store local variables
    const SharedData *sharedData = threadData->sharedData;
    const int sharedStart = sharedData->start;
    const int sharedEnd = sharedData->end;
    MPointArray *sharedPoints = sharedData->points;
    const MFloatArray *sharedWeights = sharedData->weights;
    const float sharedEnv= sharedData->env;
    const int sharedWidth = sharedData->width;
    const float *sharedAmps = sharedData->amps;
    const float *sharedFreqs = sharedData->freqs;
    const float *sharedOffsets = sharedData->offsets;
    const int sharedOctaves = sharedData->octaves;
    const float sharedLacunarity = sharedData->lacunarity;
    const float sharedPersistence = sharedData->persistence;
    const MMatrix *sharedLocalToLocatorSpaceMat = sharedData->localToLocatorSpaceMat;
    const MMatrix *sharedLocatorToLocalSpaceMat = sharedData->locatorToLocalSpaceMat;

    //get range of ids to work on
    const int threadStartId = sharedStart + threadData->id * sharedWidth;
    const int threadEndId = threadStartId + sharedWidth;

    //iterate through points within the range
    float noiseInput[3];
    float envTimesWeight;
    MPoint *pos;
    int i;
    for (i = threadStartId; i < threadEndId && i <= sharedEnd; ++i)
    {
        //get locator space position
        pos = &((*sharedPoints)[i]);
        *pos *= *sharedLocalToLocatorSpaceMat;

        //precompute some values
        noiseInput[0] = sharedFreqs[0] * pos->x - sharedOffsets[0];
        noiseInput[1] = sharedFreqs[1] * pos->y - sharedOffsets[1];
        noiseInput[2] = sharedFreqs[2] * pos->z - sharedOffsets[2];
        envTimesWeight = sharedEnv * (*sharedWeights)[i];

        //calculate new position
        pos->x += sharedAmps[0] * fbm_noise3(noiseInput[0], noiseInput[1], noiseInput[2], sharedOctaves, sharedPersistence, sharedLacunarity) * envTimesWeight;
        pos->y += sharedAmps[1] * fbm_noise3(noiseInput[0] + 123, noiseInput[1] + 456, noiseInput[2] + 789, sharedOctaves, sharedPersistence, sharedLacunarity) * envTimesWeight;
        pos->z += sharedAmps[2] * fbm_noise3(noiseInput[0] + 234, noiseInput[1] + 567, noiseInput[2] + 890, sharedOctaves, sharedPersistence, sharedLacunarity) * envTimesWeight;

        //convert back to local space
        *pos *= *sharedLocatorToLocalSpaceMat;
    }

    return static_cast<MThreadRetVal>(0);
}

//task creation and execution method
void createTasksAndExecute(void* data, MThreadRootTask* root)
{
    SharedData *sharedData = static_cast<SharedData*>(data);

    //store local variables
    const int numTasks = sharedData->numTasks;

    //create array to store thread data for each task
    ThreadData *threadData = new ThreadData[numTasks];

    //create tasks
    int i;
    for (i = 0; i < numTasks; ++i)
    {
        threadData[i].id = i;
        threadData[i].sharedData = sharedData;
        MThreadPool::createTask(threadTask, static_cast<void*>(&threadData[i]), root);
    }

    //execute tasks in parallel region and wait for all to finish
    MThreadPool::executeAndJoin(root);

    //delete array memory
    delete [] threadData;
}

//main deform method
MStatus SkNoiseDeformerMT::deform(MDataBlock& dataBlock,
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
    MDataHandle numTasksDataHandle = dataBlock.inputValue(numTasks, &stat);
    CHECK_ERROR(stat, "Unable to get numTasks data handle\n");
    int numTasks = numTasksDataHandle.asInt();

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

    //read all points
    MPointArray points;
    geomIter.allPositions(points);
    int numPoints = points.length();

    //store all weight values
    MFloatArray weights(numPoints);
    int index;
    for (geomIter.reset(); !geomIter.isDone(); geomIter.next())
    {
        index = geomIter.index();
        weights.set(weightValue(dataBlock, multiIndex, index), index);
    }

    //pack data into a struct
    SharedData sharedData;
    sharedData.start = 0;
    sharedData.end = points.length() - 1;
    sharedData.points = &points;
    sharedData.weights = &weights;
    sharedData.numTasks = numTasks;
    sharedData.width = static_cast<int>(std::ceil((sharedData.end - sharedData.start + 1.0) / numTasks));
    sharedData.env = env;
    sharedData.amps = amps;
    sharedData.freqs = freqs;
    sharedData.offsets = offsets;
    sharedData.octaves = octaves;
    sharedData.lacunarity = lacunarity;
    sharedData.persistence = persistence;
    sharedData.localToLocatorSpaceMat = &localToLocatorSpaceMat;
    sharedData.locatorToLocalSpaceMat = &locatorToLocalSpaceMat;

    //create new parallel region and start off the multi-threading functions
    MThreadPool::newParallelRegion(createTasksAndExecute, static_cast<void*>(&sharedData));

    //set all points
    geomIter.setAllPositions(points);

    return stat;
}

//accessory locator setup method
MStatus SkNoiseDeformerMT::accessoryNodeSetup(MDagModifier& dagMod)
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
MObject& SkNoiseDeformerMT::accessoryAttribute() const
{
    return locatorWorldSpace;
}

//creator method
void* SkNoiseDeformerMT::creator()
{
    return new SkNoiseDeformerMT();
}

//init method
MStatus SkNoiseDeformerMT::initialize()
{
    MStatus stat = MS::kSuccess;

    MFnNumericAttribute nAttr;
    MFnMatrixAttribute mAttr;

    //numTasks attr
    numTasks = nAttr.create("numTasks", "nt", MFnNumericData::kInt, 16, &stat);
    CHECK_ERROR(stat, "Unable to create numTasks attribute\n");
    nAttr.setMin(1);
    stat = addAttribute(numTasks);
    CHECK_ERROR(stat, "Unable to add numTasks attribute\n");

    //amplitude attr
    amp = nAttr.createPoint("amplitude", "amp", &stat);
    CHECK_ERROR(stat, "Unable to create amplitude attribute\n");
    nAttr.setDefault(1.0, 1.0, 1.0);
    nAttr.setKeyable(true);
    stat = addAttribute(amp);
    CHECK_ERROR(stat, "Unable to add amplitude attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::amp, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from amp to outputGeom");

    //frequency attr
    freq = nAttr.createPoint("frequency", "freq", &stat);
    CHECK_ERROR(stat, "Unable to create frequency attribute\n");
    nAttr.setDefault(1.0, 1.0, 1.0);
    nAttr.setKeyable(true);
    stat = addAttribute(freq);
    CHECK_ERROR(stat, "Unable to add frequency attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::freq, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from freq to outputGeom");

    //offset attr
    offset = nAttr.createPoint("offset", "off", &stat);
    CHECK_ERROR(stat, "Unable to create offset attribute\n");
    nAttr.setDefault(0.0, 0.0, 0.0);
    nAttr.setKeyable(true);
    stat = addAttribute(offset);
    CHECK_ERROR(stat, "Unable to add offset attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::offset, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from offset to outputGeom");

    //octaves attr
    octaves = nAttr.create("octaves", "oct", MFnNumericData::kInt, 1, &stat);
    CHECK_ERROR(stat, "Unable to create octaves attribute\n");
    nAttr.setMin(1);
    nAttr.setKeyable(true);
    stat = addAttribute(octaves);
    CHECK_ERROR(stat, "Unable to add octaves attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::octaves, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from octaves to outputGeom");

    //lacunarity attr
    lacunarity = nAttr.create("lacunarity", "lac", MFnNumericData::kFloat, 2.0, &stat);
    CHECK_ERROR(stat, "Unable to create lacunarity attribute\n");
    nAttr.setKeyable(true);
    stat = addAttribute(lacunarity);
    CHECK_ERROR(stat, "Unable to add lacunarity attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::lacunarity, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from lacunarity to outputGeom");

    //persistence attr
    persistence = nAttr.create("persistence", "per", MFnNumericData::kFloat, 0.5, &stat);
    CHECK_ERROR(stat, "Unable to create persistence attribute\n");
    nAttr.setKeyable(true);
    stat = addAttribute(persistence);
    CHECK_ERROR(stat, "Unable to add persistence attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::persistence, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from persistence to outputGeom");

    //locatorWorldSpace attr
    locatorWorldSpace = mAttr.create("locatorWorldSpace", "locsp", MFnMatrixAttribute::kDouble, &stat);
    CHECK_ERROR(stat, "Unable to create locatorWorldSpace attribute\n");
    mAttr.setStorable(false);
    mAttr.setHidden(true);
    stat = addAttribute(locatorWorldSpace);
    CHECK_ERROR(stat, "Unable to add locatorWorldSpace attribute\n");
    stat = attributeAffects(SkNoiseDeformerMT::locatorWorldSpace, SkNoiseDeformerMT::outputGeom);
    CHECK_ERROR(stat, "Unable to call attributeAffects from locatorWorldSpace to outputGeom");

    return stat;
}

//init plugin
MStatus initializePlugin(MObject obj)
{
    MStatus stat;
    MFnPlugin plugin(obj, "Skeel Lee", nodeVersion.asChar(), "Any");
    stat = plugin.registerNode(nodeType, SkNoiseDeformerMT::nodeId, SkNoiseDeformerMT::creator, SkNoiseDeformerMT::initialize, MPxNode::kDeformerNode);
    CHECK_ERROR(stat, "Failed to register node: " + nodeType + "\n")
    return stat;
}

//uninit plugin
MStatus uninitializePlugin(MObject obj)
{
    MStatus stat;
    MFnPlugin plugin(obj);
    stat = plugin.deregisterNode(SkNoiseDeformerMT::nodeId);
    CHECK_ERROR(stat, "Failed to register node: " + nodeType + "\n")
    return stat;
}
