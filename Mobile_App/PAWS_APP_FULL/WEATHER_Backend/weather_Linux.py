import asyncio
import time
import threading

import python_weather # https://pypi.org/project/python-weather/
from WEATHER_Backend.weather_structure import Weather_Data
#from weather_structure import   Weather_Data
class Weather:
    def __init__(self,  location:str = "Southampton", 
                        Clock: int = 10, 
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

        self.UI_Updater = None

    def start_updating(self):
        self.running = True
        threading.Thread(
            target=self._run_loop,
            daemon=True
        ).start()

    def stop_updating(self):
        self.running = False

    def _run_loop(self):
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)

        async def update_once():
            print("Updating weather data...")
            async with python_weather.Client() as client:
                weather = await client.get(self.Location)
                output = Weather_Data()
                output.date = weather.daily_forecasts[0].date.strftime("%Y-%m-%d")
                output.current_temp = weather.daily_forecasts[0].hourly_forecasts[0].temperature
                output.current_humid = weather.daily_forecasts[0].hourly_forecasts[0].humidity
                output.current_wind = weather.daily_forecasts[0].hourly_forecasts[0].wind_speed
                output.chance_rain = weather.daily_forecasts[0].hourly_forecasts[0].chances_of_rain
                output.chance_dry = weather.daily_forecasts[0].hourly_forecasts[0].chances_of_remaining_dry
                return output
        try:
            while self.running:
                data = loop.run_until_complete(update_once())
                self.UI_Updater(data)  # send result back
                time.sleep(self.clock)  # wait before next update

        finally:
            loop.close()
        
    async def update_data(self) -> Weather_Data:
        output = Weather_Data()
        print("Updating weather data...")
        async with python_weather.Client() as client: # unit=self.unit
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
        self.keep_updated = True

#if __name__ == '__main__':
    # Declare the client. The measuring unit used defaults to the metric system (celcius, km/h, etc.)
    
    #async def main():
    #    async with python_weather.Client() as client:
    #        weather = await client.get("New York")
    #        print(weather.temperature)
#
    #asyncio.run(main())
#    mp = Weather()
#    print(mp.data)
#    print(mp.Location)
#    print(mp.clock)
#    print(mp.unit)
#    data_caller_thread = threading.Thread(target=mp.loop) # daemon=True
#    data_caller_thread.start()
#    while True:
#        print(mp.data)
#        time.sleep(5)
