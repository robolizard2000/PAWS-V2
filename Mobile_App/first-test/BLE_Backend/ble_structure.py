class Service:
    def __init__(self, uuid:str, name:str):
        self.uuid = uuid
        self.name = name
class Characteristic:
    def __init__(self, uuid:str, name:str, properties:list, value_Type:str = "int"):
        self.uuid = uuid
        self.value = None
        self.name = name
        self.properties = properties
        if value_Type not in ["int", "string", "bool"]:
            raise ValueError("Invalid value type. Must be 'int', 'string', or 'bytes'.")
        if value_Type == "int":
            self.value = 0
        elif value_Type == "string":
            self.value = ""
        elif value_Type == "bool":
            self.value = False

class BLE_Data:
    def __init__(self):
        self.setting = Service("12345000-0000-1000-8000-00805F9B34FB", "Setting Service")
        self.setting_chars = [
            Characteristic("12345001-0000-1000-8000-00805F9B34FB", "Clock"           , ["Read", "Write"], "int"),
            Characteristic("12345002-0000-1000-8000-00805F9B34FB", "Back-light-timer", ["Read", "Write"], "int"),
            Characteristic("12345003-0000-1000-8000-00805F9B34FB", "LCD-Lock"        , ["Read", "Write"], "bool"),
            Characteristic("12345004-0000-1000-8000-00805F9B34FB", "Debug-Enable"    , ["Read", "Write"], "bool"),
            Characteristic("12345010-0000-1000-8000-00805F9B34FB", "Service-name"    , ["Read"]         , "string"),
            Characteristic("12345011-0000-1000-8000-00805F9B34FB", "Local-name"      , ["Read"]         , "string")
        ]
        self.controls = Service("12346000-0000-1000-8000-00805F9B34FB", "Controls")
        self.control_chars = [
            Characteristic("12346001-0000-1000-8000-00805F9B34FB", "Temp-Setpoint"        , ["Read", "Write"], "int"),
            Characteristic("12346002-0000-1000-8000-00805F9B34FB", "Temp-hysteresis"      , ["Read", "Write"], "int"),
            Characteristic("12346003-0000-1000-8000-00805F9B34FB", "Humid-Setpoint"       , ["Read", "Write"], "int"),
            Characteristic("12346004-0000-1000-8000-00805F9B34FB", "Humid-Hysteresis"     , ["Read", "Write"], "int"),
            Characteristic("12346005-0000-1000-8000-00805F9B34FB", "Wind-Max"             , ["Read", "Write"], "int"),
            Characteristic("12346006-0000-1000-8000-00805F9B34FB", "Light-Min"            , ["Read", "Write"], "int"),
            Characteristic("12346007-0000-1000-8000-00805F9B34FB", "Light-override"       , ["Read", "Write"], "bool"),
            Characteristic("12346008-0000-1000-8000-00805F9B34FB", "Light-Set"            , ["Read", "Write"], "bool"),
            Characteristic("12346009-0000-1000-8000-00805F9B34FB", "Window-override"      , ["Read", "Write"], "bool"),
            Characteristic("1234600A-0000-1000-8000-00805F9B34FB", "Window-Set"           , ["Read", "Write"], "bool")
        ]
        self.data_reading = Service("12347000-0000-1000-8000-00805F9B34FB", "Data-Reading")
        self.data_chars = [
            Characteristic("12347001-0000-1000-8000-00805F9B34FB", "Temp"                 , ["Notify"], "int"),
            Characteristic("12347002-0000-1000-8000-00805F9B34FB", "Humid"                , ["Notify"], "int"),
            Characteristic("12347003-0000-1000-8000-00805F9B34FB", "Wind"                 , ["Notify"], "int"),
            Characteristic("12347004-0000-1000-8000-00805F9B34FB", "Rain"                 , ["Notify"], "int"),
            Characteristic("12347005-0000-1000-8000-00805F9B34FB", "Light"                , ["Notify"], "int")
        ]
