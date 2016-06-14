#!/usr/bin/env python
"""
Usage: cutoffError_batch.py --config file.config

Launch 'voxelize' in batch mode using sbatch to generate volumes with 
different cutoff distance on cluster nodes. Based on the 'voxelize_batch' 
tool in BlueBrain/Fivox
"""

import argparse
import glob
import json
import math
import os
import subprocess

import time

__author__ = "Grigori Chevtchenko"
__email__ = "grigori.chevtchenko@epfl.ch"
__copyright__ = "Copyright 2016, EPFL/Blue Brain Project"

# pylint: disable=W0142

SECTION_SLURM = 'slurm'
SLURM_NAME = 'job_name'
SLURM_TIME = 'job_time'
SLURM_QUEUE = 'queue'
SLURM_ACCOUNT = 'account'
SLURM_OUTPUTDIR = 'output_dir'
SLURM_NODES = 'nodes'
SLURM_TASKS_PER_NODE = 'tasks_per_node'

SECTION_VOXELIZE = 'voxelize'
VOXELIZE_FRAME = 'frame'
VOXELIZE_RESOLUTION = 'resolution'
VOXELIZE_FUNCTOR = 'functor'

SECTION_CUTOFF = 'cutoff'
CUTOFF_STARTCUTOFF = 'start_cutoff'
CUTOFF_ENDCUTOFF = 'end_cutoff'
CUTOFF_CUTOFFSTEP = 'cutoff_step'

EXAMPLE_JSON = 'example.json'


def find_voxelize():
    """
    Search for voxelize executable in PATH and return result
    """

    from distutils import spawn
    voxelize_path = spawn.find_executable("voxelize")
    if not voxelize_path:
        print("Cannot find voxelize executable in PATH")
        return False
    print("Using voxelize executable '{0}'".format(voxelize_path))
    return True

