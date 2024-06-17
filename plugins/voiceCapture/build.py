#!/usr/bin/env python3

from os import mkdir
from shutil import rmtree
from subprocess import run, CalledProcessError
from sys import exit
from argparse import ArgumentParser

args = ArgumentParser(description="Builds the voice capture Mumble plugin")
args.add_argument("-c", "--clean", help="Performs a clean build",
                  action="store_true")
args.add_argument("-o", "--out",
                  help="The folder to write build files (and the plugin) to",
                  default="out")
opts = args.parse_args()

if opts.clean:
    rmtree(opts.out, ignore_errors=True)
try:
    mkdir(opts.out)
except FileExistsError:
    pass

try:
    run(["cmake", ".."], check=True, cwd=opts.out)
    run(["make"], check=True, cwd=opts.out)
    print(f"\n\033[92mBuild successful!\033[0m\n")
    print(f"Please install the plugin into your Mumble Client by opening the Settings menu, navigating\n"
          f"to the Plugins tab, and clicking on Install Plugin.\n")
    print(f"Please select the *.so file located within:\n"
          f"\"{opts.out}\",\n"
          f"then make sure the Enabled checkbox for the plugin is ticked.\n")
    print(f"If you already have the plugin installed, you can simply Install it again and it will be\n"
          f"overridden (Reloading Plugins does not appear to achieve this).")
except KeyboardInterrupt:
    exit(130)
except CalledProcessError:
    exit(1)
