import subprocess
import os

script_path = os.path.join(os.path.dirname(__file__), "backend.ps1")

user = input("Would you like to install glang? (Y/N): ")

if user.lower() == "y":
    subprocess.run([
        "powershell",
        "-ExecutionPolicy", "Bypass",
        "-File", script_path
    ])
else:
    print("Installation cancelled.")

input("Press Enter to exit...")