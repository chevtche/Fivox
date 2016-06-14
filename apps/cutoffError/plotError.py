#!/usr/bin/env python

import matplotlib.pyplot as plt
import argparse
import re

def main():

    parser = argparse.ArgumentParser(description="Read error values from file and create a plot")
    parser.add_argument("-f", "--file", help="File with mean square error values")

    file = open(parser.parse_args().file)
    mseList = []
    mseDerivatives = [0]
    cutoffList = []
    
    count = 0
    for line in file:
        if('\n' in line):
            line = line.strip('\n')        

        mse = float(re.findall("\d+\.\d+", line)[0])
        mseList.append(mse)
        fileEnd = re.findall("\d+\.mhd", line)[0]
        cutoff = float(re.findall("\d+", fileEnd)[0])
        cutoffList.append(cutoff)
        
        if(count > 0):
            mseDerivatives.append(mseList[count] - mseList[count - 1])
        count += 1   

    plt.plot(cutoffList,mseList,'k',cutoffList,mseList,"ro",cutoffList,mseDerivatives,'b')
    plt.show()

if __name__ == "__main__":
    main()
