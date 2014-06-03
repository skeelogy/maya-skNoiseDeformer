"""
@author: Skeel Lee
@contact: skeel@skeelogy.com
@since: 30 May 2014

A noise deformer plugin for Maya. It deforms meshes using fBm (fractional
Brownian motion) which adds up multiple layers of Simplex noises.

---------Usage-------------

1) Load the plugin, either using the Plug-in Manager or using the following MEL
   command:

        loadPlugin "skScriptedNoiseDeformer.py"

2) Select a mesh

3) Attach a new noise deformer to the mesh by executing the following MEL
   command:

        deformer -type skScriptedNoiseDeformer

4) Adjust the noise attributes (e.g. amplitude, frequency, octaves, lacunarity)
   in the channel box accordingly

5) Move/rotate/scale the accessory locator to transform the noise space, as
   desired

---------Notes-------------

In order to get the fastest speed out of this Python plugin, I would recommend
compiling/installing the noise library into mayapy.

1) Download the noise library from Casey Duncan at
   https://github.com/caseman/noise. This includes some C files that needs to
   be compiled into Python modules.

2) You will need a Python.h header file. If you do not already have that,
   execute this command in a terminal (or the equivalent in other Linux
   distros):

        > sudo apt-get install python-dev

3) Execute this command in a terminal to compile and install the Python modules
   into mayapy:

        > sudo `which mayapy` setup.py install

4) To verify that the installation has worked, try doing this in a shell:

        > mayapy
        [mayapy shell loads...]
        >>> import noise
        >>> noise.snoise3(2, 8, 3)
        -0.6522196531295776

Note that this Python plugin will still work if you are unable to perform the
steps above. The plugin will fall back to a pure-Python perlin.py module from
Casey Duncan if it cannot find the compiled noise module above. The speed is
much slower though and I would strongly recommend getting the above steps to
work if you are keen to use this Python plugin.

---------Credits-------------

This plugin uses the noise library from Casey Duncan:
https://github.com/caseman/noise

---------License-------------

Released under The MIT License (MIT) Copyright (c) 2014 Skeel Lee
(http://cg.skeelogy.com)

"""

try:
    #import the faster C-based noise module
    #if user has compiled/installed it to mayapy
    import noise
except:
    #otherwise just import the slower pure-python perlin module
    #because it works out-of-the-box without installation
    import libnoise.perlin
    noise = libnoise.perlin.SimplexNoise()

import sys

import maya.OpenMaya as om
import maya.OpenMayaMPx as omMPx

nodeType = 'skScriptedNoiseDeformer'
nodeVersion = '1.0'
nodeId = om.MTypeId(0x001212C1) #unique id obtained from ADN

EPSILON = 0.0000001

