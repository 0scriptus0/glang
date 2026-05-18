import os
import shutil
import subprocess

install_path = os.path.join(os.environ["USERPROFILE"], "glang")

print("Uninstalling glang...")

subprocess.run(["taskkill", "/F", "/IM", "glang.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

if os.path.exists(install_path):
    try:
        shutil.rmtree(install_path)
        print("Removed install folder.")
    except Exception as e:
        print(f"Failed to remove folder: {e}")
else:
    print("Install folder not found.")

try:
    import winreg

    key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_READ)
    current_path, _ = winreg.QueryValueEx(key, "Path")
    winreg.CloseKey(key)

    new_path = ";".join([p for p in current_path.split(";") if "glang" not in p.lower()])

    key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, "Environment", 0, winreg.KEY_SET_VALUE)
    winreg.SetValueEx(key, "Path", 0, winreg.REG_EXPAND_SZ, new_path)
    winreg.CloseKey(key)

    print("Removed glang from PATH.")

except Exception as e:
    print(f"PATH cleanup failed: {e}")

print("Uninstall complete.")
input("Press Enter to exit...")