from kivy.app import App
from kivy.lang import Builder
from kivy.uix.screenmanager import NoTransition, ScreenManager, Screen

class ConnectBLEScreen(Screen):
    pass    
class DataScreen(Screen):
    pass
class SettingsScreen(Screen):
    pass
class DebugScreen(Screen):
    pass        

class PAWSApp(App):
    def build(self):
        sm = ScreenManager(transition=NoTransition()) ## transition is set to none to make screen changes instant
        sm.add_widget(ConnectBLEScreen(name='connect_ble')) ## the names are what are uset to call the screen
        sm.add_widget(DataScreen(name='data_screen'))
        sm.add_widget(SettingsScreen(name='settings_screen'))
        sm.add_widget(DebugScreen(name='debug_screen'))
        return sm

if __name__ == '__main__':    
    PAWSApp().run()