class skScriptedNoiseDeformer(omMPx.MPxDeformerNode):

    amp = om.MObject()
    freq = om.MObject()
    offset = om.MObject()
    octaves = om.MObject()
    lacunarity = om.MObject()
    persistence = om.MObject()
    locatorWorldSpace = om.MObject()

    def __init__(self):
        super(skScriptedNoiseDeformer, self).__init__()

    def deform(self, dataBlock, geomIter, localToWorldMat, multiIndex):

        #get envelope value, return if sufficiently near to 0
        envDataHandle = dataBlock.inputValue(self.envelope)
        envFloat = envDataHandle.asFloat()
        if envFloat <= EPSILON:
            return

        #get attribute values
        ampDataHandle = dataBlock.inputValue(self.amp)
        ampFloats = ampDataHandle.asFloat3()
        freqDataHandle = dataBlock.inputValue(self.freq)
        freqFloats = freqDataHandle.asFloat3()
        offsetDataHandle = dataBlock.inputValue(self.offset)
        offsetFloats = offsetDataHandle.asFloat3()
        octavesDataHandle = dataBlock.inputValue(self.octaves)
        octavesInt = octavesDataHandle.asInt()
        lacunarityDataHandle = dataBlock.inputValue(self.lacunarity)
        lacunarityFloat = lacunarityDataHandle.asFloat()
        persistenceDataHandle = dataBlock.inputValue(self.persistence)
        persistenceFloat = persistenceDataHandle.asFloat()
        locatorWorldSpaceDataHandle = dataBlock.inputValue(self.locatorWorldSpace)
        locatorWorldSpaceMat = locatorWorldSpaceDataHandle.asMatrix()

        #precompute some transformation matrices
        localToLocatorSpaceMat = localToWorldMat * locatorWorldSpaceMat.inverse()
        locatorToLocalSpaceMat = locatorWorldSpaceMat * localToWorldMat.inverse()

        #iterate through all the points
        while not geomIter.isDone():

            #get weight value for this point, continue if sufficiently near to 0
            weightFloat = self.weightValue(dataBlock, multiIndex, geomIter.index())
            if weightFloat <= EPSILON:
                continue

            #get locator space position
            pos = geomIter.position()
            pos *= localToLocatorSpaceMat

            #precompute some values
            noiseInputX = freqFloats[0] * pos.x - offsetFloats[0]
            noiseInputY = freqFloats[1] * pos.y - offsetFloats[1]
            noiseInputZ = freqFloats[2] * pos.z - offsetFloats[2]
            envTimesWeight = envFloat * weightFloat

            #calculate new position
            pos.x += ampFloats[0] * noise.snoise3(
                x = noiseInputX, y = noiseInputY, z = noiseInputZ,
                octaves = octavesInt,
                lacunarity = lacunarityFloat,
                persistence = persistenceFloat
            ) * envTimesWeight
            pos.y += ampFloats[1] * noise.snoise3(
                x = noiseInputX + 123, y = noiseInputY + 456, z = noiseInputZ + 789,
                octaves = octavesInt,
                lacunarity = lacunarityFloat,
                persistence = persistenceFloat
            ) * envTimesWeight
            pos.z += ampFloats[2] * noise.snoise3(
                x = noiseInputX + 234, y = noiseInputY + 567, z = noiseInputZ + 890,
                octaves = octavesInt,
                lacunarity = lacunarityFloat,
                persistence = persistenceFloat
            ) * envTimesWeight

            #convert back to local space
            pos *= locatorToLocalSpaceMat

            #set new position
            geomIter.setPosition(pos)

            geomIter.next()

    def accessoryNodeSetup(self, dagMod):

        thisObj = self.thisMObject()

        #get current object name
        thisFn = om.MFnDependencyNode(thisObj)
        thisObjName = thisFn.name()

        #create an accessory locator for user to manipulate a local deformation space
        locObj = dagMod.createNode('locator')
        dagMod.doIt()

        #rename transform and shape nodes
        dagMod.renameNode(locObj, thisObjName + '_loc')
        locDagPath = om.MDagPath()
        locDagFn = om.MFnDagNode(locObj)
        locDagFn.getPath(locDagPath)
        locDagPath.extendToShape()
        locShapeObj = locDagPath.node()
        dagMod.renameNode(locShapeObj, thisObjName + '_locShape')

        #connect locator's worldMatrix to locatorWorldSpace
        locFn = om.MFnDependencyNode(locObj)
        worldMatrixAttr = locFn.attribute('worldMatrix')
        dagMod.connect(locObj, worldMatrixAttr, thisObj, self.locatorWorldSpace)

    def accessoryAttribute(self):
        return self.locatorWorldSpace

#creator function
def nodeCreator():
    return omMPx.asMPxPtr(skScriptedNoiseDeformer())

#init function
def nodeInitializer():

    outputGeom = omMPx.cvar.MPxDeformerNode_outputGeom

    #amplitude attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.amp = nAttr.createPoint('amplitude', 'amp')
    nAttr.setDefault(1.0, 1.0, 1.0)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.amp)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.amp, outputGeom)

    #frequency attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.freq = nAttr.createPoint('frequency', 'freq')
    nAttr.setDefault(1.0, 1.0, 1.0)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.freq)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.freq, outputGeom)

    #offset attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.offset = nAttr.createPoint('offset', 'off')
    nAttr.setDefault(0.0, 0.0, 0.0)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.offset)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.offset, outputGeom)

    #octaves attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.octaves = nAttr.create('octaves', 'oct', om.MFnNumericData.kInt, 1)
    nAttr.setMin(1)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.octaves)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.octaves, outputGeom)

    #lacunarity attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.lacunarity = nAttr.create('lacunarity', 'lac', om.MFnNumericData.kFloat, 2.0)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.lacunarity)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.lacunarity, outputGeom)

    #persistence attr
    nAttr = om.MFnNumericAttribute()
    skScriptedNoiseDeformer.persistence = nAttr.create('persistence', 'per', om.MFnNumericData.kFloat, 0.5)
    nAttr.setKeyable(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.persistence)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.persistence, outputGeom)

    #locatorWorldSpace attr
    mAttr = om.MFnMatrixAttribute()
    skScriptedNoiseDeformer.locatorWorldSpace = mAttr.create('locatorWorldSpace', 'locsp')
    mAttr.setStorable(False)
    mAttr.setHidden(True)
    skScriptedNoiseDeformer.addAttribute(skScriptedNoiseDeformer.locatorWorldSpace)
    skScriptedNoiseDeformer.attributeAffects(skScriptedNoiseDeformer.locatorWorldSpace, outputGeom)

#init plugin
def initializePlugin(mObject):
    mPlugin = omMPx.MFnPlugin(mObject, "Skeel Lee", nodeVersion, "Any")
    try:
        mPlugin.registerNode(nodeType, nodeId, nodeCreator, nodeInitializer, omMPx.MPxNode.kDeformerNode)
    except:
        sys.stderr.write('Failed to register deformer node: %s\n' % (nodeType))
        raise

#uninit plugin
def uninitializePlugin(mObject):
    mPlugin = omMPx.MFnPlugin(mObject)
    try:
        mPlugin.deregisterNode(nodeId)
    except:
        sys.stderr.write('Failed to deregister deformer node: %s\n' % (nodeType))
        raise