class VoxelizeBatch(object):
    """
    Submits sbatch jobs to generate volume files using the voxelize app in find_voxelize
    by using a configuration file for setup.
    """

    def __init__(self, verbose, dry_run):
        self.verbose = verbose
        self.dry_run = dry_run
        self.volumeNames = ""
        self.dict = {}
        self.default_dict = {}
        self._fill_default_dict()

    def _fill_default_dict(self):
        """
        Setup default values for all supported options in the configuration file
        """

        self.default_dict = {
            SECTION_SLURM: {
                SLURM_NAME: 'voxelize_batch',
                SLURM_TIME: '06:00:00',
                SLURM_QUEUE: 'prod',
                SLURM_ACCOUNT: 'proj3',
                SLURM_OUTPUTDIR: '.',
                SLURM_NODES: 1,
                SLURM_TASKS_PER_NODE: 16},
            SECTION_VOXELIZE: {
                VOXELIZE_FRAME: 0,
                VOXELIZE_RESOLUTION: 1,
                VOXELIZE_FUNCTOR: 'lfp'},
            SECTION_CUTOFF: {
                CUTOFF_STARTCUTOFF: 100,
                CUTOFF_ENDCUTOFF: 100,
                CUTOFF_CUTOFFSTEP: 0}}

    def _build_sbatch_comparator_script(self):
        """
        Build sbatch script for the comparator process
        """

        values = dict(self.dict)

        sbatch_script = '\n'.join((
            "#!/bin/bash",
            "#SBATCH --job-name=\"{slurm[job_name]}\"",
            "#SBATCH --time={slurm[job_time]}",
            "#SBATCH --partition={slurm[queue]}",
            "#SBATCH --account={slurm[account]}",
            "#SBATCH --nodes={slurm[nodes]}",
            "#SBATCH --ntasks-per-node={slurm[tasks_per_node]}",
            "#SBATCH --output={slurm[output_dir]}/%j_out.txt",
            "#SBATCH --error={slurm[output_dir]}/%j_err.txt",
            "./comparator.py " + self.volumeNames
        )).format(**values)

        if self.verbose:
            print(sbatch_script)
        return sbatch_script

    def _build_sbatch_script(self, cutoff):
        """
        Build sbatch script for a certain cutoff distance
        """

        values = dict(self.dict)
        values['current_cutoff'] = cutoff

        zero_pad = len(str(values[SECTION_CUTOFF][CUTOFF_ENDCUTOFF])) - len(str(cutoff))

        for i in range(zero_pad):
            values['volume'] += '0'

        values['volume'] += str(cutoff)
        self.volumeNames += values['volume'] + ".mhd "     

        sbatch_script = '\n'.join((
            "#!/bin/bash",
            "#SBATCH --job-name=\"{slurm[job_name]}\"",
            "#SBATCH --time={slurm[job_time]}",
            "#SBATCH --partition={slurm[queue]}",
            "#SBATCH --account={slurm[account]}",
            "#SBATCH --nodes={slurm[nodes]}",
            "#SBATCH --ntasks-per-node={slurm[tasks_per_node]}",
            "#SBATCH --output={slurm[output_dir]}/%j_out.txt",
            "#SBATCH --error={slurm[output_dir]}/%j_err.txt",
            "voxelize --volume \"fivoxtest://?resolution={voxelize[resolution]}&functor={voxelize[functor]}&cutoff={current_cutoff}\" --frame {voxelize[frame]} "\
            "--output {volume}"
        )).format(**values)

        if self.verbose:
            print(sbatch_script)
        return sbatch_script

    def write_example_config(self):
        """
        Write example configuration to current directory
        """

        with open(EXAMPLE_JSON, 'w') as configfile:
            json.dump(self.default_dict, configfile, sort_keys=True, indent=4,
                      ensure_ascii=False)
        print("Wrote {0} to current directory".format(EXAMPLE_JSON))

    def read_config(self, config):
        """
        Read configuration file and validate content
        """

        with open(config) as configfile:
            self.dict = json.loads(configfile.read())

        #volume = self.dict.get(SECTION_VOXELIZE).get(VOXELIZE_VOLUME, '')
        #if not volume:
        #    print("Error: Need valid volume URI")
        #    return False

        self.dict['volume'] = "{slurm[output_dir]}/{slurm[job_name]}_".format(**self.dict)

        return True

    def submit_jobs(self):
        """
        Submit jobs from frame range specified in configuration, but checks
        for existing volumes in output directory to submit jobs only for
        missing volumes.
        """

        voxelize_dict = self.dict[SECTION_VOXELIZE]
        cutoff_dict = self.dict[SECTION_CUTOFF]
        frame = voxelize_dict[VOXELIZE_FRAME]
        start_cutoff = cutoff_dict[CUTOFF_STARTCUTOFF]
        end_cutoff = cutoff_dict[CUTOFF_ENDCUTOFF]
        cutoff_step = cutoff_dict[CUTOFF_CUTOFFSTEP]  

        outdir = self.dict[SECTION_SLURM][SLURM_OUTPUTDIR]
        if not os.path.exists(outdir):
            os.makedirs(outdir)

        num_jobs = 0
        jobIds = ""

        if ((end_cutoff - start_cutoff) < 0):
            print("error: invalid cutoff distances range")
        elif ((start_cutoff - end_cutoff) == 0):
            print("Only one volume")
            sbatch_script = self._build_sbatch_script(start_cutoff)
            if not self.dry_run:
                sbatch = subprocess.Popen(['sbatch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE )
                out = sbatch.communicate(input=sbatch_script)
                jobIds += ":" + out[0].split()[-1]
            num_jobs += 1
            return
        else:
            for cutoff in range(start_cutoff, end_cutoff, cutoff_step):
                sbatch_script = self._build_sbatch_script(cutoff)
                if not self.dry_run:
                    sbatch = subprocess.Popen(['sbatch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    out = sbatch.communicate(input=sbatch_script)
                    jobIds += ":" + out[0].split()[-1]
                num_jobs += 1 
                print("Submit job {1} with cutoff distance {0}...".format(cutoff,num_jobs))

        sbatch_script_comparator = self._build_sbatch_comparator_script();
        print(jobIds)
        if not self.dry_run:
            sbatch = subprocess.Popen(['sbatch', "--dependency=afterok" + jobIds], stdin=subprocess.PIPE)
            sbatch.communicate(input=sbatch_script_comparator)

        print(self.volumeNames) 
        return

def main():
    """
    Entry point for voxelize batch application does argument parsing and
    calls voxelize_batch class accordingly.
    """

    parser = argparse.ArgumentParser(description="Submit sbatch job(s) \
                                     launching voxelize to generate volumes")
    parser.add_argument("-c", "--config", help="path to config file")
    parser.add_argument("--dry-run", action="store_true",
                        help="parse config file, but do not submit any jobs")
    parser.add_argument("-e", "--example-config", action="store_true",
                        help="write example.json to current directory")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="print more information")
    args = parser.parse_args()

    voxelize_batch = VoxelizeBatch(args.verbose, args.dry_run)

    if args.example_config:
        voxelize_batch.write_example_config()
        exit()

    if not args.config:
        parser.print_help()
        exit()

    if not voxelize_batch.read_config(args.config):
        exit()

    if not find_voxelize():
        exit()

    voxelize_batch.submit_jobs()

if __name__ == "__main__":
    main()
