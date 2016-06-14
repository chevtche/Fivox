#!/usr/bin/env python

import argparse
import numpy as np
import vtk
from vtk.util.numpy_support import vtk_to_numpy

def load( volumeFile ):
    imr = vtk.vtkMetaImageReader()
    imr.SetFileName(volumeFile)
    imr.Update()

    im = imr.GetOutput()
    x, y, z = im.GetDimensions()
    sc = im.GetPointData().GetScalars()
    a = vtk_to_numpy(sc)
    a = a.reshape(x, y, z)
    print( a.shape )
    print( im.GetDimensions() )
    assert a.shape==im.GetDimensions()
    return a

def main():
    """
    Entry point for comparator application does argument parsing and
    calls voxelize_batch class accordingly.
    """
    
    parser = argparse.ArgumentParser(description="Compare diffent volumes")
    parser.add_argument("volumes", metavar='Volume', type=str, nargs='+', help="list of .mhd volumes")
    args = parser.parse_args()
    outputFile = open("ComparatorOutput.txt", "w")

    minVolume = load(args.volumes[0])

    for volume in args.volumes:
        print( volume )
        biggerVolume = load( volume )
        reduced = np.zeros( minVolume.shape ) 

        #We assume that bounding box of the events is always centered
        offset = np.subtract(biggerVolume.shape, minVolume.shape) / 2

        reduced[:] = biggerVolume[offset[0]:minVolume.shape[0]+offset[0],offset[1]:minVolume.shape[1]+offset[1],offset[2]:minVolume.shape[2]+offset[2]]
        errorsArrayReduced = ((minVolume - reduced) ** 2)
        
        mseReduced = errorsArrayReduced.mean(axis=None)        
 
        maxSeReduced = np.amax(errorsArrayReduced)
        outputFile.write(str(mseReduced) + str(volume) + "\n")
        
        print("Mean Square Error (Reduced Volume): {0}  Max Square Error (Reduced Volume): {1}").format(mseReduced, maxSeReduced)

if __name__ == "__main__":
    main()
