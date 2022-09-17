# ePaper-Display-V2
Waveshare 7.5 ePaper display with XIAO-ESP32C3 board and 3Ah LiPo.
Get an image from backend web server (BW-BMP or BW-PNG) and draw it if there is a change.
Config is done via serial (USB).

runtime calculation with PNG and without battery self discharge for best wifi conditions
 sleep: 165mC/h = 0,046mAh
 wakeup+check without redraw: 35mC = 0,0097mAh
 wakeup+check+redraw: 200mC = 0,056mAh
 battery capacity: 10000000mC = 3000mAh
 lets use interval 5 min, redraw all once a hour
 consumption per hour: 165mC + 11 * 35mC + 1 * 200mC = 750mC -> 10800000mC / 750mC/h = 14400h = 600d
 consumption per hour: 0,046 + 11 * 0,0097mAh + 1 * 0,056mAh = 0,2087mAh -> 3000mAh / 0,2087mAh/h = 14374h = 598d
 to get a nice formula we use do some rounding
  n = wakeup / hour
  m = redraws / hour
  C = battery capacity in mC
  c = battery capacity in mAh
 to calculate hours from Coulomb
 t = C / ((n - m) * 35 + m * 200 + 165) = C / ( n * 35 + (m+1) * 165 ) = C / 35 / (n + 5m + 5))
  because 3600C = 1Ah -> C = c * 3600
 t = c * 3600 / 35 / ( n + 5m + 5) ~ c * 100 / (n + 5m + 5)
 to get days divide by 24 (just use 25)
 t ~ c * 4 / (n + 5m + 5)
 
 so time in days is aprox c * 4 / (n + 5m + 5). keep in mind it is only for best WiFi conditions and fast web response.
 for the example above (5 min wakeup, 1 refresh/h):
 t = 12000 / (12 + 5 + 5) = 545d
 
 if the display refresh is twice (n = 12, m = 2):
 t = 12000 / (12 + 10 + 5) = 324d
 
 wake 3 min + redraw once a hour:
 t = 12000 / (20 + 5 + 5) = 400d

wake 1 min + redraw once a hour:
 t = 12000 / (60 + 5 + 5) = 171d
 
 wake 1 min + redraw twice a hour:
 t = 12000 / (60 + 10 + 5) = 160d
