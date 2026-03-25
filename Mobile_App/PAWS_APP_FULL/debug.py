from kivy.utils import platform
import os 

class DebugLog:
    def __init__(self, screen:str = "general", enable:bool = True, log_file:str = "debug_log.txt"):
        self.logs = []
        self.screen = screen
        self.enable = enable
        if platform == "android":
            from android.storage import app_storage_path
            path = app_storage_path()
        else:
            path = "."

        self.file_path = f"{path}/{log_file}"


    def log(self, message):
        message = f"[{self.screen}]: {message}"
        if self.enable:
            print(message)
            with open(self.file_path, "a") as f:
                f.write(message + "\n")
            f.close()
        
    def start(self):
        self.logs = []
        self.log("Debug logging started.")
    def stop(self):
        self.log("Debug logging stopped.")

    def clear(self):
        self.logs = []
        if os.path.exists(self.file_path):
            os.remove(self.file_path)
        else:
            self.log("The file does not exist")
