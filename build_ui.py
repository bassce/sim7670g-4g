#  This program is free software; you can use it, redistribute it
#  and / or modify it under the terms of the GNU General Public License
#  (GPL) as published by the Free Software Foundation; either version 3
#  of the License or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program, in a file called gpl.txt or license.txt.
#   If not, write to the Free Software Foundation Inc.,
#   59 Temple Place - Suite 330, Boston, MA  02111-1307 USA
import os
import os.path
import platform
import re
import shutil
import sys
from subprocess import run, CalledProcessError

Import("env")
from SCons.Script import COMMAND_LINE_TARGETS

# Install custom packages from the PyPi registry
try:
    import intelhex
except ImportError:
    run([sys.executable, "-m", "pip", "install", "intelhex"], check=True)

def writeUIConfiguration(env):
    confJson = "./data/public/configuration.json"
    if os.path.exists(confJson):
        os.remove(confJson)
    f = open(confJson, "w+")
    f.write("{\"deviceType\": \"" + env["PIOENV"] + "\"}")
    f.close()


def isTool(name):
    return shutil.which(name) is not None


def buildUI():
    if isTool("npm"):
        print("Attempting to build UI...")
        install_command = "ci" if os.path.isfile("package-lock.json") else "install"
        try:
            if platform.system() == "Windows":
                print(run(["npm.cmd", install_command], check=True, capture_output=False, text=True).stdout)
                print(run(["npm.cmd", "run", "build"], check=True, capture_output=False, text=True).stdout)
            else:
                print(run(["npm", install_command], check=True, capture_output=False, text=True).stdout)
                print(run(["npm", "run", "build"], check=True, capture_output=False, text=True).stdout)
        except OSError as e:
            print("Encountered error OSError building UI:", e)
            if e.filename:
                print("Filename is", e.filename)
            print("WARNING: Failed to build UI package Using pre-built page.")
        except CalledProcessError as e:
            print(e.output)
            print("Encountered error CalledProcessError building UI:", e)
            print("WARNING: Failed to build UI package. Using pre-built page.")
        except Exception as e:
            print("Encountered error", type(e).__name__, "building UI:", e)
            print("WARNING: Failed to build UI package. Using pre-built page.")


def removeUIFiles(public_path):
    pattern = ".*(\\.js$|\\.txt(\\.gz)?$)"

    print("Remove not gzip files...")

    for root, dirs, files in os.walk(public_path):
        for file in filter(lambda x: re.match(pattern, x), files):
            print("..." + file)
            os.remove(os.path.join(root, file))


def fsBuild(source, target, env):
    if os.getenv("OBD2_MQTT_PREBUILT_UI") != "1":
        buildUI()
    if not os.path.isfile("./data/public/index.html"):
        raise RuntimeError("UI build failed and no prebuilt index.html exists")
    removeUIFiles("./data/public/")
    writeUIConfiguration(env)


# Run before the platform filesystem builder enumerates data/. A pre-action on
# a guessed image node can miss the Windows builder's actual target node.
if {"buildfs", "uploadfs", "uploadfsota"}.intersection(COMMAND_LINE_TARGETS):
    fsBuild(None, None, env)
