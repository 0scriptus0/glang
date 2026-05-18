import subprocess

user = input("Would you like to install glang? (Y/N): ")

if (user == "y" or user == "Y"):
    subprocess.run(["powershell", "-ExecutionPolicy", "Bypass", "-File", "install.ps1"])
else:
    print("Installation cancelled.")