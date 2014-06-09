skNoiseDeformer Maya Plugin
===========================

### Introduction

This is a noise deformer plugin for Maya. It deforms meshes using fBm (fractional Brownian motion) which adds up multiple layers of Simplex noises.

Both the C++ and Python plugins are available. Multi-threaded implementations are also included, labelled with a suffix of MT.

### Usage

1) Load the plugin, either using the Plug-in Manager or using the following MEL command:

If you are using the C++ plugin:

    loadPlugin "skNoiseDeformer.so"
	or
    loadPlugin "skNoiseDeformerMT.so" (for the multi-threaded version)

If you are using the Python plugin:

    loadPlugin "skScriptedNoiseDeformer.py"

2) Select a mesh

3) Attach a new noise deformer to the mesh by executing the following MEL command:

If you are using the C++ plugin:

    deformer -type skNoiseDeformer
    or
    deformer -type skNoiseDeformerMT (for the multi-threaded version)

If you are using the Python plugin:

    deformer -type skScriptedNoiseDeformer

4) Adjust the noise attributes (e.g. amplitude, frequency, octaves, lacunarity) in the channel box accordingly

5) Move/rotate/scale the accessory locator to transform the noise space, as desired

### C++ Plugin

#### Linux

To compile the C++ plugin in Linux, you need to use a specific gcc compiler version based on the Maya version that you are using. For example, for Maya 2014, gcc 4.1.2 is required. You can follow the steps in the [Maya 2014 Linux compiler requirement documentation](http://docs.autodesk.com/MAYAUL/2014/ENU/Maya-API-Documentation/index.html?url=files/Shapes.htm,topicNumber=d30e14674) to get that installed.

The makefile provided here is for Maya 2014 and uses g++412. If you are using another version of Maya, you might need to edit the makefile accordingly.

Once the correct version of gcc is installed on your Linux system, you can execute the following command in a terminal to compile the plugin:

    > make
    or
    > make TYPE=MT (for the multi-threaded version)

A *skNoiseDeformer.so* file will be created in the bin sub-directory. This is the actual compiled plugin. You can copy it manually to a suitable installation folder, or you can run this in the same terminal:

    > make install MAYA_VERSION=2014-x64
    or
    > make install MAYA_VERSION=2014-x64 TYPE=MT (for the multi-threaded version)

which will install the *skNoiseDeformer.so* file to a plugin directory in the user maya directory (the one where preferences are stored).

You can also compile the debug version of the plugin if necessary:

    > make BUILD=debug
    or
    > make BUILD=debug TYPE=MT (for the multi-threaded version)

which will produce a *skNoiseDeformer_d.so* plugin.

You can also install this debug plugin in a similar fashion:

    > make install MAYA_VERSION=2014-x64 BUILD=debug
    or
    > make install MAYA_VERSION=2014-x64 BUILD=debug TYPE=MT (for the multi-threaded version)

If you need to clean the project, you can do it using the usual way:

    > make clean

#### Windows

If you are using Windows, you will need to setup a Visual Studio project to compile the plugin.

### Python Plugin

The equivalent Python plugin is also available, just in case you have problems compiling the C++ files. This Python file can be loaded into Maya directly as a plugin, without the need to compile. Note that the speed is considerably slower than the C++ version but it might be adequate for simple basic usages.

In order to get the fastest speed out of the Python plugin, I would recommend compiling/installing the noise library into mayapy.

1) Download the noise library from Casey Duncan at <https://github.com/caseman/noise>. This includes some C files that needs to be compiled into Python modules.

2) You will need a Python.h header file. If you do not already have that, execute this command in a terminal (or the equivalent in other Linux distros):

    > sudo apt-get install python-dev

3) Execute this command in a terminal to compile and install the Python modules into mayapy:

    > sudo `which mayapy` setup.py install

4) To verify that the installation has worked, try doing this in a shell:

    > mayapy
    [mayapy shell loads...]
    >>> import noise
    >>> noise.snoise3(2, 8, 3)
    -0.6522196531295776

Note that the Python plugin will still work if you are unable to perform the steps above. The plugin will fall back to a pure-Python perlin.py module from Casey Duncan if it cannot find the compiled noise module above. The speed is much slower though and I would strongly recommend getting the above steps to work if you are keen to use the Python plugin.

### Credits

This plugin uses the noise library from Casey Duncan: <https://github.com/caseman/noise>

### License

Released under The MIT License (MIT)<br/>
Copyright (c) 2014 Skeel Lee <http://cg.skeelogy.com>