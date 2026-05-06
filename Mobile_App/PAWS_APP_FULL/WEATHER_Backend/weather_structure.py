import python_weather, datetime
class Weather_Data:
    def __init__(self):
        self.current_temp:  int = 0
        self.current_humid: int =0 
        self.current_wind:  int = 0
        self.chance_rain:   int =  0
        self.chance_dry:    int = 0
        self.date: datetime 

    def __str__(self):
        return f"{self.date!r}, tem: {self.current_temp}, humid: {self.current_humid!r}"

