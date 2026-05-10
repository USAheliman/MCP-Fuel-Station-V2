Import("env")
import subprocess, os, sys

def clear_otadata(source, target, env):
    port   = env.GetProjectOption("upload_port")
    python = os.path.join(env["PROJECT_PACKAGES_DIR"],
                          "tool-esptoolpy", "esptool.py")
    if not os.path.isfile(python):
        print("clear_otadata: esptool not found, skipping")
        return
    cmd = [sys.executable, python,
           "--chip", "esp32s3",
           "--port", port,
           "--baud", "921600",
           "erase_region", "0xe000", "0x2000"]
    print("clear_otadata: erasing otadata so factory partition boots")
    subprocess.run(cmd, check=False)

env.AddPostAction("upload", clear_otadata)
