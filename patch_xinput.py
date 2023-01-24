#
# Based upon the "standard" https://docs.platformio.org/en/stable/scripting/examples/override_package_files.html
#

from pathlib import Path
import shutil

Import("env")

def main():
    framework_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy"))
    flag_path = framework_dir / ".patching-done"
    
    t4 = Path("./lib/ArduinoXInput_Teensy/teensy/avr/cores/teensy4")
    
    if not t4.exists():
        print("Missing submodule for ArduinoXInput_Teensy! Did you clone with --recurse-submodules?")
        exit(-1)
    
    if flag_path.exists():
        print("Already patched, exiting.")
        return
    
    framework_t4 = framework_dir / "cores" / "teensy4"
    
    assert (framework_t4 / "usb_desc.h").exists()
    assert (t4 / "usb_desc.h").exists()
    
    for f in t4.iterdir():
        print(f"Patching {f.name}...")
        shutil.copy(f, framework_t4)
    
    def create_flag(**_):
        with open(flag_path, "w") as flag:
            flag.write("")
    
    env.Execute(create_flag)

# Can't check __name__ == "__main__" because then platformio doesn't run it.
main()
