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

#ifndef _SK_NOISE_DEFORMER_MT_H_
#define _SK_NOISE_DEFORMER_MT_H_

class SkNoiseDeformerMT : public MPxDeformerNode
{

public:
    SkNoiseDeformerMT();
    virtual ~SkNoiseDeformerMT();
    virtual MStatus deform(MDataBlock& dataBlock,
                           MItGeometry& geomIter,
                           const MMatrix& localToWorldMat,
                           unsigned int multiIndex);
    virtual MStatus accessoryNodeSetup(MDagModifier& dagMod);
    virtual MObject& accessoryAttribute() const;
    static void* creator();
    static MStatus initialize();

public:
    static MTypeId nodeId;
    static MObject numTasks;
    static MObject amp;
    static MObject freq;
    static MObject offset;
    static MObject octaves;
    static MObject lacunarity;
    static MObject persistence;
    static MObject locatorWorldSpace;

};

#endif
