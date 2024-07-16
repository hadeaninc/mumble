#!/usr/bin/env python3


from argparse import ArgumentParser
from socket import gethostname
from subprocess import run
from pathlib import Path
from os import getcwd


def run_command(cmd: str, *, cwd: str = None) -> None:
    run(cmd.split(" "), check=True, cwd=cwd)


# Extract arguments.
parser = ArgumentParser(
    prog="Setup Mumble",
    description="Builds and configures the Observer Client (and builds the Mumble Server)"
)
parser.add_argument("-v", "--services-host",
                    default="localhost",
                    help="The host at which the Hadean services are running")
parser.add_argument("-p", "--services-port",
                    default=8080, type=int,
                    help="The port at which the Hadean services are running")
parser.add_argument("-s", "--mumble-server",
                    default=f"{gethostname()}-2",
                    help="The IP address or name of the machine running the Mumble server to automatically connect to on launch")
parser.add_argument("-t", "--topic-name", "--topic", "--username",
                    default="BLUEFOR",
                    help="Transcribed radio messages will be posted to the Hadean services as chats under this topic")
parser.add_argument("--skip-dependencies",
                    action="store_true",
                    help="Skip installing build dependencies")
parser.add_argument("--skip-build",
                    action="store_true",
                    help="Skip building the client and server")
args = parser.parse_args()
print(f"Hadean services: \033[96m{args.services_host}:{args.services_port}\033[0m")
print(f"Mumble server will be hosted at: \033[96m{args.mumble_server}\033[0m")
print(f"The observer client will connect to said Mumble server as \033[96m{args.topic_name}\033[0m by default")

# Install build dependencies.
if args.skip_dependencies:
    print("Skipping dependency installation step")
else:
    run_command("git submodule update --init --recursive")
    run_command("sudo apt update")
    # https://github.com/hadeaninc/mumble/blob/master/docs/dev/build-instructions/build_linux.md.
    run_command("sudo apt install build-essential cmake pkg-config qt5-qmake qtbase5-dev qttools5-dev qttools5-dev-tools libqt5svg5-dev libboost-dev libssl-dev libprotobuf-dev protobuf-compiler libprotoc-dev libcap-dev libxi-dev libasound2-dev libogg-dev libsndfile1-dev libopus-dev libspeechd-dev libavahi-compat-libdnssd-dev libxcb-xinerama0 libzeroc-ice-dev libpoco-dev g++-multilib")


# Build Observer Client and Mumble Server, and Voice Capture plugin.
Path("build").mkdir(exist_ok=True)
if args.skip_build:
    print("Skipping build step")
else:
    run_command("cmake ..", cwd="build")
    run_command("make -j 12", cwd="build")


# Write JSON configuration file containing the host and port.
with open("build/voiceCapture.json", encoding="utf-8", mode='w') as f:
    f.write(f'{{ "host": "{args.services_host}", "port": {args.services_port} }}\n')


# Write Mumble configuration file. This will automate most of the setup process.
# This file will be based on the mumble_settings.json script in the current
# directory, that acts as a template for the final file. All occurrences of ~#
# will be replaced with another value, where # is...
#     H -> the home directory ($HOME)
#     C -> the current working directory ($PWD)
#     S -> the Mumble server to auto connect to ($server)
#     T -> the username to connect with ($topic)
mumble_config_filename = "mumble_settings.json"
with open(mumble_config_filename, encoding="utf-8", mode="r") as f:
    mumble_config = f.read()

mumble_config = mumble_config.replace("~H", Path.home().__str__())
mumble_config = mumble_config.replace("~C", getcwd())
mumble_config = mumble_config.replace("~S", args.mumble_server)
mumble_config = mumble_config.replace("~T", args.topic_name)

mumble_config_folder = f"{Path.home()}/.config/Mumble/Mumble"
Path(mumble_config_folder).mkdir(parents=True, exist_ok=True)
with open(f"{mumble_config_folder}/{mumble_config_filename}", encoding="utf-8", mode='w') as f:
    f.write(mumble_config)


# Instruct user on how to setup, launch and shutdown.
print(
"""\033[93m
Issue the following command: cd build
You can now launch the server using: ./mumble-server
(or you can deploy it and its configuration files).
Then, you can launch the channel observer client using: ./mumble

Further setup of the channel observer client is required:

    1. Ensure Automatic certificate creation is left ON and click Next. Then click Finish.

    2. You should be able to connect to your desired server automatically. You may have to
       wait some seconds for the server to allow you to connect, and then click OK to
       make the connection. Accept the self-signed cert.

       The name you give to the Observer Client when you join the server will be used as
       the chat topic to send transcriptions to. The username textbox should already be
       populated with """+args.topic_name+""", but you may change it before connection if you wish. Note
       that every client connected to the server should only use alphanumeric characters
       (A-Z, a-z, 0-9), as usernames will be inserted directly into URLs, but if they do
       include special charcters, these will be stripped out when necessary.

    3. Each client that's connected to the server MUST use PTT! Remaining unmuted with
       continuous transmission turned on will prevent the plugin from detecting when to
       stop recordings.

You can then leave the client running until the end of the exercise, at which point you
can simply close the client window to shut everything down.

If you need to change the host and port used to communicate with Hadean services, amend
the voiceCapture.json file in the build directory.

When you're done with the Mumble server, issue: ps x | grep mumble
Then issue: kill -9 <SERVER_PID>

After the first run-through of the above, you won't have to perform any special steps to
get the channel observer client up and running, just re-run the client using: ./mumble
\033[0m"""
)
