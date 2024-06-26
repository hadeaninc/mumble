#!/usr/bin/env bash

# Step 1. Grab host and port from command-line arguments.
if [ "$#" -lt 2 ]; then
  echo "Checks out the DASA branch, installs build dependencies, then builds Mumble Server and the Observer Client."
  echo
  echo "Usage: ./install-mumble.sh host port"
  echo " E.g.: ./install-mumble.sh localhost 8080"
  echo
  echo " host : Define the host where the voice capture plugin can send audio files to."
  echo " port : Define the port where the voice capture plugin can send audio files to."
  exit 0
fi
host=$1
port=$2
echo "Hadean services host: $host"
echo "Hadean services port: $port"

# Step 2. Install build dependencies.
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

# Step 3. Build Mumble client and server, and voice capture plugin.
mkdir build
cd build
cmake ..
make -j12

# Step 4. Write JSON configuration file containing the host and port.
printf "{ \"host\": \"$host\", \"port\": $port }\n" > voiceCapture.json

# Step 5. All done. Instruct user on how to setup, launch and shutdown.
#         TODO: would be handy to automate some of this stuff.
echo -e "\e[33m"
echo "Issue the following command: cd build"
echo "You can now launch the server using: ./mumble-server"
echo "Then, you can launch the channel observer client using: ./mumble"
echo
echo "Further setup of the channel observer client is required:"
echo
echo "    1. You can cancel audio tuning when the client is opened for the first time, but if"
echo "       you do, you may have to reconfigure your audio output system in the settings menu"
echo "       if the plugin doesn't detect speech in the channel."
echo
echo "    2. Ensure Automatic certificate creation is left ON and click Next. Then click Finish."
echo
echo "    3. The Mumble server will be run local to the channel observer client,"
echo "       so do not consent to the transmission of your IP address."
echo
echo "    4. Connect to the Mumble server. You may have to wait a few seconds for the Connect"
echo "       button to become enabled. Accept the self-signed cert. The name you give to the"
echo "       observer client when you join the server will be used as the chat topic to send"
echo "       transcriptions to. Note that every client connected to the server should only use"
echo "       alphanumeric characters (A-Z, a-z, 0-9), as usernames will be inserted directly"
echo "       into URLs, but if they do include special charcters, these will be stripped out"
echo "       when necessary."
echo
echo "    5. Mute the client, but DO NOT DEAFEN IT. Deafening will prevent the plugin from"
echo "       receiving voice packets, and the client will not be able to record them. For this"
echo "       reason, the deafen button and menu item from the main window have been disabled to"
echo "       make it difficult to deafen the observer client (i.e. clicking them will do nothing)."
echo
echo "    6. Each client that's connected to the server MUST use PTT! Remaining unmuted with"
echo "       continuous transmission turned on will prevent the plugin from detecting when to"
echo "       stop recordings."
echo
echo "    7. Open the settings menu, navigate to Plugins, scroll down to Hadean Voice Capture,"
echo "       and tick the Enable box. Press OK. You should see the following log printed in the"
echo "       client's output (it might be buried somewhat):"
echo "       [HADEAN] Voice capture plugin periodic function started"
echo
echo "You can then leave the client running until the end of the exercise, at which point you"
echo "can simply close the client window to shut everything down."
echo
echo "If you need to change the host and port used to communicate with Hadean services, amend"
echo "the voiceCapture.json file in the build directory."
echo
echo "When you're done with the Mumble server, issue: ps x | grep mumble"
echo "Then issue: kill -9 <SERVER_PID>"
echo
echo "After the first run-through of the above, you won't have to perform any special steps to"
echo "get the channel observer client up and running, just re-run the client using: ./mumble"
echo -e "\e[0m"
