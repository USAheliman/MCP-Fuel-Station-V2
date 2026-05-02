Import("env")
import subprocess, sys

def upload_via_ota(source, target, env):
    firmware = str(source[0])
    host     = env.GetProjectOption("upload_port")
    base_url = f"http://{host}"

    print(f"\n>>> OTA upload → {base_url}/ota/upload")
    r1 = subprocess.run(
        ["curl", "-s", "-f", "-F", f"firmware=@{firmware}",
         f"{base_url}/ota/upload"],
        capture_output=True, text=True
    )
    if r1.returncode != 0:
        print("Upload failed:", r1.stderr or r1.stdout)
        sys.exit(1)
    print("Upload OK:", r1.stdout.strip())

    print(f">>> Installing slot 0")
    r2 = subprocess.run(
        ["curl", "-s", "-f", "-X", "POST",
         f"{base_url}/ota/install?slot=0"],
        capture_output=True, text=True
    )
    print("Install response:", r2.stdout.strip())
    if r2.returncode == 0:
        print("OTA complete — board is rebooting")
    else:
        print("Install step failed:", r2.stderr or r2.stdout)
        sys.exit(1)

env.Replace(UPLOADCMD=upload_via_ota)
