from kivy.utils import platform

if platform == "android":
    print("Running on Android")
else:
    print("Running on Linux/Desktop")