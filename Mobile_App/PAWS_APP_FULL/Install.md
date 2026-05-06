## Windows

First install Python and ensure it is  python 3.11
I used python 3.11.9 check with :
'python --version'

### Run to install

pip install --upgrade pip
py -3.11 -m venv kivy_env
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
kivy_env\Scripts\activate 
python.exe -m pip install --upgrade pip
pip install --upgrade pip setuptools wheel
pip install kivy
pip install bleak
pip install python_weather