#!/usr/bin/env bash

# Step 1. Install build dependencies.
git checkout projects/dasa-ptwot
git submodule update --init --recursive
sudo apt update
# https://github.com/hadeaninc/mumble/blob/master/docs/dev/build-instructions/build_linux.md.
sudo apt install \
  build-essential \
  cmake \
  pkg-config \
  qt5-qmake \
  qtbase5-dev \
  qttools5-dev \
  qttools5-dev-tools \
  libqt5svg5-dev \
  libboost-dev \
  libssl-dev \
  libprotobuf-dev \
  protobuf-compiler \
  libprotoc-dev \
  libcap-dev \
  libxi-dev \
  libasound2-dev \
  libogg-dev \
  libsndfile1-dev \
  libopus-dev \
  libspeechd-dev \
  libavahi-compat-libdnssd-dev \
  libxcb-xinerama0 \
  libzeroc-ice-dev \
  libpoco-dev \
  g++-multilib

# Step 2. Build Mumble client and server, and voice capture plugin.
mkdir build
cd build
cmake ..
make -j12

# Step 3. All done. Instruct user on how to setup, launch and shutdown.
#         TODO: would be handy to automate some of this stuff.
echo -e "\e[33m"
echo "Issue the following command: cd build"
echo "You can now launch the server using: ./mumble-server"
echo "Then, you can launch the channel observer client using: ./mumble"
echo
echo "Further setup of the channel observer client is required:"
echo "    1. You can cancel audio tuning when the client is opened for the first time."
echo "    2. Ensure Automatic certificate creation is left ON and click Next. Then click Finish."
echo "    3. The Mumble server will be run local to the channel observer client,"
echo "       so do not consent to the transmission of your IP address."
echo "    4. Connect to the Mumble server. You may have to wait a few seconds for the Connect"
echo "       button to become enabled. Accept the self-signed cert."
echo "    5. Mute the client."
echo "    6. Open the settings menu, navigate to Plugins, scroll down to Hadean Voice Capture,"
echo "       and tick the Enable box. Press OK. You should see the following log printed in the"
echo "       client's output (it might be buried somewhat):"
echo "       [HADEAN] Voice capture plugin periodic function started"
echo "You can then leave the client running until the end of the exercise, at which point you"
echo "can simply close the client window to shut everything down."
echo
echo "When you're done with the Mumble server, issue: ps x | grep mumble"
echo "Then issue: kill -9 <SERVER_PID>"
echo
echo "After the first run-through of the above, you won't have to perform any special steps to"
echo "get the channel observer client up and running, just re-run the client using: ./mumble"
echo -e "\e[0m"
