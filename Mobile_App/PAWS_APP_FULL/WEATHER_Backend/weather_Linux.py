import asyncio
import time
import threading

import python_weather # https://pypi.org/project/python-weather/
from WEATHER_Backend.weather_structure import Weather_Data
#from weather_structure import   Weather_Data
class Weather:
    def __init__(self,  location:str = "Southampton", 
                        Clock: int = 60, 
                        metric: bool = True):
        self.Location = location
        self.clock = Clock
        self.checked = False
        self.keep_updated = True
        self.data = Weather_Data()
        if metric:
            self.unit = python_weather.METRIC
        else:
            self.unit = python_weather.IMPERIAL

    async def update_data(self) -> Weather_Data:
        output = Weather_Data()
        async with python_weather.Client(unit=self.unit) as client:
            weather = await client.get(self.Location)
            output.date = weather.daily_forecasts[0].date
            output.current_temp = weather.daily_forecasts[0].hourly_forecasts[0].temperature
            output.current_humid = weather.daily_forecasts[0].hourly_forecasts[0].humidity
            output.current_wind = weather.daily_forecasts[0].hourly_forecasts[0].wind_speed
            output.chance_rain = weather.daily_forecasts[0].hourly_forecasts[0].chances_of_rain
            output.chance_dry = weather.daily_forecasts[0].hourly_forecasts[0].chances_of_remaining_dry
            self.checked = False
        return output
    def loop(self):
        while self.keep_updated:
            self.data = asyncio.run(self.update_data())
            self.checked = False
            time.sleep(self.clock)

if __name__ == '__main__':
    mp = Weather()
    mp.data = asyncio.run(mp.update_data())
    print(mp.data)
