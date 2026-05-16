Import("env")
import subprocess, sys, os

def upload_via_ota(source, target, env):
    firmware = str(source[0])
    host     = env.GetProjectOption("upload_port")
    base_url = f"http://{host}"

    # Filesystem image — upload data/ files individually via /fs/upload
    if "littlefs" in firmware.lower() or "spiffs" in firmware.lower():
        _upload_fs_files(base_url, env)
        return

    # Firmware binary — store on device then flash
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


def _upload_fs_files(base_url, env):
    data_dir = env.subst("$PROJECT_DATA_DIR")
    if not data_dir or not os.path.isdir(data_dir):
        data_dir = os.path.join(env.subst("$PROJECT_DIR"), "data")
    if not os.path.isdir(data_dir):
        print(f"Data directory not found: {data_dir}")
        sys.exit(1)

    print(f"\n>>> Uploading web files from {data_dir} to {base_url}/fs/upload")
    ok = fail = 0
    for fname in os.listdir(data_dir):
        fpath = os.path.join(data_dir, fname)
        if not os.path.isfile(fpath):
            continue
        print(f"  /{fname} ...", end="", flush=True)
        r = subprocess.run(
            ["curl", "-s", "-f",
             "-F", f"file=@{fpath};filename=/{fname}",
             f"{base_url}/fs/upload"],
            capture_output=True, text=True
        )
        if r.returncode == 0:
            print(" OK")
            ok += 1
        else:
            print(f" FAILED: {r.stderr or r.stdout}")
            fail += 1

    if fail:
        print(f"\nFilesystem upload: {ok} OK, {fail} FAILED")
        sys.exit(1)
    print(f"\nFilesystem upload: all {ok} files uploaded OK")


env.Replace(UPLOADCMD=upload_via_ota)
