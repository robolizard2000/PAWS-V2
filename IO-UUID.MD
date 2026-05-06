## 12345000-0000-1000-8000-00805F9B34FB - Settings 
12345001-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Clock                - Seconds 
12345002-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Back-light-timer     - Minuits * 10 
12345003-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - LCD-Lock             - True for lock
12345004-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - Debug-Enable         - True to enable
12345010-0000-1000-8000-00805F9B34FB - Read|     |       - Char(20) - Service-name (debug) - 
12345011-0000-1000-8000-00805F9B34FB - Read|     |       - Char(20) - Local-name (debug)

## 12346000-0000-1000-8000-00805F9B34FB - Controls
12346001-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Temp-Setpoint        - *c *10 
12346002-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Temp-hysteresis      - *c *10       
12346003-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Humid-Setpoint       - %
12346004-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Humid-Hysteresis     - %
12346005-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Wind-Max             - mph *10
12346006-0000-1000-8000-00805F9B34FB - Read|Write|       - Int32    - Light-Min            - %
12346007-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - Light-override       - True to override 
12346008-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - Light-Set            - True for Light on
12346009-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - Window-override      - True to override
1234600A-0000-1000-8000-00805F9B34FB - Read|Write|       - Bool     - Window-Set           - True to open

## 12347000-0000-1000-8000-00805F9B34FB - Data-Reading    
12347001-0000-1000-8000-00805F9B34FB -     |     |Notify - Int32    - Temp                 - *c *100
12347002-0000-1000-8000-00805F9B34FB -     |     |Notify - Int32    - Humid                - % *100 
12347003-0000-1000-8000-00805F9B34FB -     |     |Notify - Int32    - Wind                 - mph * 10
12347004-0000-1000-8000-00805F9B34FB -     |     |Notify - Bool     - Rain                 - True for rain
12347005-0000-1000-8000-00805F9B34FB -     |     |Notify - Int32    - Light                - % *10